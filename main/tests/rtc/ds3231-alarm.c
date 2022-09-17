#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"

#include "ds3231.h"

// DS3231 INT pin is pulled High and goes to this S3 GPIO:
#define GPIO_RTC_INT GPIO_NUM_6


#if CONFIG_GET_CLOCK
    #define NTP_SERVER " "
#endif


static const char *TAG = "DS3213";

RTC_DATA_ATTR static int boot_count = 0;


void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void getClock(void *pvParameters)
{
    // Initialize RTC
    i2c_dev_t dev;
    if (ds3231_init_desc(&dev, I2C_NUM_0, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

    // Initialise the xLastWakeTime variable with the current time.
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Get RTC date and time
    //
        float temp;
        struct tm rtcinfo;

        if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
            ESP_LOGE(pcTaskGetName(0), "Could not get temperature.");
            while (1) { vTaskDelay(1); }
        }

        if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
            ESP_LOGE(pcTaskGetName(0), "Could not get time.");
            while (1) { vTaskDelay(1); }
        }

        rtcinfo.tm_min += 1;
        // Set a test alarm
        ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2);
    // i2c_dev_t, ds3231_alarm_t alarms, struct tm *time1,ds3231_alarm1_rate_t option1, struct tm *time2, ds3231_alarm2_rate_t option2
        ds3231_set_alarm(&dev, DS3231_ALARM_2, &rtcinfo, 0,  &rtcinfo, DS3231_ALARM2_MATCH_MINHOUR);
        ds3231_enable_alarm_ints(&dev, DS3231_ALARM_2);

        ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, %.2f deg Cel", 
            rtcinfo.tm_year, rtcinfo.tm_mon + 1,
            rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, temp);
	while (1) {
    vTaskDelayUntil(&xLastWakeTime, 1000);
    }
}


void app_main()
{
    gpio_set_direction(GPIO_RTC_INT, GPIO_MODE_INPUT);

    ++boot_count;
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);
    ESP_LOGI(TAG, "Boot count: %d", boot_count);
    

#if CONFIG_GET_CLOCK
    // Get clock
    xTaskCreate(getClock, "getClock", 1024*4, NULL, 2, NULL);
#endif

    while (true) {
        // Read GPIO state
        if ( gpio_get_level(GPIO_RTC_INT) == 0) {
            ESP_LOGI(TAG, "RTC alarm trigered: %d", gpio_get_level(GPIO_RTC_INT));
            
        } else {
            ESP_LOGI(TAG, "IO6: %d", gpio_get_level(GPIO_RTC_INT));
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
