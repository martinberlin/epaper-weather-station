//ESP32-S3 deep sleep consumption test
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static const char *TAG = "SLEEP";
// Wake up every:
#define DEEP_SLEEP_SECONDS 240
uint64_t USEC = 1000000;

// Enable on HIGH 5V boost converter
#define GPIO_ENABLE_5V GPIO_NUM_38

void deep_sleep() {
    // Turn off the 3.7 to 5V step-up
    gpio_set_level(GPIO_ENABLE_5V, 0);
    ESP_LOGI(TAG, "DEEP_SLEEP_SECONDS: %d seconds to wake-up", DEEP_SLEEP_SECONDS);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_SECONDS * USEC);
    esp_deep_sleep_start();
}

extern "C"
{
    void app_main();
}

void app_main()
{
    deep_sleep();
}
