#ifdef __cplusplus
extern "C" {
#endif

#include "esp_log.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

// IDF 4.4
bool adc_calibration_init(void);
uint16_t adc_battery_voltage(adc1_channel_t adc_channel);

#ifdef __cplusplus
}
#endif