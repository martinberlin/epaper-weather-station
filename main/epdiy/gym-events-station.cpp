#include "ds3231.h"
esp_err_t ds3231_initialization_status = ESP_OK;
struct tm rtcinfo;
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
#include "esp_wifi.h"
// IMPORTANT: Needs an EPDiy board
// https://github.com/vroland/epdiy
#include "epd_driver.h"
#include "epd_highlevel.h"

// Fonts. EPDiy fonts are prefixed by "e" in /components/big-fonts
#include "e_ubuntu_b_120.h"
#include "e_ubuntu_b_60.h"
#include "e_ubuntu_b_40.h"

EpdFontProperties font_props;

#define FONT_CLOCK   ubuntu_b_120
#define FONT_TEXT_60 e_ubuntu_b_60
#define FONT_TEXT_40 e_ubuntu_b_40


/**
┌───────────────────────────┐
│ NIGHT MODE configuration  │ Make the module sleep in the night to save battery power
└───────────────────────────┘ /home/martin/esp/projects/epaper-weather-station/components/big-fonts
**/

// Leave NIGHT_SLEEP_START in -1 to never sleep. Example START: 22 HRS: 8  will sleep from 10PM till 6 AM
#define NIGHT_SLEEP_START 21
#define NIGHT_SLEEP_HRS   11
// sleep_mode=1 uses precise RTC wake up. RTC alarm pulls GPIO_RTC_INT low when triggered
// sleep_mode=0 wakes up every 10 min till NIGHT_SLEEP_HRS. Useful to log some sensors while epaper does not update
uint8_t sleep_mode = 0;
bool rtc_wakeup = false;

// Flag to know that we've synced the hour with timeQuery request
int16_t nvs_boots = 0;
uint8_t sleep_flag = 0;
uint8_t sleep_msg = 0;
// Sunday: Sleep for an entire day!
#define DAY_WEEK_OFF 0

// NVS non volatile storage
nvs_handle_t storage_handle;
// Not implemented in EPDiy due to lack of pins
#define GPIO_RTC_INT GPIO_NUM_6

#define STATION_USE_SCD40 true
#define STATION_CO2_HIGH_ALERT 1000
// SCD4x consumes significant battery when reading the CO2 sensor, so make it only every N wakeups
// Only number from 1 to N. Example: Using DEEP_SLEEP_SECONDS 120 a 10 will read SCD data each 20 minutes 
#define USE_SCD40_EVERY_X_BOOTS 2

bool scd40_data_update = false;
/* Vectors belong to a C++ library called std so we need to import it first.
   They are use here only to save activities that are hooked with hr_start + hr_end */
#include <vector>
using namespace std;
#include "activities.h" // Structure for an activity + vector management

//#include "e_firasans_24.h"
#include "logo/logo-clb.h"
#if STATION_USE_SCD40
    #include "scd4x_i2c.h"
    #include "sensirion_common.h"
    #include "sensirion_i2c_hal.h"
    uint16_t scd4x_co2 = 0;
    int32_t scd4x_temperature = 0;
    int32_t scd4x_humidity = 0;
    uint8_t scd4x_read_error = 0;
    float scd4x_tem = 0;
    float scd4x_hum = 0;
#endif

#define WAVEFORM EPD_BUILTIN_WAVEFORM
EpdiyHighlevelState hl;
uint8_t temperature = 25;
// EPD framebuffer
uint8_t* fb;
// Clock will refresh every:
#define DEEP_SLEEP_SECONDS 110
uint64_t USEC = 1000000;

// Do a full EPD clear screen every x boots:
uint8_t reset_every_x = 1;
// Random x: Disabled on 0 and up to 255. Moves the X so the numbers are not marked in the epaper display creating permanent ghosts
uint8_t random_x = 255;

