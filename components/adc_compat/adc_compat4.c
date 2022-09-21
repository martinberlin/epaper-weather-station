#include "adc_compat4.h"

static const char *ADC_TAG = "ADC compat";

bool adc_calibration_init(void)
{
    esp_err_t ret;
    bool cali_enable = false;
    // Check https://github.com/espressif/esp-idf/blob/release/v4.4/examples/peripherals/adc/single_read/single_read/main/single_read.c
    // For other MCU targets
    ret = esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP_FIT);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(ADC_TAG, "Calibration scheme not supported, skip software calibration");
    } else if (ret == ESP_ERR_INVALID_VERSION) {
        ESP_LOGW(ADC_TAG, "eFuse not burnt, skip software calibration");
    } else if (ret == ESP_OK) {
        cali_enable = true;
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, (adc_bits_width_t)ADC_WIDTH_BIT_DEFAULT, 0, &adc1_chars);
    } else {
        ESP_LOGE(ADC_TAG, "Invalid arg");
    }

    return cali_enable;
}

uint16_t adc_battery_voltage(adc1_channel_t adc_channel) {
    bool cali_enable = adc_calibration_init();
    int adc_raw = adc1_get_raw(adc_channel);
    //ADC1 config
    ESP_ERROR_CHECK(adc1_config_width((adc_bits_width_t)ADC_WIDTH_BIT_DEFAULT));
    ESP_ERROR_CHECK(adc1_config_channel_atten(adc_channel, ADC_ATTEN_DB_11));
    uint16_t voltage = 1000;
    if (cali_enable) {
        voltage = esp_adc_cal_raw_to_voltage(adc_raw, &adc1_chars);
        ESP_LOGI(ADC_TAG, "cali data: %d mV", voltage);
    }
    return voltage;
}
