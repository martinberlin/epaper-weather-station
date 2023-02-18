// RESEARCH FOR SPI HAT Cinwrite, PCB and Schematics            https://github.com/martinberlin/H-cinread-it895
// Note: This is just a simple example without RTC that takes measurements from a sensor
//       and draws circles in an epaepr screen
// Non-Volatile Storage (NVS) - borrrowed from esp-idf/examples/storage/nvs_rw_value
#include "nvs_flash.h"
#include "nvs.h"

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
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
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"

// Seeed dust sensor used:
// https://github.com/Seeed-Studio/Seeed_PM2_5_sensor_HM3301
#include "hm3301.h"
// Important: SET pin on low put's sensor to sleep (With IO32 didn't worked correctly)
// If you don't do this it will keep consuming after reading values
#define HM3301_SET_GPIO GPIO_NUM_14

// Note: Setting this to true will make a mess
//       Since it will draw mono partials over 4 gray mode that is not supported
#define DRAW_P10_USING_PARTIAL_UPDATE false
// Our sensor class with I2C address
HM330X sensor(0x40);
// Your SPI epaper class
// Find yours here: https://github.com/martinberlin/cale-idf/wiki
#include <gdew042t2Grays.h>
EpdSpi io;
Gdew042t2Grays display(io);

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 0)
#error "ESP_IDF version not supported. Please use IDF>=4.4"
#endif

// Fonts
#include <Ubuntu_M8pt8b.h>
#include <Ubuntu_M12pt8b.h>

// NVS non volatile storage
nvs_handle_t storage_handle;

/**
┌───────────────────────────┐
│ CLOCK configuration       │ Device wakes up each N minutes
└───────────────────────────┘
**/
#define DEEP_SLEEP_SECONDS 56

uint64_t USEC = 1000000;

// You have to set these CONFIG value using: idf.py menuconfig --> DS3231 Configuration
#if 0
#define CONFIG_SCL_GPIO 7
#define CONFIG_SDA_GPIO 15
#endif

static const char *TAG = "Particle draw";

// I2C descriptor
i2c_dev_t dev;
uint16_t maxx = 0;
uint16_t maxy = 0;

extern "C"
{
    void app_main();
}

uint16_t generateRandom(uint16_t max)
{
    if (max > 0)
    {
        srand(esp_timer_get_time());
        return rand() % max;
    }
    return 0;
}

