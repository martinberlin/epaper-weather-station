//ESP32-S3 detect from where comes the power
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static const char *TAG = "POWER BY";

// STAT pin of TPS2113
#define TPS_POWER GPIO_NUM_5


extern "C"
{
    void app_main();
}

void app_main()
{
    while (1) {
        uint8_t powered_by = gpio_get_level(TPS_POWER);
        ESP_LOGI(TAG, "%d", powered_by);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}
