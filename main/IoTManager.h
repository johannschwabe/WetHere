#pragma once

#include "esp_wifi.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string>

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

class IoTManager {
private:
    // WiFi credentials
    const char* ssid;
    const char* password;
    
    // Server details
    const char* server_url;
    
    // Device identity
    std::string device_id;
    
    // FreeRTOS event group to signal WiFi events
    EventGroupHandle_t wifi_event_group;
    
    // Retry count for WiFi connection
    int retry_num;
    const int max_retry;
    
    // Flags
    bool wifi_connected;
    bool nvs_initialized;
    
    // Event handler for WiFi events
    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data);
    
    // HTTP client event handler
    static esp_err_t http_event_handler(esp_http_client_event_t *evt);
    
    // Initialize non-volatile storage (NVS)
    esp_err_t init_nvs();
    
public:
    IoTManager(const char* wifi_ssid, 
               const char* wifi_password, 
               const char* url, 
               const char* device_identifier,
               int max_retries = 5);
    
    ~IoTManager();
    
    // Initialize and connect to WiFi
    esp_err_t init();
    
    // Check if connected to WiFi
    bool isConnected() const { return wifi_connected; }
    
    // Send data to server
    esp_err_t sendData(const char* data_json);
    
    // Reconnect to WiFi if disconnected
    esp_err_t reconnect();
    
    // Get device ID
    const std::string& getDeviceId() const { return device_id; }
};