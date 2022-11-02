// RESEARCH FOR SPI HAT Cinwrite, PCB and Schematics            https://github.com/martinberlin/H-cinread-it895
// Note: This requires an IT8951 board and our Cinwrite PCB. It can be also adapted to work without it using an ESP32 (Any of it's models)
// If you want to help us with the project please get one here: https://www.tindie.com/stores/fasani
#include "ds3231.h"
struct tm rtcinfo;
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

// Your SPI epaper class
// Find yours here: https://github.com/martinberlin/cale-idf/wiki
#include <gdew042t2Grays.h>
EpdSpi io;
Gdew042t2Grays display(io);

// SCD4x consumes significant battery when reading the CO2 sensor, so make it only every N wakeups
// Only number from 1 to N. Example: Using DEEP_SLEEP_SECONDS 120 a 10 will read SCD data each 20 minutes 
#define USE_SCD40_EVERY_X_BOOTS 10

// ADC Battery voltage reading. Disable with false if not using Cinwrite board
#define CINREAD_BATTERY_INDICATOR true

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    #include "adc_compat.h" // compatibility with IDF 5
    #define ADC_CHANNEL ADC_CHANNEL_3
    double raw2batt_multi = 3.6;
    
  #else
    // Other IDF versions (But must be >= 4.4)
    #include "adc_compat4.h" // compatibility with IDF 5
    #define ADC_CHANNEL ADC1_CHANNEL_3
    double raw2batt_multi = 4.2;
#endif
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 0)
  #error "ESP_IDF version not supported. Please use IDF 4.4 or IDF v5.0-beta1"
#endif

// Fonts
#include <Ubuntu_M12pt8b.h>
#include <Ubuntu_M24pt8b.h>
#include <Ubuntu_M48pt8b.h>
#include <DejaVuSans_Bold60pt7b.h>
// NVS non volatile storage
nvs_handle_t storage_handle;

// STAT pin of TPS2113
#define TPS_POWER_MODE GPIO_NUM_5
// DS3231 INT pin is pulled High and goes to this S3 GPIO:
#define GPIO_RTC_INT GPIO_NUM_6

/**
┌───────────────────────────┐
│ CLOCK configuration       │ Device wakes up each N minutes
└───────────────────────────┘ Takes about 3.5 seconds to run the program
**/
#define DEEP_SLEEP_SECONDS 56
/**
┌───────────────────────────┐
│ NIGHT MODE configuration  │ Make the module sleep in the night to save battery power
└───────────────────────────┘
**/
// Leave NIGHT_SLEEP_START in -1 to never sleep. Example START: 22 HRS: 8  will sleep from 10PM till 6 AM
#define NIGHT_SLEEP_START 24
#define NIGHT_SLEEP_HRS   9
// sleep_mode=1 uses precise RTC wake up. RTC alarm pulls GPIO_RTC_INT low when triggered
// sleep_mode=0 wakes up every 10 min till NIGHT_SLEEP_HRS. Useful to log some sensors while epaper does not update
uint8_t sleep_mode = 1;
bool rtc_wakeup = true;
// sleep_mode=1 requires precise wakeup time and will use NIGHT_SLEEP_HRS+20 min just as a second unprecise wakeup if RTC alarm fails
// Needs menuconfig --> DS3231 Configuration -> Set clock in order to store this alarm once
uint8_t wakeup_hr = 8;
uint8_t wakeup_min= 1;

uint64_t USEC = 1000000;

// Weekdays and months translatables
char weekday_t[][12] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
char month_t[][12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

char temperature_suffix[] = "C";
char co2_suffix[]         = "CO2";
char humidity_suffix[]    = "% H";


uint8_t powered_by = 0;

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

static const char *TAG = "WeatherST SPI";

// I2C descriptor
i2c_dev_t dev;


extern "C"
{
    void app_main();
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

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
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

    display.setFont(&Ubuntu_M24pt8b);
    display.println("Initial date time\nis saved on RTC\n");

    display.printerf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    if (sleep_mode) {
        // Set RTC alarm
        time.tm_hour = wakeup_hr;
        time.tm_min  = wakeup_min;
        display.println("RTC alarm set to this hour:");
        display.printerf("%02d:%02d", time.tm_hour, time.tm_min);
        ESP_LOGI((char*)"RTC ALARM", "%02d:%02d", time.tm_hour, time.tm_min);
        ds3231_clear_alarm_flags(&dev, DS3231_ALARM_2);
        // i2c_dev_t, ds3231_alarm_t alarms, struct tm *time1,ds3231_alarm1_rate_t option1, struct tm *time2, ds3231_alarm2_rate_t option2
        ds3231_set_alarm(&dev, DS3231_ALARM_2, &time, (ds3231_alarm1_rate_t)0,  &time, DS3231_ALARM2_MATCH_MINHOUR);
        ds3231_enable_alarm_ints(&dev, DS3231_ALARM_2);
    }
    // Wait some time to see if disconnecting all changes background color
    delay_ms(50); 
    // goto deep sleep
    esp_deep_sleep_start();
}

// Round clock draw functions
uint16_t clock_x_shift = 100;
uint16_t clock_y_shift = 60;
uint16_t clock_radius = 80;
uint16_t maxx = 0;
uint16_t maxy = 0;
void secHand(uint8_t sec)
{
    int sec_radius = clock_radius-20;
    float O;
    int x = maxx/2+clock_x_shift;
    int y = maxy/2+clock_y_shift;
    /* determining the angle of the line with respect to vertical */
    O=(sec*(M_PI/30)-(M_PI/2)); 
    x = x+sec_radius*cos(O);
    y = y+sec_radius*sin(O);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+clock_y_shift,x,y, EPD_DARKGREY);
}