// Weekdays and months translatables
char weekday_t[][12] = { "Domingo", "Lunes", "Martes", "Miercoles", "Jueves", "Viernes", "Sabado" };
char month_t[][12] = { "ENERO", "FEBRERO", "MARZO", "ABRIL", "MAYO", "JUNIO", 
                        "JULIO", "AGOSTO", "SEPTIEMBRE", "OCTUBRE", "NOVIEMBRE", "DICIEMBRE"};

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

void draw_logo(uint16_t x, uint16_t y) {
      EpdRect logo_area = {
      .x = x,
      .y = y,
      .width = logoclb_width,
      .height = logoclb_height
  };
  epd_draw_rotated_image(logo_area, logoclb_data, fb);
}

#if STATION_USE_SCD40
void scd_render_co2(uint16_t co2, int x, int y, EpdFontProperties font_props) {
    //logo_co2(x, y, font_props);
    x += 140;
    y += 10;
    font_props.fg_color = 0;
    char textbuffer[6];
    snprintf(textbuffer, sizeof(textbuffer), "%d", co2);
    epd_write_string(&FONT_TEXT_40, textbuffer, &x, &y, fb, &font_props);
}

void scd_render_temp(double temp, int x, int y, EpdFontProperties font_props) {
    x += 140;
    char textbuffer[6];
    snprintf(textbuffer, sizeof(textbuffer), "%.1f", temp);
    epd_write_string(&FONT_TEXT_40, textbuffer, &x, &y, fb, &font_props);
    y -= 200;
    epd_write_string(&FONT_TEXT_40, "°C", &x, &y, fb, &font_props);
    
}

void scd_render_h(double hum, int x, int y, EpdFontProperties font_props) {
    //logo_co2(x, y, font_props);
    x += 140;
    char textbuffer[6];
    snprintf(textbuffer, sizeof(textbuffer), "%.1f", hum);
    epd_write_string(&FONT_TEXT_40, textbuffer, &x, &y, fb, &font_props);
    y -= 200;
    epd_write_string(&FONT_TEXT_40, "% h", &x, &y, fb, &font_props);
}

int16_t scd40_read() {
    int16_t error = 0;

    // Do not start I2C again if it's already there
    if (ds3231_initialization_status != ESP_OK) {
      sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);
    }

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
        
        //printf("ready_flag %d try:%d\n", (uint8_t)data_ready_flag, c);
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
            if (scd4x_co2 > 2600 && read_repeat == false) {
                read_repeat = true;
                continue;
            }
            scd4x_stop_periodic_measurement();
            nvs_set_u16(storage_handle, "scd4x_co2", scd4x_co2);
            nvs_set_i32(storage_handle, "scd4x_tem", scd4x_temperature);
            nvs_set_i32(storage_handle, "scd4x_hum", scd4x_humidity);
            ESP_LOGI(TAG, "CO2 : %u", scd4x_co2);
            ESP_LOGI(TAG, "Temp: %d mC", (int)scd4x_temperature);
            ESP_LOGI(TAG, "Humi: %d mRH", (int)scd4x_humidity);
            nvs_commit(storage_handle);
            scd40_data_update = true;
            
            read_nr++;
            if (read_nr == reads_till_snapshot) break;
        }
    }
    scd4x_power_down();
    // Release I2C (We leave it open for DS3231)
    // sensirion_i2c_hal_free();
    return error;
}
#endif

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
    epd_write_string(&FONT_TEXT_40, message, &x, &y, fb, &font_props);
    
    epd_hl_update_area(&hl, MODE_GC16, temperature, area);
    vTaskDelay(200);
    epd_poweroff();
}

// Time related sync
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

void deep_sleep(uint16_t seconds_to_sleep) {
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_SECONDS: %d seconds to wake-up", DEEP_SLEEP_SECONDS);
    esp_sleep_enable_timer_wakeup(seconds_to_sleep * USEC);
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
    deep_sleep(3600);
}

