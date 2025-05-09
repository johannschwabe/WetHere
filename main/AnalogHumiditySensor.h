#pragma once

#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"

class AnalogHumiditySensor {
private:
    const adc1_channel_t adc_channel;
    const int samples_count;
    esp_adc_cal_characteristics_t adc_chars;
    bool initialized;

    // Calibration parameters
    float dry_value;    // ADC reading when humidity is 0%
    float wet_value;    // ADC reading when humidity is 100%

    // Manufacturer-specific coefficients
    float a, b, c;      // For non-linear calibration

    // Perform multiple readings and average them to reduce noise
    uint32_t get_adc_reading();

    // Temperature compensation (if sensor provides temperature)
    float compensate_for_temperature(float raw_humidity, float temperature_c);

public:
    // Constructor with configurable ADC channel and sample count
    AnalogHumiditySensor(adc1_channel_t channel = ADC1_CHANNEL_0,
                         int samples = 10,
                         float dry_val = 4400.0,
                         float wet_val = 1700.0);

    ~AnalogHumiditySensor();

    // Initialize the sensor
    esp_err_t init();

    // Read raw ADC value
    esp_err_t readRawValue(uint32_t *raw_value);

    // Read voltage
    esp_err_t readVoltage(float *voltage);

    // Read humidity as a percentage
    esp_err_t readHumidity(float *humidity);

    // Calibrate the sensor
    void calibrate(float new_dry_value, float new_wet_value);

    // Set non-linear calibration coefficients
    void setCalibrationCoefficients(float coeff_a, float coeff_b, float coeff_c);
};