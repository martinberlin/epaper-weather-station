// Non-Volatile Storage (NVS) - borrrowed from esp-idf/examples/storage/nvs_rw_value
#include "nvs_flash.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
// SCD4x
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

// IMPORTANT: Needs an EPDiy board
// https://github.com/vroland/epdiy
#include "epd_driver.h"
#include "epd_highlevel.h"
// Fonts. EPDiy fonts are prefixed by "e" in /components/big-fonts
#include "e_ubuntu_b_120.h"
#include "e_ubuntu_b_80.h"
#include "e_ubuntu_b_40.h"
#define FONT_B_UBUNTU_120 ubuntu_b_120
#define FONT_UBUNTU_80 e_ubuntu_b_80
#define FONT_UBUNTU_40 e_ubuntu_b_40

//#define FONT_TEXT_2 FiraSans24

#define WAVEFORM EPD_BUILTIN_WAVEFORM
EpdiyHighlevelState hl;
uint8_t temperature = 25;
EpdFontProperties font_props;
// EPD framebuffer
uint8_t* fb;
// Station will refresh every:
#define DEEP_SLEEP_MINUTES 2
uint64_t USEC = 1000000;

/** Drawing mode
 *  Use use_partial_update false, random_x disabled, and reset_every_x 1 for the cleanest mode
 *  Use all other combinations to experiment and make an unique clock
 */
// Use partial update for HH:MM and temperature. Leaves ghosts with EPDiy V5 boards and 9.7"
bool use_partial_update = false;
// Do a full EPD clear screen every x boots:
uint8_t reset_every_x = 1;
// Random x: Disabled on 0 and up to 255. Moves the X so the numbers are not marked in the epaper display creating permanent ghosts
uint8_t random_x = 255;

#define SCL_GPIO		14  // Do not use IO 12, since ESP32 will fail to boot if pulled-high
#define SDA_GPIO		13  // B3 in Inkster, with removed pull-down R40 (Otherwise I2C won't work)

static const char *TAG = "CO2_ST";

extern "C"
{
    void app_main();
}

void deep_sleep() {
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_MINUTES: %d mins to wake-up", DEEP_SLEEP_MINUTES);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_MINUTES * 60 * USEC);
    esp_deep_sleep_start();
}

uint16_t generateRandom(uint16_t max) {
    if (max>0) {
        srand(esp_timer_get_time());
        return rand() % max;
    }
    return 0;
}

// Flag to know that we've synced the hour with timeQuery request
int16_t nvs_boots = 0;

void logo_co2(int x, int y, EpdFontProperties font_props) {
    epd_fill_circle(x-100, y-60, 100, 0, fb);
    epd_fill_circle(x-50 , y-45, 120, 0, fb);
    epd_fill_circle(x, y-110, 60, 0, fb);
    epd_fill_circle(x+60, y-90, 40, 0, fb);
    epd_fill_circle(x+60, y-30, 58, 0, fb);
    font_props.fg_color = 15;
    x -= 170;
    epd_write_string(&FONT_UBUNTU_80, "CO", &x, &y, fb, &font_props);
    y -= 195;
    x -= 8;
    font_props.fg_color = 10;
    epd_write_string(&FONT_UBUNTU_40, "2", &x, &y, fb, &font_props);
}

void scd_render_co2(uint16_t co2, int x, int y, EpdFontProperties font_props) {
    logo_co2(x, y, font_props);
    x += 140;
    y += 10;
    font_props.fg_color = 0;
    char textbuffer[6];
    snprintf(textbuffer, sizeof(textbuffer), "%d", co2);
    epd_write_string(&FONT_B_UBUNTU_120, textbuffer, &x, &y, fb, &font_props);
}

void scd_render_temp(double temp, int x, int y, EpdFontProperties font_props) {
    x += 140;
    char textbuffer[6];
    snprintf(textbuffer, sizeof(textbuffer), "%.1f", temp);
    epd_write_string(&FONT_UBUNTU_80, textbuffer, &x, &y, fb, &font_props);
    y -= 200;
    epd_write_string(&FONT_UBUNTU_80, "°C", &x, &y, fb, &font_props);
    
}

void scd_render_h(double hum, int x, int y, EpdFontProperties font_props) {
    //logo_co2(x, y, font_props);
    x += 140;
    char textbuffer[6];
    snprintf(textbuffer, sizeof(textbuffer), "%.1f", hum);
    epd_write_string(&FONT_UBUNTU_80, textbuffer, &x, &y, fb, &font_props);
    y -= 200;
    epd_write_string(&FONT_UBUNTU_80, "% h", &x, &y, fb, &font_props);
}

void epd_print_error(char * message) {
    int x = 100; //EPD_WIDTH/2-300
    int y = 200;

    font_props.bg_color = 12;
    font_props.fg_color = 0;
    EpdRect area = {
        .x = x,
        .y = y,
        .width = EPD_WIDTH-x,
        .height = 200
    };
    epd_poweron();
    epd_fill_rect(area, 200, fb);
    x += 20;
    y += 120;
    epd_write_string(&FONT_UBUNTU_40, message, &x, &y, fb, &font_props);
    
    epd_hl_update_area(&hl, MODE_GC16, temperature, area);
    vTaskDelay(1000);
    epd_poweroff();
    deep_sleep();
}

void scd_read() {
    int16_t error = 0;
    font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_LEFT;
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
        epd_print_error((char*)"Please insert sensor");
        deep_sleep();
    }

    printf("Waiting for first measurement... (5 sec)\n");
    bool data_ready_flag = false;
    for (uint8_t c=0;c<100;++c) {
        // Read Measurement
        sensirion_i2c_hal_sleep_usec(100000);
        //bool data_ready_flag = false;
        error = scd4x_get_data_ready_flag(&data_ready_flag);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_get_data_ready_flag(): %i\n", error);
            continue;
        }
        if (data_ready_flag) {
            break;
        }

    }
    if (!data_ready_flag) {
        ESP_LOGE(TAG, "SCD4x ready flag is not coming in time");
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
    
    
        epd_poweron();

        int cursor_x = 260;
        int cursor_y = 250;
        font_props.fg_color = 0;
        scd_render_co2(co2, cursor_x, cursor_y, font_props);

        cursor_y+=250;
        scd_render_temp(tem, cursor_x, cursor_y, font_props);

        cursor_y+=250;
        scd_render_h(hum, cursor_x, cursor_y, font_props);

        epd_hl_update_screen(&hl, MODE_GL16, temperature);
        epd_poweroff();
    }

    deep_sleep();
}

void app_main()
{
    printf("EPD width: %d height: %d\n\n", EPD_WIDTH, EPD_HEIGHT);
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle_t my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } 
    // Read stored
    nvs_get_i16(my_handle, "boots", &nvs_boots);
    ESP_LOGI(TAG, "-> NVS Boot count: %d", nvs_boots);
    nvs_boots++;
    // Set new value
    nvs_set_i16(my_handle, "boots", nvs_boots);

    epd_init(EPD_OPTIONS_DEFAULT);
    hl = epd_hl_init(WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);
    epd_set_rotation(EPD_ROT_INVERTED_LANDSCAPE);

    epd_poweron();
    if (nvs_boots%reset_every_x == 0) {
        printf("EPD clear triggered on %d\n", reset_every_x); 
        epd_clear();
    }
    epd_poweroff();
    // Initialize SCD40
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", SDA_GPIO);
    scd_read();
}