/** Find an activity between HH:MM start and HH:MM end
    da.day_week = 2;
    da.hr_start = 8;
    da.mm_start = 0;
    da.hr_end = 11;
    da.mm_end = 0;
*/
int vector_find(int day_week, int hr_now, int mm_now) {
  int found_idx = -1;
  for(uint16_t i = 0; i < date_vector.size(); i++) {
    if (date_vector[i].day_week != day_week ) continue;
    // Comes from RTC: Count how many minutes since 00:00
    uint16_t min_since_0 = (hr_now*60)+mm_now;
    bool check_act =
      (min_since_0    >= (date_vector[i].hr_start*60)+date_vector[i].mm_start) && 
      (min_since_0 -1 <= (date_vector[i].hr_end*60)+date_vector[i].mm_end);
      // Debug time ranges
    /*     
    printf("IDX: %d DAY %d==%d COMP: %d >= %d && %d <= %d  LIVE:%d\n%s\n", i, date_vector[i].day_week, day_week, min_since_0, (date_vector[i].hr_start*60)+date_vector[i].mm_start,
    min_since_0 -1 , (date_vector[i].hr_end*60)+date_vector[i].mm_end, (int)check_act, date_vector[i].note);
     */
    if (check_act) {
	  // Activity found: Return the index of the Vector found
      printf("Found activity ID %d\n%s\n", i, date_vector[i].note);
      return i;
    }
  }
  return found_idx;
}

