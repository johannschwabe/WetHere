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

// Unique device ID
#define DEVICE_ID 1  // Give each ESP a unique ID

// Time between readings (30 minutes = 1800 seconds)
// #define SEND_INTERVAL_SEC 1800
#define SEND_INTERVAL_SEC 300

// Define a struct to hold our sensor and IoT manager instances
typedef struct {
    AnalogHumiditySensor *sensor;
    IoTManager *iot;
} AppContext;

// Function to send data and go to sleep
void send_data_and_sleep(AnalogHumiditySensor* sensor, IoTManager* iot);

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

    // Instead of creating a separate task, perform the measurement and send directly
    send_data_and_sleep(context->sensor, context->iot);

    // Cleanup if we ever wake up without sleeping (shouldn't happen)
    delete context->sensor;
    delete context->iot;
    delete context;
}

// Function to send data and go to sleep
void send_data_and_sleep(AnalogHumiditySensor* sensor, IoTManager* iot)
{
    // Variables for sensor readings
    uint32_t raw_value;
    float voltage, humidity;

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
        cJSON_AddNumberToObject(root, "sensor_id", iot->getDeviceId());
        cJSON_AddNumberToObject(root, "humidity", humidity);
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

    // Wait a moment to ensure data transmission completes
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Going to deep sleep for %d seconds", SEND_INTERVAL_SEC);

    // Set wakeup time
    esp_sleep_enable_timer_wakeup(SEND_INTERVAL_SEC * 1000000ULL); // Time in microseconds

    // Enter deep sleep
    esp_deep_sleep_start();
}