void minHand(uint8_t min)
{
    int min_radius = clock_radius-10;
    float O;
    int x = maxx/2+clock_x_shift;
    int y = maxy/2+clock_y_shift;
    O=(min*(M_PI/30)-(M_PI/2)); 
    x = x+min_radius*cos(O);
    y = y+min_radius*sin(O);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+clock_y_shift,x,y, EPD_DARKGREY);
    display.drawLine(maxx/2+clock_x_shift,maxy/2-4+clock_y_shift,x,y, EPD_BLACK);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+4+clock_y_shift,x,y, EPD_DARKGREY);
}

void hrHand(uint8_t hr, uint8_t min)
{
    uint16_t hand_radius = clock_radius-30;
    float O;
    int x = maxx/2+clock_x_shift;
    int y = maxy/2+clock_y_shift;
    
    if(hr<=12)O=(hr*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    if(hr>12) O=((hr-12)*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    x = x+hand_radius*cos(O);
    y = y+hand_radius*sin(O);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+clock_y_shift, x, y, EPD_BLACK);
    display.drawLine(maxx/2-1+clock_x_shift,maxy/2-3+clock_y_shift, x-1, y-1, EPD_DARKGREY);
    display.drawLine(maxx/2+1+clock_x_shift,maxy/2+3+clock_y_shift, x+1, y+1, EPD_BLACK);
}

void clockLayout(uint8_t hr, uint8_t min, uint8_t sec)
{
    //printf("%02d:%02d:%02d\n", hr, min, sec);    
    for(uint8_t i=1;i<5;i++) {
        /* printing a round ring with outer radius of 5 pixel */
        display.drawCircle(maxx/2+clock_x_shift, maxy/2+clock_y_shift, clock_radius-i, 0);
    }
    // Circle in the middle
    display.drawCircle(maxx/2+clock_x_shift, maxy/2+clock_y_shift, 6, EPD_DARKGREY);

    uint16_t x=maxx/2+clock_x_shift;
    uint16_t y=maxy/2+clock_y_shift;

    for(float j=M_PI/6;j<=(2*M_PI);j+=(M_PI/6)) {    /* marking the hours for every 30 degrees */
        x=(maxx/2)+clock_x_shift+clock_radius*cos(j);
        y=(maxy/2)+clock_y_shift+clock_radius*sin(j);        
        display.drawCircle(x,y,4,0);
    }

    // Draw hour hands
    hrHand(hr, min);
    minHand(min);
    secHand(sec);
}


void getClock() {
    // Get RTC date and time
    float temp;
    if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
        ESP_LOGE(TAG, "Could not get temperature.");
        return;
    }
    if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(TAG, "Could not get time.");
        return;
    }
    ESP_LOGI("CLOCK", "\n%s\n%02d:%02d", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_hour, rtcinfo.tm_min);

    // Start Y line:
    uint16_t y_start = 60;
    // Print day
    char text_buffer[50];
    sprintf(text_buffer, "%s", weekday_t[rtcinfo.tm_wday]);
    uint16_t x_cursor = 10;
    
    display.setCursor(x_cursor, y_start-20);
    display.setTextColor(EPD_LIGHTGREY);   
    display.print(text_buffer);
    text_buffer[0] = 0;

    display.setTextColor(EPD_BLACK);
    y_start += 60;
    display.setFont(&Ubuntu_M48pt8b);
    display.setCursor(x_cursor, y_start);
    // Print clock HH:MM (Seconds excluded: rtcinfo.tm_sec)
    display.printerf("%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min);
    display.setFont(&Ubuntu_M12pt8b);
    display.setTextColor(EPD_LIGHTGREY);
    display.setCursor(246, 70);
    display.printerf(":%02d", rtcinfo.tm_sec);
    
    // Print temperature
    y_start += 50;
    display.setFont(&Ubuntu_M24pt8b);
    display.setTextColor(EPD_DARKGREY);
    display.setCursor(x_cursor, y_start);
    display.printerf("%.1f °C", temp);


    // Print date format as you want
    y_start += 40;
    display.setFont(&Ubuntu_M12pt8b);
    display.setTextColor(EPD_LIGHTGREY);
    display.setCursor(x_cursor, y_start);
    display.printerf("%d %s", rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
    
    #if CINREAD_BATTERY_INDICATOR
        display.setFont(&Ubuntu_M12pt8b);
        uint16_t raw_voltage = adc_battery_voltage(ADC_CHANNEL);
        uint16_t batt_volts = raw_voltage*raw2batt_multi;
        uint16_t percentage = round((batt_volts-3500) * 100 / 700);// 4200 is top charged -3500 remains latest 700mV 

        display.setCursor(display.width() - 160, 30);
        display.printerf("%dmV %d%%", batt_volts, percentage);
    #endif
    clockLayout(rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

    display.update();
    ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp);
    // Wait some millis before switching off IT8951 otherwise last lines might not be printed
    delay_ms(1400);
    
    deep_sleep(DEEP_SLEEP_SECONDS);
}

void display_print_sleep_msg() {
    nvs_set_u8(storage_handle, "sleep_msg", 1);

    // Wait until board is fully powered
    delay_ms(80);
    display.init();
    
    display.setFont(&Ubuntu_M24pt8b);
    unsigned int color = EPD_WHITE;

    display.fillRect(0, 0, display.width() , display.height(), color);
    uint16_t y_start = display.height()/2-240;
    display.setCursor(100, y_start);
    display.print("NIGHT SLEEP");
    display.setCursor(100, y_start+94);
    display.printerf("%d:00 + %d Hrs.", NIGHT_SLEEP_START, NIGHT_SLEEP_HRS);

    float temp;
    if (ds3231_get_temp_float(&dev, &temp) == ESP_OK) {
        y_start+= 384;
        display.setTextColor(EPD_DARKGREY);
        display.setCursor(100, y_start);
        display.printerf("%.2f C", temp);
    }
    delay_ms(180);
}

// NVS variable to know how many times the ESP32 wakes up
int16_t nvs_boots = 0;
uint8_t sleep_flag = 0;
uint8_t sleep_msg = 0;

// Calculates if it's night mode
bool calc_night_mode(struct tm rtcinfo) {
    struct tm time_ini, time_rtc;
    // Night sleep? (Save battery)
    nvs_get_u8(storage_handle, "sleep_flag", &sleep_flag);
    //printf("sleep_flag:%d\n", sleep_flag);

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
    gpio_set_direction(GPIO_RTC_INT, GPIO_MODE_INPUT);
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = nvs_open("storage", NVS_READWRITE, &storage_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    // Initialize RTC
    ds3231_initialization_status = ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO);
    if (ds3231_initialization_status != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }
    #if CONFIG_SET_CLOCK
    // Set clock & Get clock
        xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
        return;
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
        if (false) {
          printf("NIGHT mode:%d | sleep_mode:%d | sleep_msg: %d | RTC IO: %d\n",
          (uint8_t)night_mode, sleep_mode, sleep_msg, gpio_get_level(GPIO_RTC_INT));
        }
        if (night_mode) {
            if (sleep_msg == 0) {
                display_print_sleep_msg();
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
    // :=[] Charging mode
    gpio_set_direction(TPS_POWER_MODE, GPIO_MODE_INPUT);

    // Read stored
    nvs_get_i16(storage_handle, "boots", &nvs_boots);

    ESP_LOGI(TAG, "-> NVS Boot count: %d", nvs_boots);
    nvs_boots++;
    // Set new value
    nvs_set_i16(storage_handle, "boots", nvs_boots);
    // Reset sleep msg flag so it prints it again before going to sleep
    if (sleep_msg) {
        nvs_set_u8(storage_handle, "sleep_msg", 0);
    }

    display.init(false);
    display.setRotation(2);
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);

    powered_by = gpio_get_level(TPS_POWER_MODE);

    display.setFont(&Ubuntu_M24pt8b);
    maxx = display.width();
    maxy = display.height();
   #if CONFIG_GET_CLOCK
    getClock();
   #endif
}