void getClock()
{
    EpdRect area;
    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_LEFT;

    area = {
    .x = 1,
    .y = 1,
    .width = EPD_WIDTH,
    .height = EPD_HEIGHT
    };
    epd_clear_area(area);

    // Client LOGO
    draw_logo(10, 20);

    // Cursors:
    int cursor_x = 1100;
    int y_start = 52;
    
    if (scd40_data_update && scd4x_co2 > 1200) {
        epd_write_string(&FONT_TEXT_40, "Alto CO2!", &cursor_x, &y_start, fb, &font_props);
    }
    
    // Container variables
    char clock_buffer[8];
    char date_buffer[18];

    char act_title_buffer[18];
    char act_desc_buffer[28];
    
    // Get RTC date and time (Not precise enough for ambient temp.)
    /* float temp;
    if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get temperature.");
        while (1) { vTaskDelay(1); }
    } */
    /* if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get time.");
        while (1) { vTaskDelay(1); }
    } */
    // Activity at this time? (-1 = No activity)
    int act_id = vector_find(rtcinfo.tm_wday, rtcinfo.tm_hour, rtcinfo.tm_min);
    // Fill buffers
    snprintf(clock_buffer, sizeof(clock_buffer), "%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min);
    snprintf(date_buffer, sizeof(date_buffer), "%d %s", rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
    // Debug activity search
    //printf("ACTIVITY wday %d:%d\n\n", rtcinfo.tm_wday, act_id);



    // NO EVENT. Show different layout
    if (act_id == -1 || nvs_boots%2 == 0) {
    uint16_t x_rand = generateRandom(random_x);
    // Print day. fg foreground text color:0 black 8 more light gray
    /* 
    font_props.fg_color = 0;
    cursor_x = 450 + x_rand;
    epd_write_string(&FONT_TEXT_60, weekday_t[rtcinfo.tm_wday], &cursor_x, &y_start+100, fb, &font_props);
    */
    // HH:MM -> Clear first
    cursor_x = 220;
    y_start = 260;
    x_rand = generateRandom(random_x);
    //printf("random2 %d\n", x_rand);
    cursor_x += x_rand;
    y_start = 440;
    epd_write_string(&FONT_CLOCK, clock_buffer, &cursor_x, &y_start, fb, &font_props);

    x_rand = generateRandom(random_x);
    cursor_x = 110 + generateRandom(x_rand);
    y_start = 550;
    font_props.fg_color = 0;
    
    // To add year: %d %s, %d
    //font_props.fg_color = 4;
    epd_write_string(&FONT_TEXT_40, date_buffer, &cursor_x, &y_start, fb, &font_props);
    
    } else {
        // EVENT FOUND, change layout, make HH:MM smaller
        cursor_x = 430;
        y_start = 190;
        epd_write_string(&FONT_TEXT_60, clock_buffer, &cursor_x, &y_start, fb, &font_props);

        cursor_x = 770;
        y_start = 190;
        epd_write_string(&FONT_TEXT_40, weekday_t[rtcinfo.tm_wday], &cursor_x, &y_start, fb, &font_props);

        area = {
        .x = 430,
        .y = 200,
        .width = EPD_WIDTH/2+170,
        .height = 450
        };
        epd_fill_rect(area, 0, fb);

        font_props.fg_color = 15;
        snprintf(act_title_buffer, sizeof(act_title_buffer),"%d:%02d a %d:%02d", date_vector[act_id].hr_start, date_vector[act_id].mm_end,
                 date_vector[act_id].hr_end, date_vector[act_id].mm_end);
        snprintf(act_desc_buffer,sizeof(act_desc_buffer),date_vector[act_id].note);
        cursor_x = 430;
        y_start = 300;
        
        epd_write_string(&FONT_TEXT_60, act_title_buffer, &cursor_x, &y_start, fb, &font_props);
        cursor_x = 480;
        y_start = 430;
        font_props.fg_color = 12;
        epd_write_string(&FONT_TEXT_40, act_desc_buffer, &cursor_x, &y_start, fb, &font_props);
    }
    
    // Footer with data: Temp, Hum, CO2
    font_props.fg_color = 6;
    cursor_x = 15;
    const uint16_t y_footer = 790;

    char temp_buffer[32];
    y_start = y_footer;
    snprintf(temp_buffer, sizeof(temp_buffer), "%.1f°C", scd4x_tem);
    epd_write_string(&FONT_TEXT_60, temp_buffer, &cursor_x, &y_start, fb, &font_props);
    
    y_start = y_footer;
    cursor_x = 450;
    snprintf(temp_buffer, sizeof(temp_buffer), "%.f%%H", scd4x_hum); // N% H
    epd_write_string(&FONT_TEXT_40, temp_buffer, &cursor_x, &y_start, fb, &font_props);
    y_start = y_footer;
    cursor_x = EPD_WIDTH - 460;

    if (scd4x_co2 > STATION_CO2_HIGH_ALERT) {
        area = {
        .x = cursor_x-10,
        .y = y_footer -100,
        .width = 470,
        .height = 110
        };
        epd_draw_rect(area, 50, fb);
        area = {
        .x = area.x-1,
        .y = area.y-1,
        .width = area.width,
        .height = area.height
        };
        epd_draw_rect(area, 50, fb);
    }

    snprintf(temp_buffer, sizeof(temp_buffer), "%d", scd4x_co2);
    epd_write_string(&FONT_TEXT_60, temp_buffer, &cursor_x, &y_start, fb, &font_props);
    y_start = y_footer;
    epd_write_string(&FONT_TEXT_40, "CO2", &cursor_x, &y_start, fb, &font_props);

    cursor_x = 110;
    epd_hl_update_screen(&hl, MODE_GL16, temperature);
    
    /* ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp); */
    

    epd_poweroff();
    epd_deinit();

    vTaskDelay(100 / portTICK_PERIOD_MS);
    deep_sleep(DEEP_SLEEP_SECONDS);
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

// Calculates if it's night mode
bool calc_night_mode(struct tm rtcinfo) {
    struct tm time_ini, time_rtc;
    // Night sleep? (Save battery)
    nvs_get_u8(storage_handle, "sleep_flag", &sleep_flag);
    printf("calc_night_mode sleep_flag:%d\n", sleep_flag);

    if (rtcinfo.tm_hour >= NIGHT_SLEEP_START && sleep_flag == 0) {
        // Save actual time struct in NVS
        nvs_set_u8(storage_handle, "sleep_flag", 1);
        
        nvs_set_u16(storage_handle, "nm_year", rtcinfo.tm_year);
        nvs_set_u8(storage_handle, "nm_mon",  rtcinfo.tm_mon);
        nvs_set_u8(storage_handle, "nm_mday", rtcinfo.tm_mday);
        nvs_set_u8(storage_handle, "nm_hour", rtcinfo.tm_hour);
        nvs_set_u8(storage_handle, "nm_min", rtcinfo.tm_min);
        printf("night mode nm_* time saved %d:%d\n", rtcinfo.tm_hour, rtcinfo.tm_min);
        return true;
    }
    if (sleep_flag == 1) {
        uint8_t tm_mon,tm_mday,tm_hour,tm_min;
        uint16_t tm_year;
        nvs_get_u16(storage_handle, "nm_year", &tm_year);
        nvs_get_u8(storage_handle, "nm_mon", &tm_mon);
        nvs_get_u8(storage_handle, "nm_mday", &tm_mday);
        nvs_get_u8(storage_handle, "nm_hour", &tm_hour);
        nvs_get_u8(storage_handle, "nm_min", &tm_min);

        struct tm time_ini_sleep = {
            .tm_sec  = 0,
            .tm_min  = tm_min,
            .tm_hour = tm_hour,
            .tm_mday = tm_mday,
            .tm_mon  = tm_mon,  // 0-based
            .tm_year = tm_year - 1900,
        };
        // update 'rtcnow' variable with current time
        char strftime_buf[64];

        time_t startnm = mktime(&time_ini_sleep);
        // RTC stores year 2022 as 122
        rtcinfo.tm_year -= 1900;
        time_t rtcnow = mktime(&rtcinfo);

        localtime_r(&startnm, &time_ini);
        localtime_r(&rtcnow, &time_rtc);
        // Some debug to see what we compare
        if (false) {
            strftime(strftime_buf, sizeof(strftime_buf), "%F %r", &time_ini);
            ESP_LOGI(pcTaskGetName(0), "INI datetime is: %s", strftime_buf);
            strftime(strftime_buf, sizeof(strftime_buf), "%F %r", &time_rtc);
            ESP_LOGI(pcTaskGetName(0), "RTC datetime is: %s", strftime_buf);
        }
        // Get the time difference
        double timediff = difftime(rtcnow, startnm);
        uint16_t wake_seconds = NIGHT_SLEEP_HRS * 60 * 60;
        ESP_LOGI(pcTaskGetName(0), "Time difference is:%f Wait till:%d seconds", timediff, wake_seconds);
        if (timediff >= wake_seconds) {
            nvs_set_u8(storage_handle, "sleep_flag", 0);
        } else {
            return true;
        }
        // ONLY Debug and testing
        //nvs_set_u8(storage_handle, "sleep_flag", 0);
    }
  return false;
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
            } else {
                printf("Wake up from GPIO\n");
            }

            rtc_wakeup = true;
            // Woke up from RTC, clear alarm flag
            ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2);
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

    err = nvs_open("storage", NVS_READWRITE, &storage_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } 
    // Read stored
    nvs_get_i16(storage_handle, "boots", &nvs_boots);
    ESP_LOGI(TAG, "-> NVS Boot count: %d", nvs_boots);
    nvs_boots++;

    // Reset sleep msg flag so it prints it again before going to sleep
    if (sleep_msg) {
        nvs_set_u8(storage_handle, "sleep_msg", 0);
    }

    // Load activities that are fetched checking RTC time
    activity_load();
    // Set new value
    nvs_set_i16(storage_handle, "boots", nvs_boots);

    epd_init(EPD_OPTIONS_DEFAULT);
    hl = epd_hl_init(WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);
    epd_poweron();

    if (nvs_boots%reset_every_x == 0) {
        printf("EPD clear triggered on %d\n", reset_every_x); 
        //epd_clear();
    }

    // Initialize RTC. IMPORTANT: Needs a DS3231 connected to the EPDiy board SDA & SDL pins
    // In this example: I2C pins are defined here in the CPP since it's a customized board
    //                  being only CONFIG_TIMEZONE defined using menuconfig
    ds3231_initialization_status = ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO);
    if (ds3231_initialization_status != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);

    #if STATION_USE_SCD40
        if (nvs_boots % USE_SCD40_EVERY_X_BOOTS == 0 || rtc_wakeup) {
            // We read SDC40 only each N boots since it consumes quite a lot in 3.3V
            scd4x_read_error = scd40_read();
        }
        nvs_get_u16(storage_handle, "scd4x_co2", &scd4x_co2);
        nvs_get_i32(storage_handle, "scd4x_tem", &scd4x_temperature);
        nvs_get_i32(storage_handle, "scd4x_hum", &scd4x_humidity);
        scd4x_tem = (float)scd4x_temperature/1000;
        scd4x_hum = (float)scd4x_humidity/1000;

        ESP_LOGI(TAG, "Read from NVS Co2:%d temp:%d hum:%d\nTemp:%.1f Humidity:%.1f", 
                    (int)scd4x_co2, (int)scd4x_temperature, (int)scd4x_humidity, scd4x_tem, scd4x_hum);
    #endif

#if CONFIG_SET_CLOCK
    xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
#endif

// Maximum power saving (But slower WiFi which we use only to callibrate RTC)
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);

    // Determine wakeup cause and clear RTC alarm flag
    wakeup_cause(); // Needs I2C dev initialized

    if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get time.");
    }

    //sleep_flag = 0; // To preview night message
    // Validate NIGHT_SLEEP_START (On -1 is disabled)
    if (NIGHT_SLEEP_START >= 0 && NIGHT_SLEEP_START <= 23) {
        bool night_mode = calc_night_mode(rtcinfo);

        nvs_get_u8(storage_handle, "sleep_msg", &sleep_msg);
        if (true) {
          printf("NIGHT mode:%d | sleep_mode:%d | sleep_msg: %d | RTC IO: %d\n",
          (uint8_t)night_mode, sleep_mode, sleep_msg, gpio_get_level(GPIO_RTC_INT));
        }
        if (night_mode) {
            if (sleep_msg == 0) {
                epd_print_error((char*)"NIGHT SLEEP MODE");
            }
            switch (sleep_mode)
            {
            case 0:
                printf("Sleep Hrs from %d, %d hours\n", NIGHT_SLEEP_START, NIGHT_SLEEP_HRS);
                deep_sleep(60*10); // Sleep 10 minutes
                break;
            /* RTC Wakeup */
            case 1:
                ESP_LOGI("EXT1_WAKEUP", "When IO %d is LOW", (uint8_t)GPIO_RTC_INT);
                esp_sleep_enable_ext1_wakeup(1ULL<<GPIO_RTC_INT, ESP_EXT1_WAKEUP_ALL_LOW);
                // Sleep NIGHT_SLEEP_HRS + 20 minutes
                // deep_sleep(NIGHT_SLEEP_HRS*60*60+(60*20));
                esp_deep_sleep_start();
                break;
            default:
                printf("Sleep %d mode not implemented\n", sleep_mode);
                return;
            }
        }
    }

    if (rtcinfo.tm_wday == DAY_WEEK_OFF) {
    // DAY OFF Do not update display just sleep one hour
    deep_sleep(3600);
    }


#if CONFIG_GET_CLOCK
    // Get clock
    getClock();
#endif

#if CONFIG_DIFF_CLOCK
    // Diff clock
    xTaskCreate(diffClock, "diffClock", 1024*4, NULL, 2, NULL);
#endif
}

