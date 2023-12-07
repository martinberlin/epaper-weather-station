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
#include "protocol_examples_common.h"
#include "esp_sntp.h"
#include "i2cdev.h"
// Your SPI epaper class
// Find yours here: https://github.com/martinberlin/cale-idf/wiki
#include <dke/depg1020bn.h>

EpdSpi io;             //    Configure the GPIOs using: idf.py menuconfig   -> section "Display configuration"
Depg1020bn display(io);

/* LASKA Kit v1 IO Config for SPI 
CONFIG_EINK_SPI_MOSI=23
CONFIG_EINK_SPI_CLK=18
CONFIG_EINK_SPI_CS=5
CONFIG_EINK_DC=17
CONFIG_EINK_RST=16
CONFIG_EINK_BUSY=4 */
// NVS non volatile storage
nvs_handle_t storage_handle;
// EPAPER power on IO
#define GPIO_EPAPER_POWER GPIO_NUM_2
// Fonts
#include <Ubuntu_M8pt8b.h>
#include <Ubuntu_B40pt7b.h>
#include <Ubuntu_M48pt8b.h>
#include <Ubuntu_B80pt8b.h>
// SENSIRION SCD CO2 sensor
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"
uint16_t scd4x_co2 = 0;
int32_t scd4x_temperature = 0;
int32_t scd4x_humidity = 0;
uint8_t scd4x_read_error = 0;
float scd4x_tem = 0;
float scd4x_hum = 0;

bool rtc_wakeup = false;
/**
┌───────────────────────────┐
│ CLOCK configuration       │ Device wakes up each N minutes
└───────────────────────────┘ Takes about 3.5 seconds to run the program
**/
#define DEEP_SLEEP_SECONDS 60 *30

uint64_t USEC = 1000000;
#include <logo/logo_skygate.h>
#include <logo/logo_fasani.h>

// You have to set these CONFIG value using: idf.py menuconfig --> DS3231 Configuration
#if 0
#define CONFIG_SCL_GPIO		7
#define CONFIG_SDA_GPIO		15
#define	CONFIG_TIMEZONE		9
#define NTP_SERVER 		"pool.ntp.org"
#endif

esp_err_t ds3231_initialization_status = ESP_OK;
#if CONFIG_SET_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif
#if CONFIG_GET_CLOCK
    #define NTP_SERVER " "
#endif

static const char *TAG = "WeatherST SCD40";

// I2C descriptor
i2c_dev_t dev;


extern "C"
{
    void app_main();
}

/**
 * @brief receives an image C array and sends it to the display buffer
 *        TODO: x is for the moment ignored, so it's always printed on x position: 0
 * 
 * @param image  C array created in https://javl.github.io/image2cpp/ (Needs also image_width image_height defined)
 * @param width  in px from the image
 * @param height 
 */
void image_draw(const char * image, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    uint32_t buffer_pointer = 0;
    uint32_t epd_pointer = 0;
    uint16_t buffer_max_x = width/8;
    
    for (uint16_t posy = y; posy<y+height; posy++) {

        for (uint16_t posx = 0; posx<buffer_max_x; posx++) {
            display.setRawBuf(epd_pointer, image[buffer_pointer]);
            buffer_pointer++;
            epd_pointer++;
        }
        epd_pointer = (display.width()/8) * posy;
    }
}

int16_t scd40_read() {
    int16_t error = 0;

    sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);
  
    // Clean up potential SCD40 states
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    error = scd4x_start_periodic_measurement();
    if (error) {
        ESP_LOGE(TAG, "Error executing scd4x_start_periodic_measurement(): %i\n", error);
        return 1;
    }
    printf("Waiting for first measurement... (5 sec)\n");
    uint8_t read_nr = 0;
    uint8_t reads_till_snapshot = 1;
    bool read_repeat = false;

    for (uint8_t c=0;c<100;++c) {
        // Read Measurement
        sensirion_i2c_hal_sleep_usec(100000);
        bool data_ready_flag = false;
        error = scd4x_get_data_ready_flag(&data_ready_flag);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_get_data_ready_flag(): %i\n", error);
            continue;
        }
        printf("ready_flag %d try:%d\n", (uint8_t)data_ready_flag, c);
        if (!data_ready_flag) {
            continue;
        }
        
        error = scd4x_read_measurement(&scd4x_co2, &scd4x_temperature, &scd4x_humidity);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_read_measurement(): %i\n", error);
        } else if (scd4x_co2 == 0) {
            ESP_LOGI(TAG, "Invalid sample detected, skipping.\n");
        } else {
            // If CO2 is major than this amount is probably a wrong reading
            if (scd4x_co2 > 2000 && read_repeat == false) {
                read_repeat = true;
                continue;
            }
            scd4x_stop_periodic_measurement();
            nvs_set_u16(storage_handle, "scd4x_co2", scd4x_co2);
            nvs_set_i32(storage_handle, "scd4x_tem", scd4x_temperature);
            nvs_set_i32(storage_handle, "scd4x_hum", scd4x_humidity);
            ESP_LOGI(TAG, "CO2 : %u", (int)scd4x_co2);
            ESP_LOGI(TAG, "Temp: %d mC", (int)scd4x_temperature);
            ESP_LOGI(TAG, "Humi: %d mRH", (int)scd4x_humidity);
            read_nr++;
            if (read_nr == reads_till_snapshot) break;
        }
    }
    scd4x_power_down();

    // Release I2C
    sensirion_i2c_hal_free();

    return error;
}