void delay_ms(uint32_t period_ms)
{
    sys_delay_ms(period_ms);
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void deep_sleep(uint16_t seconds_to_sleep)
{
    // Turn off the 3.7 to 5V step-up and put all IO pins in INPUT mode
    uint8_t EP_CONTROL[] = {CONFIG_EINK_SPI_CLK, CONFIG_EINK_SPI_MOSI, CONFIG_EINK_SPI_MISO, CONFIG_EINK_SPI_CS};
    for (int io = 0; io < 4; io++)
    {
        gpio_set_level((gpio_num_t)EP_CONTROL[io], 0);
        gpio_set_direction((gpio_num_t)EP_CONTROL[io], GPIO_MODE_INPUT);
    }
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_SECONDS: %d seconds to wake-up", seconds_to_sleep);
    esp_sleep_enable_timer_wakeup(seconds_to_sleep * USEC);
    esp_deep_sleep_start();
}

void displayPrintLabels()
{
    display.printerf("1: %d 2.5: %d 10: %d", sensor.getPM1_sp(), sensor.getPM2dot5_sp(), sensor.getPM10_sp());
}

void readSensor()
{
    // RESET and initialize HM3301
    uint8_t sensor_data[30];

    if (sensor.init(&dev, I2C_NUM_0, (gpio_num_t)CONFIG_SDA_GPIO, (gpio_num_t)CONFIG_SCL_GPIO) != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not init device descriptor.");
        while (1)
        {
            vTaskDelay(1);
        }
    }
    else
    {
        // Let the particle sensor and fan start
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        sensor.read_sensor_value(&dev, sensor_data);
        ESP_LOGI(TAG, "Dust sensor initialized");
        // Debug RAW buffer
        // ESP_LOG_BUFFER_HEX("RAW HEX", sensor_data, 29);
    }
    // Put sensor in sleep mode after reading
    i2c_dev_delete(&dev);
    if (sensor.getPM2dot5_sp() > 60)
    {
        // Let the fan work 2 seconds more to ventilate the fumes#
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    gpio_set_level(HM3301_SET_GPIO, 0);

    // cursors
    uint16_t y_start = display.height();
    uint16_t x_cursor = 20;

    // Render HM3301 dust sensor here
    display.setTextColor(EPD_LIGHTGREY);
    y_start -= 30;

    // Avoid showing fake readings if the sensor goes nuts
    if (sensor.getPM2dot5_sp() < 9000)
    {
        uint8_t rad1 = 6;
        uint8_t rad2dot5 = 10;
        uint8_t rad10 = 18;
        for (uint16_t c=0; c<sensor.getPM1_sp(); c++) {
            display.fillCircle(generateRandom(maxx-rad1), generateRandom(maxy-rad1), rad1, EPD_LIGHTGREY);
        }
        for (uint16_t c=0; c<sensor.getPM2dot5_sp(); c++) {
            display.fillCircle(generateRandom(maxx-rad2dot5), generateRandom(maxy-rad2dot5), rad2dot5, EPD_DARKGREY);
        }
        #if DRAW_P10_USING_PARTIAL_UPDATE==false
        for (uint16_t c=0; c<sensor.getPM10_sp(); c++) {
            display.fillCircle(generateRandom(maxx-rad10), generateRandom(maxy-rad10), rad10, EPD_BLACK);
        }
        #endif

        y_start += 20;
        display.setCursor(x_cursor, y_start);
        displayPrintLabels();
        display.setTextColor(EPD_DARKGREY);
        display.setCursor(x_cursor-1, y_start-1);
        displayPrintLabels();

        display.update();

        #if DRAW_P10_USING_PARTIAL_UPDATE
            // Draw this using partial update
            display.setMonoMode(true);
            delay_ms(200);
            
            for (uint16_t c=0; c<sensor.getPM10_sp(); c++) {
                uint16_t rand_x = generateRandom(maxx-rad10);
                uint16_t rand_y= generateRandom(maxy-rad10);
                display.fillCircle(rand_x-rad10, rand_y-rad10, rad10, EPD_BLACK);
                display.updateWindow(rand_x-(rad10*2), rand_y-(rad10*2), rad10*2+1, rad10*2+1, true);
                delay_ms(50);
            }
        #endif
    }
    else
    {
        // If the PM 2.5 is major than 9000 you won't be able to read this anyways
        display.setFont(&Ubuntu_M12pt8b);
        display.print("Sensor readings wrong!");
    }
    /**
     * @brief Doing a lot of display actions corrupts main task from time to time
     *
     * SOLUTION
     * menuconfig and select:
     * Component config -> FreeRTOS -> Kernel
     * and update:
     * configCHECK_FOR_STACK_OVERFLOW: method 1
     */
    //display.update();

    // Wait some millis before switching off epaper otherwise last lines might not be printed
    // HOLD IO low in deepsleep
    gpio_hold_en(HM3301_SET_GPIO);
    gpio_deep_sleep_hold_en();
    delay_ms(100);

    deep_sleep(DEEP_SLEEP_SECONDS);
}

// NVS variable to know how many times the ESP32 wakes up
int16_t nvs_boots = 0;
uint8_t sleep_flag = 0;
uint8_t sleep_msg = 0;

void wakeup_cause()
{
    switch (esp_sleep_get_wakeup_cause())
    {
    case ESP_SLEEP_WAKEUP_EXT0:
    {
        printf("Wake up from ext0\n");
        break;
    }
    case ESP_SLEEP_WAKEUP_EXT1:
    {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask != 0)
        {
            int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
            printf("Wake up from GPIO %d\n", pin);
        }
        else
        {
            printf("Wake up from GPIO\n");
        }
        break;
    }
    case ESP_SLEEP_WAKEUP_TIMER:
    {
        printf("Wake up from timer\n");
        break;
    }
    default:
        printf("Wake up cause unknown: %d\n\n", esp_sleep_get_wakeup_cause());
        break;
    }
}

void app_main()
{
    // GPIO mode config
    gpio_set_direction(HM3301_SET_GPIO, GPIO_MODE_OUTPUT);
    // Disable GPIO hold otherwise won't relase the LOW state!
    gpio_hold_dis(HM3301_SET_GPIO);
    gpio_set_level(HM3301_SET_GPIO, 1);

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open("storage", NVS_READWRITE, &storage_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }

    // Maximum power saving (But slower WiFi which we use only to callibrate RTC)
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    // Determine wakeup cause and clear RTC alarm flag
    wakeup_cause(); // Needs I2C dev initialized

    // Read stored
    nvs_get_i16(storage_handle, "boots", &nvs_boots);

    ESP_LOGI(TAG, "-> NVS Boot count: %d", nvs_boots);
    nvs_boots++;
    // Set new value
    nvs_set_i16(storage_handle, "boots", nvs_boots);
    // Reset sleep msg flag so it prints it again before going to sleep
    if (sleep_msg)
    {
        nvs_set_u8(storage_handle, "sleep_msg", 0);
    }

    display.init(false);
    display.setMonoMode(false);
    display.setRotation(0);
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);

    display.setFont(&Ubuntu_M8pt8b);
    maxx = display.width();
    maxy = display.height();
    readSensor();
}
