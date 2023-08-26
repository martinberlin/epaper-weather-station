/** IMPORTANT: Uses a submodule
               git submodule add https://github.com/martinberlin/esp-idf-pcf8563.git components/rtc/pcf8563
**/
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
#include <math.h>
// Solve INT issue in IDF >= 5
#include <stdint.h>
#include <inttypes.h>
// RTC chip
#include "pcf8563.h"
#define CONFIG_SDA_GPIO 39
#define CONFIG_SCL_GPIO 40
i2c_dev_t dev;
// IMPORTANT: Needs an EPDiy board
// https://github.com/vroland/epdiy
#include "epdiy.h"

// Fonts. EPDiy fonts are prefixed by "e" in /components/big-fonts
#include "e_ubuntu_l_80.h"
#include "e_ubuntu_b_40.h"
// Weekdays and months translatables
char weekday_t[][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
char month_t[][12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

EpdFontProperties font_props;
#define FONT_CLOCK  ubuntu_l_80
#define FONT_TEXT   e_ubuntu_b_40
#define WAVEFORM EPD_BUILTIN_WAVEFORM
EpdiyHighlevelState hl;
uint8_t temperature = 25;
// EPD framebuffer
uint8_t* fb;
// Clock will refresh every:
#define DRAW_CLOCK_EVERY_SECONDS 5
uint64_t USEC = 1000000;
uint16_t maxx = 0;
uint16_t maxy = 0;
uint16_t clock_radius;
// You can also set these CONFIG value using menuconfig.
/* #if 1
#define CONFIG_SCL_GPIO		17
#define CONFIG_SDA_GPIO		18
#define	CONFIG_TIMEZONE		2
#define NTP_SERVER 		"pool.ntp.org"
#endif */

#if CONFIG_SET_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif
#if CONFIG_GET_CLOCK
    #define NTP_SERVER " "
#endif
#if CONFIG_DIFF_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif

static const char *TAG = "PCF8563";

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
    ESP_ERROR_CHECK( nvs_flash_init() );
    //tcpip_adapter_init();
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

    struct tm time = {
        .tm_sec  = timeinfo.tm_sec,
        .tm_min  = timeinfo.tm_min,
        .tm_hour = timeinfo.tm_hour,
        .tm_mday = timeinfo.tm_mday,
        .tm_mon  = timeinfo.tm_mon,  // 0-based
        .tm_year = timeinfo.tm_year + 1900,  
    };

    if (pcf8563_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not set time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI(pcTaskGetName(0), "Set initial date time done");

    // goto deep sleep
    const int deep_sleep_sec = 10;
    ESP_LOGI(pcTaskGetName(0), "Entering deep sleep for %d seconds", deep_sleep_sec);
    esp_deep_sleep(1000000LL * deep_sleep_sec);
}

void secHand(uint8_t sec)
{
    int sec_radius = clock_radius-20;
    float O;
    int x = maxx/2;
    int y = maxy/2;
    /* determining the angle of the line with respect to vertical */
    O=(sec*(M_PI/30)-(M_PI/2)); 
    x = x+sec_radius*cos(O);
    y = y+sec_radius*sin(O);
    epd_draw_line(maxx/2,maxy/2,x,y, 0, fb);
}

void minHand(uint8_t min)
{
    int min_radius = clock_radius-40;
    float O;
    int x = maxx/2;
    int y = maxy/2;
    O=(min*(M_PI/30)-(M_PI/2)); 
    x = x+min_radius*cos(O);
    y = y+min_radius*sin(O);
    epd_draw_line(maxx/2,maxy/2,x,y, 0, fb);
    epd_draw_line(maxx/2,maxy/2-4,x,y, 0, fb);
    epd_draw_line(maxx/2,maxy/2+4,x,y, 0, fb);
}

void hrHand(uint8_t hr, uint8_t min)
{
    uint16_t hand_radius = 100;
    float O;
    int x = maxx/2;
    int y = maxy/2;
    
    if(hr<=12)O=(hr*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    if(hr>12) O=((hr-12)*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    x = x+hand_radius*cos(O);
    y = y+hand_radius*sin(O);
    epd_draw_line(maxx/2,maxy/2, x, y, 0, fb);
    epd_draw_line(maxx/2-1,maxy/2-3, x-1, y-1, 0, fb);
    epd_draw_line(maxx/2+1,maxy/2+3, x+1, y+1, 0, fb);
}

void clockLayout(uint8_t hr, uint8_t min, uint8_t sec)
{
    printf("%02d:%02d:%02d\n", hr, min, sec);
    EpdRect area = {
        .x = (maxx/2) - clock_radius -25,
        .y = 0,
        .width = clock_radius*2 + 30,
        .height = epd_height()
    };
    // Clear up area 
    epd_fill_rect(area, 255, fb);
    epd_clear_area(area);

    font_props.fg_color = 0;
    for(uint8_t i=1;i<5;i++) {
    /* printing a round ring with outer radius of 5 pixel */
        epd_draw_circle(maxx/2, maxy/2, clock_radius-i, 0, fb);
    }
    // Circle in the middle
    epd_fill_circle(maxx/2, maxy/2, 6, 0, fb);

    uint16_t x=maxx/2;
    uint16_t y=maxy/2;
    int fontx, fonty;
    uint8_t hourname = 4;
    char hourbuffer[3];
    for(float j=M_PI/6;j<=(2*M_PI);j+=(M_PI/6)) {    /* marking the hours for every 30 degrees */
        if (hourname>12) {
            hourname = 1;
        }
        x=(maxx/2)+clock_radius*cos(j);
        y=(maxy/2)+clock_radius*sin(j);
        itoa(hourname, hourbuffer, 10);
        if (x < (epd_width()/2)) {
            fontx = x -20;
        } else {
            fontx = x +20;
        }
        if (y < (epd_height()/2)) {
            fonty = y -20;
        } else {
            fonty = y +20;
        }
        // Funny is missing the number 3. It's just a fun clock anyways ;)
        epd_write_string(&FONT_TEXT, hourbuffer, &fontx, &fonty, fb, &font_props);
        
        epd_fill_circle(x,y,4,0, fb);

        hourname++;
    }

    // Draw hour hands
    hrHand(hr, min);
    minHand(min);
    secHand(sec);

    epd_hl_update_area(&hl, MODE_DU, temperature, area);
}


void getClock()
{

    struct tm rtcinfo;
    if (pcf8563_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get time.");
        while (1) { vTaskDelay(1); }
    }
    uint16_t fontx = epd_width()/2-100;
    uint16_t fonty = epd_height()/2-30;
    uint16_t hello_x = epd_width()/2-100;
    uint16_t hello_y = 200;
    EpdRect clock_area = {
        .x = fontx - 200,
        .y = fonty - 120,
        .width = 850,
        .height = 200,
    };
    EpdRect date_area = {
        .x = clock_area.x,
        .y = 100,
        .width = 600,
        .height = 100,
    };
    font_props = epd_font_properties_default();
    font_props.fg_color = 2;
    epd_poweron();
    char datebuffer[28];
    char hellobuffer[28];
    sprintf(datebuffer, "%s %d %s", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
    printf(hellobuffer, "%s", "HELLO VALENTIN!");
    epd_write_string(&FONT_TEXT, datebuffer, &date_area.x, &date_area.y, fb, &font_props);
    //font_props.fg_color = 6;
    //epd_write_string(&FONT_TEXT, hellobuffer, &hello_x, &hello_y, fb, &font_props);
    epd_hl_update_screen(&hl, MODE_GL16, temperature);
    //epd_hl_update_area(&hl, MODE_DU, temperature, date_area);
    epd_poweroff();
    font_props.fg_color = 0;

    // Get RTC date and time
    while (1) {

        if (pcf8563_get_time(&dev, &rtcinfo) != ESP_OK) {
            ESP_LOGE(pcTaskGetName(0), "Could not get time.");
            while (1) { vTaskDelay(1); }
        }
        // Fix cursor for every write
        fontx = epd_width()/2-100;
        fonty = epd_height()/2-30;
        // Draw analog circular clock
        epd_poweron();
        epd_fill_rect(clock_area, 0xFF, fb);
        epd_hl_update_area(&hl, MODE_DU, temperature, clock_area);

        char hourbuffer[18];
        sprintf(hourbuffer, "%02d:%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);
        
        // Funny is missing the number 3. It's just a fun clock anyways ;)
        epd_write_string(&FONT_CLOCK, hourbuffer, &fontx, &fonty, fb, &font_props);

        //clockLayout(rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);
        epd_hl_update_area(&hl, MODE_DU, temperature, clock_area);
        //epd_hl_update_screen(&hl, MODE_GL16, temperature);
        
        /* ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d", 
            rtcinfo.tm_year, rtcinfo.tm_mon + 1,
            rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec); */
            vTaskDelay(2);
        epd_poweroff();
        vTaskDelay(pdMS_TO_TICKS(DRAW_CLOCK_EVERY_SECONDS*1000));
        
    }
}

void diffClock(void *pvParameters)
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
    strftime(strftime_buf, sizeof(strftime_buf), "%m-%d-%y %H:%M:%S", &timeinfo);
    ESP_LOGI(pcTaskGetName(0), "NTP date/time is: %s", strftime_buf);

    //vTaskDelay(pdMS_TO_TICKS(500));
    // Get RTC date and time
    struct tm rtcinfo;
    if (pcf8563_get_time(&dev, &rtcinfo) != ESP_OK) {
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
    //ESP_LOGI(pcTaskGetName(0), "RTC date/time is: %s", strftime_buf);

    // Get the time difference
    double x = difftime(rtcnow, now);
    ESP_LOGI(pcTaskGetName(0), "Time difference is: %f", x);
    
    while(1) {
        vTaskDelay(1000);
    }
}

void app_main()
{
    clock_radius = (maxy / 2) - 60;
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);
    printf("pcf8563 RTC test\n");
    
    epd_init(&epd_board_v7, &ED097OC4, EPD_LUT_64K);
    // Set VCOM for boards that allow to set this in software (in mV).
    // This will print an error if unsupported. In this case,
    // set VCOM using the hardware potentiometer and delete this line.
    epd_set_vcom(1760);
    maxx = epd_width();
    maxy = epd_height();
    printf("EPD width: %d height: %d\n\n", maxx, maxy);
// Initialize RTC
    if (pcf8563_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init RTC descriptor (I2C already initiated by epdiy)");
        //while (1) { vTaskDelay(1); }
    }

    hl = epd_hl_init(WAVEFORM);
    fb = epd_hl_get_framebuffer(&hl);
    epd_poweron();
    epd_clear();

#if CONFIG_SET_CLOCK
    xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
#endif

getClock();

#if CONFIG_DIFF_CLOCK
    // Diff clock
    xTaskCreate(diffClock, "diffClock", 1024*4, NULL, 2, NULL);
#endif
}

