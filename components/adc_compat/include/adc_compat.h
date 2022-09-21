#ifdef __cplusplus
extern "C" {
#endif

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);
void adc_calibration_deinit(adc_cali_handle_t handle);
uint16_t adc_battery_voltage(adc_channel_t adc_channel);

#ifdef __cplusplus
}
#endif
