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
#include "driver/gpio.h"
// Addressable status LED in menuconfig: Component config → WS2812 RGB LED
// Please note that we are using IO14 for I2C SCL, so we will just turn it low after end of communication (Or simply disconnect it)
// #include "ws2812_led.h"
// Test logo image
#include "logo/serra.h"
// SCD4x
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"

// IMPORTANT: Needs an EPDiy board
// https://github.com/vroland/epdiy
#include "epd_driver.h"
#include "epd_highlevel.h"
// Board buttons (pulled down -> https://raw.githubusercontent.com/mcer12/Inkster-ESP32/main/Resources/Inkster_v1.3_SCHEMATIC.pdf)
#define BUTTON1 GPIO_NUM_36
#define BUTTON2 GPIO_NUM_39
bool button1_wakeup = false;
bool button2_wakeup = false;
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
#define DEEP_SLEEP_MINUTES 20
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
    vTaskDelay(200);
    epd_poweroff();
    deep_sleep();
}

void draw_logo(uint16_t x, uint16_t y) {
      EpdRect logo_area = {
      .x = x,
      .y = y,
      .width = logo_width,
      .height = logo_height
  };
  epd_draw_rotated_image(logo_area, logo_data, fb);
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
        scd4x_stop_periodic_measurement();
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
        // Demo logo
        draw_logo(60, EPD_HEIGHT/2+30);

        epd_hl_update_screen(&hl, MODE_GL16, temperature);
        epd_poweroff();
    }
    
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI(TAG, "scd4x_power_down()");
    scd4x_power_down();
    sensirion_i2c_hal_free();
    
    // Disable I2C and hold it while deep sleep:
    gpio_set_direction((gpio_num_t) SCL_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t) SCL_GPIO, 0);
    gpio_hold_en((gpio_num_t) SCL_GPIO);
    gpio_deep_sleep_hold_en();

    deep_sleep();
}

void present_tab2() {
    scd4x_power_down();
    uint16_t a_x = 100;
    uint16_t a_y = 200;
    uint16_t a_w = 500;
    uint16_t a_h = 300;
    EpdRect area = {
        .x = a_x,
        .y = a_y,
        .width = a_w,
        .height = a_h
    };
    int cursor_x = 100;
    int cursor_y = 70;
    font_props.fg_color = 7;
    epd_write_string(&FONT_UBUNTU_40, "Designed by", &cursor_x, &cursor_y, fb, &font_props);
    epd_fill_rect(area, 0, fb);
    font_props.fg_color = 15;
    cursor_x = 180;
    cursor_y = 330;
    epd_write_string(&FONT_UBUNTU_40, "FASANI", &cursor_x, &cursor_y, fb, &font_props);
    cursor_x = 240;
    cursor_y += 10;
    epd_write_string(&FONT_UBUNTU_40, "CORP.", &cursor_x, &cursor_y, fb, &font_props);
    //                side 1 x   , y      , side 2  x, y          , side 3      
    epd_fill_triangle(a_x+a_w-101, a_y+a_h, a_x+a_w+1, a_y+a_h-100, a_x+a_w+1, a_y+a_h, 255, fb);

    font_props.fg_color = 7;
    cursor_x = 100;
    cursor_y = EPD_HEIGHT-70;
    epd_write_string(&FONT_UBUNTU_40, "fasani.de | Barcelona", &cursor_x, &cursor_y, fb, &font_props);

    epd_hl_update_screen(&hl, MODE_GL16, temperature);
    epd_poweroff();

    vTaskDelay(100);
    deep_sleep();
}

void wakeup_cause()
{
    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_EXT0: {
            printf("Wake up from ext0\n");
            break;
        }
        case ESP_SLEEP_WAKEUP_EXT1: {
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            if (wakeup_pin_mask != 0) {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                printf("Wake up from GPIO %d\n", pin);
                switch (pin)
                {
                case 36:
                    button1_wakeup = true;
                    break;
                case 39:
                    button2_wakeup = true;
                    break;
                }
            } else {
                printf("Wake up from GPIO\n");
            }
            break;
        }
        case ESP_SLEEP_WAKEUP_TIMER: {
            button1_wakeup = true;
            printf("Wake up from timer\n");
            break;
        }
        // Ignore rest of wakeup causes
        default:
            break;
    }
}

void app_main()
{
    printf("EPD width: %d height: %d\n\n", EPD_WIDTH, EPD_HEIGHT);
    gpio_set_direction(BUTTON1, GPIO_MODE_INPUT);
    gpio_set_direction(BUTTON2, GPIO_MODE_INPUT);
    
    ESP_LOGI("EXT1_WAKEUP", "When IO %d or %d is LOW", (uint8_t)BUTTON1, (uint8_t)BUTTON2);
    
    // Determine wakeup cause and from what button
    wakeup_cause();
    // Wake up with buttons on high
    esp_sleep_enable_ext1_wakeup(1ULL<<BUTTON1 | 1ULL<<BUTTON2, ESP_EXT1_WAKEUP_ANY_HIGH);

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
    if (button2_wakeup) {
        present_tab2();
        return;
    }
    epd_poweroff();
    
    // Initialize SCD40
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", SDA_GPIO);
    scd_read();
}

