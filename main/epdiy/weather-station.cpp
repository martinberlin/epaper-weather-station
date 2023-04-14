#include "ds3231.h"

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
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"
#include "esp_timer.h"
// IMPORTANT: Needs an EPDiy board
// https://github.com/vroland/epdiy
#include "epd_driver.h"
#include "epd_highlevel.h"
// Fonts. EPDiy fonts are prefixed by "e" in /components/big-fonts
#include "e_ubuntu_b_120.h"
#include "e_ubuntu_l_80.h"
//#include "e_firasans_24.h"

#define FONT_CLOCK  ubuntu_b_120
#define FONT_TEXT_1 ubuntu_l_80
//#define FONT_TEXT_2 FiraSans24

#define WAVEFORM EPD_BUILTIN_WAVEFORM
EpdiyHighlevelState hl;
uint8_t temperature = 25;
// EPD framebuffer
uint8_t* fb;
// Clock will refresh every:
#define DEEP_SLEEP_SECONDS 60
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

// Weekdays and months translatables
char weekday_t[][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

char month_t[][12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// Set TIMEZONE using idf.py menuconfig --> DS3231 Configuration
// PINs for an EPDiy V5 board: Need to be soldered, they are not there exposed.
#define SCL_GPIO		14  // Do not use IO 12, since ESP32 will fail to boot if pulled-high
#define SDA_GPIO		13  // B3 in Inkster, with removed pull-down R40 (Otherwise I2C won't work)

#if CONFIG_SET_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif
#if CONFIG_GET_CLOCK
    #define NTP_SERVER " "
#endif
#if CONFIG_DIFF_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif

static const char *TAG = "WeatherST";

// I2C descriptor
i2c_dev_t dev;


extern "C"
{
    void app_main();
}

void delay_ms(uint32_t period_ms) {
    sys_delay_ms(period_ms * 1000);
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    //sntp_setservername(0, "pool.ntp.org");
    ESP_LOGI(TAG, "Your NTP Server is %s", NTP_SERVER);
    sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

static bool obtain_time(void)
{
    ESP_ERROR_CHECK( esp_netif_init() );
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    initialize_sntp();

    // wait for time to be set
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    ESP_ERROR_CHECK( example_disconnect() );
    if (retry == retry_count) return false;
    return true;
}

void deep_sleep() {
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_SECONDS: %d seconds to wake-up", DEEP_SLEEP_SECONDS);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_SECONDS * USEC);
    esp_deep_sleep_start();
}

uint16_t generateRandom(uint16_t max) {
    if (max>0) {
        srand(esp_timer_get_time());
        return rand() % max;
    }
    return 0;
}

void setClock(void *pvParameters)
{
    // obtain time over NTP
    ESP_LOGI(pcTaskGetName(0), "Connecting to WiFi and getting time over NTP.");
    if(!obtain_time()) {
        ESP_LOGE(pcTaskGetName(0), "Fail to getting time over NTP.");
        while (1) { vTaskDelay(1); }
    }

    // update 'now' variable with current time
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];
    time(&now);
    now = now + (CONFIG_TIMEZONE*60*60);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(pcTaskGetName(0), "The current date/time is: %s", strftime_buf);

    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_sec=%d",timeinfo.tm_sec);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_min=%d",timeinfo.tm_min);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_hour=%d",timeinfo.tm_hour);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_wday=%d",timeinfo.tm_wday);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mday=%d",timeinfo.tm_mday);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_mon=%d",timeinfo.tm_mon);
    ESP_LOGD(pcTaskGetName(0), "timeinfo.tm_year=%d",timeinfo.tm_year);

    printf("Setting tm_wday: %d\n\n", timeinfo.tm_wday);

    struct tm time = {
        .tm_sec  = timeinfo.tm_sec,
        .tm_min  = timeinfo.tm_min,
        .tm_hour = timeinfo.tm_hour,
        .tm_mday = timeinfo.tm_mday,
        .tm_mon  = timeinfo.tm_mon,  // 0-based
        .tm_year = timeinfo.tm_year + 1900,
        .tm_wday = timeinfo.tm_wday
    };

    if (ds3231_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not set time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI(pcTaskGetName(0), "Set initial date time done");

    // goto deep sleep
    deep_sleep();
}

