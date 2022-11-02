//ESP32-S3 already in Kconfig
//#define SDA_GPIO 7
//#define SCL_GPIO 15
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdio.h>  // printf

#include "hm3301.h"
//ESP32-S3 already in Kconfig
//#define SDA_GPIO 7
//#define SCL_GPIO 15

static const char *TAG = "HM330x";

#define ENABLE_SEEED_GPIO GPIO_NUM_48

// Our sensor class:
HM330X sensor(0x40);

extern "C"
{
    void app_main();
}

void app_main() {
   // Turn on the SEEED sensor
    gpio_set_level(ENABLE_SEEED_GPIO, 1);
    // Initialize I2C
    i2c_dev_t dev;
    if (sensor.init(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t)CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(TAG, "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    } else {
        ESP_LOGI(TAG, "Dust sensor initialized");
    }

    printf("Waiting for first measurement...\n");
    uint8_t sensor_data[30];
    


    for (;;) {
        // Read Measurement
        printf("Read...\n");

        sensor.read_sensor_value(&dev, sensor_data);
        ESP_LOGI(TAG, "Standard Particles PM1:%d PM2_5:%d PM10:%d", 
        sensor.getPM1_sp(),sensor.getPM2dot5_sp(),sensor.getPM10_sp());

        ESP_LOGI(TAG, "Pollutants PM1:%d PM2_5:%d PM10:%d", 
        sensor.getPM1_pol(),sensor.getPM2dot5_pol(),sensor.getPM10_pol());

        ESP_LOG_BUFFER_HEX("RAW HEX", sensor_data, 29);
        // Wait 10 secs
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    gpio_set_level(ENABLE_SEEED_GPIO, 0);
}