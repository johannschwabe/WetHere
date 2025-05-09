#include "AnalogHumiditySensor.h"
#include "IoTManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "credentials.h"
static const char *TAG = "HUMIDITY_IOT";

// Unique device ID - you can use MAC address or a custom ID
#define DEVICE_ID "humidity_sensor_1"  // Give each ESP a unique ID

// Time between readings (30 minutes = 1800 seconds)
#define SEND_INTERVAL_SEC 1800

// Task handle for the humidity monitoring task
TaskHandle_t humidity_task_handle = NULL;

// Function prototypes
void humidity_monitoring_task(void *pvParameters);
void deep_sleep_task(void *pvParameters);

// Define a struct to hold our sensor and IoT manager instances
typedef struct {
    AnalogHumiditySensor *sensor;
    IoTManager *iot;
} AppContext;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Humidity IoT Sensor starting...");

    // Allocate memory for our application context
    AppContext* context = new AppContext;

    // Create sensor instance
    context->sensor = new AnalogHumiditySensor(ADC1_CHANNEL_0,  // ADC channel
                                              15,               // Sample count
                                              4400.0,           // Dry value in normal air
                                              1700.0);          // Wet value in water

    // Create IoT manager instance
    context->iot = new IoTManager(WIFI_SSID,
                                 WIFI_PASSWORD,
                                 SERVER_URL,
                                 DEVICE_ID);

    // Initialize the sensor
    esp_err_t ret = context->sensor->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor");
        delete context->sensor;
        delete context->iot;
        delete context;
        return;
    }

    // Initialize WiFi and network
    ret = context->iot->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        // Continue anyway, we'll try to reconnect later
    }

    // Create the humidity monitoring task
    xTaskCreate(humidity_monitoring_task, "humidity_task", 8192, context, 5, &humidity_task_handle);

    ESP_LOGI(TAG, "Humidity IoT Sensor initialized successfully");

    // Note: The main task will end here, but the humidity_monitoring_task will continue running
}

// Main task that reads sensor and sends data
void humidity_monitoring_task(void *pvParameters)
{
    AppContext* context = static_cast<AppContext*>(pvParameters);
    AnalogHumiditySensor* sensor = context->sensor;
    IoTManager* iot = context->iot;

    // Variables for sensor readings
    uint32_t raw_value;
    float voltage, humidity;

    while (1) {
        // Read raw ADC value
        sensor->readRawValue(&raw_value);
        ESP_LOGI(TAG, "Raw ADC: %ld", raw_value);

        // Read voltage
        sensor->readVoltage(&voltage);
        ESP_LOGI(TAG, "Voltage: %.2f V", voltage);

        // Read humidity
        sensor->readHumidity(&humidity);
        ESP_LOGI(TAG, "Humidity: %.2f %%", humidity);

        // If not connected to WiFi, try to reconnect
        if (!iot->isConnected()) {
            ESP_LOGI(TAG, "Reconnecting to WiFi...");
            iot->reconnect();
        }

        // If connected, send data to server
        if (iot->isConnected()) {
            // Create JSON object
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "device_id", iot->getDeviceId().c_str());
            cJSON_AddNumberToObject(root, "humidity", humidity);
            cJSON_AddNumberToObject(root, "voltage", voltage);
            cJSON_AddNumberToObject(root, "raw_value", raw_value);

            // Get the formatted JSON string
            char *post_data = cJSON_Print(root);

            // Send data to server
            ESP_LOGI(TAG, "Sending data to server: %s", post_data);
            esp_err_t result = iot->sendData(post_data);

            if (result == ESP_OK) {
                ESP_LOGI(TAG, "Data sent successfully");
            } else {
                ESP_LOGE(TAG, "Failed to send data: %s", esp_err_to_name(result));
            }

            // Free JSON resources
            cJSON_free(post_data);
            cJSON_Delete(root);
        } else {
            ESP_LOGE(TAG, "WiFi not connected, data not sent");
        }

        // Option 1: Use deep sleep to save power
        //Create a task to put the system to sleep after some delay
         xTaskCreate(deep_sleep_task, "sleep_task", 2048, NULL, 5, NULL);

        // Option 2: Stay awake and just wait for the next interval
        //ESP_LOGI(TAG, "Waiting %d seconds until next reading...", SEND_INTERVAL_SEC);
        //vTaskDelay(SEND_INTERVAL_SEC * 1000 / portTICK_PERIOD_MS);
    }
}

// Optional task to put the system into deep sleep
void deep_sleep_task(void *pvParameters)
{
    // Wait for a moment to ensure data transmission completes
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Going to deep sleep for %d seconds", SEND_INTERVAL_SEC);

    // Set wakeup time
    esp_sleep_enable_timer_wakeup(SEND_INTERVAL_SEC * 1000000ULL); // Time in microseconds

    // Enter deep sleep
    esp_deep_sleep_start();

    // This line will never be reached
    vTaskDelete(NULL);
}