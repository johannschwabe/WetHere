#include "IoTManager.h"
#include "esp_event.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "IOT_MANAGER";

// Static event handler for WiFi events
void IoTManager::wifi_event_handler(void* arg, esp_event_base_t event_base,
                                   int32_t event_id, void* event_data)
{
    IoTManager* manager = static_cast<IoTManager*>(arg);
    
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (manager->retry_num < manager->max_retry) {
            esp_wifi_connect();
            manager->retry_num++;
            ESP_LOGI(TAG, "Retrying to connect to the AP (attempt %d of %d)", 
                    manager->retry_num, manager->max_retry);
        } else {
            xEventGroupSetBits(manager->wifi_event_group, WIFI_FAIL_BIT);
            manager->wifi_connected = false;
        }
        ESP_LOGI(TAG,"Connect to the AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        manager->retry_num = 0;
        xEventGroupSetBits(manager->wifi_event_group, WIFI_CONNECTED_BIT);
        manager->wifi_connected = true;
    }
}

// Static HTTP event handler
esp_err_t IoTManager::http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (evt->data_len > 0) {
                printf("%.*s", evt->data_len, (char*)evt->data);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

esp_err_t IoTManager::init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret == ESP_OK) {
        nvs_initialized = true;
    }
    
    return ret;
}

IoTManager::IoTManager(const char* wifi_ssid, 
                     const char* wifi_password, 
                     const char* url, 
                     int device_identifier,
                     int max_retries)
    : ssid(wifi_ssid), 
      password(wifi_password), 
      server_url(url),
      device_id(device_identifier),
      retry_num(0),
      max_retry(max_retries),
      wifi_connected(false),
      nvs_initialized(false)
{
    // Create FreeRTOS event group for WiFi events
    wifi_event_group = xEventGroupCreate();
}

IoTManager::~IoTManager()
{
    // Clean up resources
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
    }
    
    // Disconnect WiFi if connected
    if (wifi_connected) {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
}

esp_err_t IoTManager::init()
{
    // Initialize NVS
    esp_err_t ret = init_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS");
        return ret;
    }
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create default WiFi station
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      this,
                                                      NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                      IP_EVENT_STA_GOT_IP,
                                                      &wifi_event_handler,
                                                      this,
                                                      NULL));
    
    // Configure WiFi settings
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, password);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    // Set WiFi mode and config
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization completed");
    
    // Wait for WiFi connection or failure
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to SSID: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to SSID: %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "Unexpected event");
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t IoTManager::sendData(const char* data_json)
{
    if (!wifi_connected) {
        ESP_LOGE(TAG, "WiFi not connected, cannot send data");
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = server_url;
    config.method = HTTP_METHOD_POST;
    config.event_handler = http_event_handler;
    
    // Initialize HTTP client
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    // Set HTTP headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    // Set POST data
    esp_http_client_set_post_field(client, data_json, strlen(data_json));
    
    // Perform HTTP request
    esp_err_t err = esp_http_client_perform(client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);
    } else {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
    }
    
    // Clean up
    esp_http_client_cleanup(client);
    
    return err;
}

esp_err_t IoTManager::reconnect()
{
    if (wifi_connected) {
        return ESP_OK; // Already connected
    }
    
    retry_num = 0;
    esp_wifi_connect();
    
    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         10000 / portTICK_PERIOD_MS); // 10 second timeout
    
    if (bits & WIFI_CONNECTED_BIT) {
        wifi_connected = true;
        return ESP_OK;
    } else {
        return ESP_FAIL;
    }
}