uint16_t generateRandom(uint16_t max) {
    if (max>0) {
        srand(esp_timer_get_time());
        return rand() % max;
    }
    return 0;
}

void delay_ms(uint32_t period_ms) {
    sys_delay_ms(period_ms);
}

void deep_sleep(uint16_t seconds_to_sleep) {
    // Turn off the 3.7 to 5V step-up and put all IO pins in INPUT mode
    uint8_t EP_CONTROL[] = {CONFIG_EINK_SPI_CLK, CONFIG_EINK_SPI_MOSI, CONFIG_EINK_SPI_MISO, CONFIG_EINK_SPI_CS};
    for (int io = 0; io < 4; io++) {
        gpio_set_level((gpio_num_t) EP_CONTROL[io], 0);
        gpio_set_direction((gpio_num_t) EP_CONTROL[io], GPIO_MODE_INPUT);
    }
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_SECONDS: %d seconds to wake-up", seconds_to_sleep);
    esp_sleep_enable_timer_wakeup(seconds_to_sleep * USEC);
    esp_deep_sleep_start();
}

void wakeup_cause()
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0: {
            printf("Wake up from ext0\n");
            break;
        }
        case ESP_SLEEP_WAKEUP_EXT1: {
            // C3 TODO: esp_sleep_get_ext1_wakeup_status -> No ext1 wakeup
         uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            if (wakeup_pin_mask != 0) {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                printf("Wake up from GPIO %d\n", pin);
            } else {
                printf("Wake up from GPIO\n");
            }

            rtc_wakeup = true;
            // Woke up from RTC, clear alarm flag
            
            break;
        }
        case ESP_SLEEP_WAKEUP_TIMER: {
            printf("Wake up from timer\n");
            break;
        }
        default:
            break;
    }
}


void app_main()
{
    gpio_set_direction(GPIO_EPAPER_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_EPAPER_POWER, 1);
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    scd40_read();
    scd4x_tem = (float)scd4x_temperature/1000;
    scd4x_hum = (float)scd4x_humidity/1000;

    display.init(false);
    display.setRotation(0);
    display.setTextColor(EPD_BLACK);
    display.setFont(&Ubuntu_M8pt8b);
    display.setCursor(6, display.height()-170);
    display.print("developed by");
    image_draw(logo_fasani, 0, display.height()-150, logo_fasani_width, logo_fasani_height);

    display.setFont(&Ubuntu_B80pt8b);
    uint16_t x_margin = 315;
    display.setCursor(x_margin, 200);
    display.printerf("%d", scd4x_co2);

    image_draw(logo_skygate, 0, 24, logo_skygate_width, logo_skygate_height);
    
    

    //CO2 cloud
    display.setFont(&Ubuntu_M48pt8b);
    display.setCursor(display.width()-280,200);
    display.setTextColor(EPD_WHITE);
    display.fillCircle(display.width()-200,150, 100, EPD_BLACK);
    display.fillCircle(display.width()-120,170, 80,EPD_BLACK);
    display.fillRect(display.width()-190, 202, 80, 50, EPD_BLACK);
    display.print("CO");
    display.setCursor(display.width()-140,220);
    display.print("2");
    display.setTextColor(EPD_BLACK);

    display.setFont(&Ubuntu_B80pt8b);
    display.setCursor(x_margin,400);
    // Draw ° since with the font does not work
    display.drawCircle(x_margin+345, 302, 16, EPD_BLACK);
    display.drawCircle(x_margin+345, 302, 15, EPD_BLACK);
    display.drawCircle(x_margin+345, 302, 14, EPD_BLACK);
    display.drawCircle(x_margin+345, 302, 13, EPD_BLACK);
    display.drawCircle(x_margin+345, 302, 12, EPD_BLACK);
    display.printerf("%.1f  C", scd4x_tem);

    display.setCursor(x_margin,600);
    display.printerf("%.1f%% H", scd4x_hum);

    display.update();

    deep_sleep(DEEP_SLEEP_SECONDS);
}
