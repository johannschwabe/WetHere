#include "AnalogHumiditySensor.h"
#include "driver/adc.h"
#include "esp_log.h"

static const char *TAG = "ANALOG_HUMIDITY_SENSOR";

// Perform multiple readings and average them to reduce noise
uint32_t AnalogHumiditySensor::get_adc_reading() {
    uint32_t adc_reading = 0;

    // Take multiple samples and average to reduce noise
    for (int i = 0; i < samples_count; i++) {
        // Add small delay between readings for stability
        vTaskDelay(pdMS_TO_TICKS(1));
        adc_reading += adc1_get_raw(adc_channel);
    }

    // Return average
    return adc_reading / samples_count;
}

// Constructor with configurable ADC channel and sample count
AnalogHumiditySensor::AnalogHumiditySensor(adc1_channel_t channel,
                     int samples,
                     float dry_val,
                     float wet_val)
    : adc_channel(channel),
      samples_count(samples),
      initialized(false),
      dry_value(dry_val),
      wet_value(wet_val),
      // Default polynomial coefficients for non-linear calibration
      a(0.0f), b(1.0f), c(0.0f) {}

AnalogHumiditySensor::~AnalogHumiditySensor() {
    // Nothing specific to clean up for ADC
}

// Initialize the sensor
esp_err_t AnalogHumiditySensor::init() {
    // Configure ADC
    esp_err_t ret;

    // Configure ADC1 width (12-bit resolution)
    adc1_config_width(ADC_WIDTH_BIT_12);  // 12-bit resolution (0-4095)

    // Configure the ADC channel with 11dB attenuation for full voltage range
    adc1_config_channel_atten(adc_channel, ADC_ATTEN_DB_11); // Full range: 0-3.3V

    // Characterize ADC for more accurate voltage readings
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    // Analog sensors often need warm-up time, especially on first reading
    vTaskDelay(pdMS_TO_TICKS(100));

    // Take a few readings to stabilize the sensor
    for (int i = 0; i < 5; i++) {
        get_adc_reading();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    initialized = true;
    ESP_LOGI(TAG, "Analog humidity sensor initialized on ADC1 channel %d", adc_channel);
    ESP_LOGI(TAG, "Calibration values: Dry = %.1f, Wet = %.1f", dry_value, wet_value);

    return ESP_OK;
}

// Read raw ADC value
esp_err_t AnalogHumiditySensor::readRawValue(uint32_t *raw_value) {
    if (!initialized) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    *raw_value = get_adc_reading();
    return ESP_OK;
}

// Read voltage
esp_err_t AnalogHumiditySensor::readVoltage(float *voltage) {
    if (!initialized) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t raw_value = get_adc_reading();
    *voltage = esp_adc_cal_raw_to_voltage(raw_value, &adc_chars) / 1000.0f; // Convert mV to V

    return ESP_OK;
}

esp_err_t AnalogHumiditySensor::readHumidity(float *humidity) {
    if (!initialized) {
        ESP_LOGE(TAG, "Sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t raw_value = get_adc_reading();

    // Map the ADC reading to a humidity percentage
    // Since higher value means drier, we invert the relationship
    *humidity = 100.0f - ((raw_value - wet_value) * 100.0f / (dry_value - wet_value));

    // Clamp the result to valid range
    if (*humidity < 0.0f) *humidity = 0.0f;
    if (*humidity > 100.0f) *humidity = 100.0f;

    return ESP_OK;
}


// Calibrate the sensor
void AnalogHumiditySensor::calibrate(float new_dry_value, float new_wet_value) {
    dry_value = new_dry_value;
    wet_value = new_wet_value;
    ESP_LOGI(TAG, "Sensor calibrated with: Dry = %.1f, Wet = %.1f", dry_value, wet_value);
}

// Set non-linear calibration coefficients
void AnalogHumiditySensor::setCalibrationCoefficients(float coeff_a, float coeff_b, float coeff_c) {
    a = coeff_a;
    b = coeff_b;
    c = coeff_c;
    ESP_LOGI(TAG, "Non-linear calibration set: a=%.4f, b=%.4f, c=%.4f", a, b, c);
}