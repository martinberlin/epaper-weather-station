//ESP32-S3 already in Kconfig
//#define SDA_GPIO 7
//#define SCL_GPIO 15
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
static const char *TAG = "SCD40";


#include <stdio.h>  // printf

#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

void app_main() {
    int16_t error = 0;
    //int16_t sensirion_i2c_hal_init(int gpio_sda, int gpio_scl);
    sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);

    // Clean up potential SCD40 states
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    uint16_t serial_0;
    uint16_t serial_1;
    uint16_t serial_2;
    error = scd4x_get_serial_number(&serial_0, &serial_1, &serial_2);
    if (error) {
        printf("Error executing scd4x_get_serial_number(): %i\n", error);
    } else {
        ESP_LOGI(TAG, "serial: 0x%04x%04x%04x\n", serial_0, serial_1, serial_2);
    }

    // Start Measurement

    error = scd4x_start_periodic_measurement();
    if (error) {
        ESP_LOGE(TAG, "Error executing scd4x_start_periodic_measurement(): %i\n", error);
    }

    printf("Waiting for first measurement... (5 sec)\n");

    for (;;) {
        // Read Measurement
        sensirion_i2c_hal_sleep_usec(100000);
        bool data_ready_flag = false;
        error = scd4x_get_data_ready_flag(&data_ready_flag);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_get_data_ready_flag(): %i\n", error);
            continue;
        }
        if (!data_ready_flag) {
            continue;
        }

        uint16_t co2;
        int32_t temperature;
        int32_t humidity;
        error = scd4x_read_measurement(&co2, &temperature, &humidity);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_read_measurement(): %i\n", error);
        } else if (co2 == 0) {
            ESP_LOGI(TAG, "Invalid sample detected, skipping.\n");
        } else {
            float tem = (float)temperature/1000;
            float hum = (float)humidity/1000;
            
            ESP_LOGI(TAG, "CO2 : %u", co2);
            ESP_LOGI(TAG, "Temp: %d m°C %.1f C", temperature, tem);
            ESP_LOGI(TAG, "Humi: %d mRH %.1f %%\n", humidity, hum);
        }
        // Wait 10 secs
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}