void getClock()
{
    EpdRect area;
    printf("getClock()\n\n");
    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_LEFT;

    // Get RTC date and time
    float temp;
    struct tm rtcinfo;
    vTaskDelay(200 / portTICK_PERIOD_MS);
    if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get temperature.");
        while (1) { vTaskDelay(1); }
    }
    if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get time.");
        while (1) { vTaskDelay(1); }
    }
    temperature = (uint8_t) temp;
    // Start Y line:
    int y_start = 136;

    uint16_t x_rand = generateRandom(random_x);
    printf("random1 %d\n", x_rand);
    // Print day. fg foreground text color:0 black 8 more light gray
    font_props.fg_color = 4;
    int cursor_x = 100 + x_rand;
    epd_write_string(&FONT_TEXT_1, weekday_t[rtcinfo.tm_wday], &cursor_x, &y_start, fb, &font_props);

    // HH:MM -> Clear first
    cursor_x = 80;
    y_start = 200;
    area = {
        .x = cursor_x, 
        .y = y_start,
        .width = EPD_WIDTH-100,
        .height = 200
    };
    if (use_partial_update) {
      epd_clear_area(area);
    }
    font_props.fg_color = 0;
    char clock_buffer[8];

    x_rand = generateRandom(random_x);
    printf("random2 %d\n", x_rand);
    cursor_x += x_rand;
    y_start = 380;
    snprintf(clock_buffer, sizeof(clock_buffer), "%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min);
    epd_write_string(&FONT_CLOCK, clock_buffer, &cursor_x, &y_start, fb, &font_props);

    x_rand = generateRandom(random_x);
    cursor_x = 110 + generateRandom(x_rand);
    y_start = 550;
    font_props.fg_color = 0;
    char date_buffer[18];
    // To add year: %d %s, %d
    font_props.fg_color = 4;
    snprintf(date_buffer, sizeof(date_buffer), "%d %s", rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
    epd_write_string(&FONT_TEXT_1, date_buffer, &cursor_x, &y_start, fb, &font_props);
    // Prints first the lines: 1. day of week 2. HH:MM 3. day number, month name 
    epd_hl_update_screen(&hl, MODE_GL16, temperature);
    
    // Print temperature
    font_props.fg_color = 6;
    cursor_x = 110;
    y_start = 750;
    area = {
        .x = cursor_x, 
        .y = y_start-170,
        .width = EPD_WIDTH-100,
        .height = 200
    };
    if (use_partial_update) {
      epd_clear_area(area);
    }
    x_rand = generateRandom(random_x);
    cursor_x += x_rand;
    char temp_buffer[22];
    snprintf(temp_buffer, sizeof(temp_buffer), "%.2f °C", temp);
    epd_write_string(&FONT_TEXT_1, temp_buffer, &cursor_x, &y_start, fb, &font_props);
   
    cursor_x = 110;
    if (use_partial_update) {
      epd_hl_update_area(&hl, MODE_GL16, temperature, area);
    } else {
      epd_hl_update_screen(&hl, MODE_GL16, temperature);
    }
    ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp);
    

    epd_poweroff();
    epd_deinit();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    deep_sleep();
}

void diffClock(void *pvParameters)
{
    // Obtain time over NTP
    ESP_LOGI(pcTaskGetName(0), "Connecting to WiFi and getting time over NTP.");
    if(!obtain_time()) {
        ESP_LOGE(pcTaskGetName(0), "Fail to getting time over NTP.");
        while (1) { vTaskDelay(1); }
    }

    // update 'now' variable with current time
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];
    time(&now);
    now = now + (CONFIG_TIMEZONE*60*60);
    localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%m-%d-%y %H:%M:%S", &timeinfo);
    ESP_LOGI(pcTaskGetName(0), "NTP date/time is: %s", strftime_buf);

    // Get RTC date and time
    struct tm rtcinfo;
    if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get time.");
        while (1) { vTaskDelay(1); }
    }
    rtcinfo.tm_year = rtcinfo.tm_year - 1900;
    rtcinfo.tm_isdst = -1;
    ESP_LOGD(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

    // update 'rtcnow' variable with current time
    time_t rtcnow = mktime(&rtcinfo);
    localtime_r(&rtcnow, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%m-%d-%y %H:%M:%S", &timeinfo);
    ESP_LOGI(pcTaskGetName(0), "RTC date/time is: %s", strftime_buf);

    // Get the time difference
    double x = difftime(rtcnow, now);
    ESP_LOGI(pcTaskGetName(0), "Time difference is: %f", x);
    
    while(1) {
        vTaskDelay(1000);
    }
}

// Flag to know that we've synced the hour with timeQuery request
int16_t nvs_boots = 0;

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
    epd_poweron();
    if (nvs_boots%reset_every_x == 0) {
        printf("EPD clear triggered on %d\n", reset_every_x); 
        epd_clear();
    }

    // Initialize RTC. IMPORTANT: Needs a DS3231 connected to the EPDiy board SDA & SDL pins
    // In this example: I2C pins are defined here in the CPP since it's a customized board
    //                  being only CONFIG_TIMEZONE defined using menuconfig
    if (ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) SDA_GPIO, (gpio_num_t) SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);

#if CONFIG_SET_CLOCK
    // Set clock & Get clock. Update this comparison to a number that is minor than what you see in Serial Output to update the clock
    if (nvs_boots < 61) {
        xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
    } else {
        getClock();
    }
#endif

#if CONFIG_GET_CLOCK
    // Get clock
    getClock();
#endif

#if CONFIG_DIFF_CLOCK
    // Diff clock
    xTaskCreate(diffClock, "diffClock", 1024*4, NULL, 2, NULL);
#endif
}

