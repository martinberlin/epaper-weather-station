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
// RTC chip
#include "pcf8563.h"
i2c_dev_t dev;
// TOUCH
#include "FT6X36.h"

// INTGPIO is touch interrupt, goes low when it detects a touch, which coordinates are read by I2C
FT6X36 ts(CONFIG_TOUCH_INT);

// Find yours here: https://github.com/martinberlin/cale-idf/wiki
#include <goodisplay/gdey0154d67.h>
EpdSpi io;
Gdey0154d67 display(io);

// Fonts. EPDiy fonts are prefixed by "e" in /components/big-fonts
#include "Ubuntu_M8pt8b.h"
#include "Ubuntu_M12pt8b.h"

char weekday_t[][12] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
char month_t[][12] = { "Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sep", "Oct", "Nov", "Dec"};

#define FONT_CLOCK  Ubuntu_M8pt8b
#define FONT_TEXT   Ubuntu_M12pt8b 

// Clock will refresh every:
#define DRAW_CLOCK_EVERY_SECONDS 30
uint64_t USEC = 1000000;
uint16_t maxx = 0;
uint16_t maxy = 0;
const uint16_t clock_radius=80;

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
extern "C"
{
    void app_main();
}

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

    // Initialize RTC
    i2c_dev_t dev;
    if (pcf8563_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

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
        .tm_wday = timeinfo.tm_wday
    };

    if (pcf8563_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not set time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI(pcTaskGetName(0), "Set initial date time done");

    display.setCursor(1,20);
    // HH:MM
    display.printerf("%02d:%02d", time.tm_hour, time.tm_min);
    display.setCursor(1,50);

    display.printerf("weekday: %d | %d-%d-%d", time.tm_wday, time.tm_year, time.tm_mon, time.tm_mday);
    display.update();
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
    display.drawLine(maxx/2,maxy/2,x,y, EPD_BLACK);
}

void minHand(uint8_t min)
{
    int min_radius = clock_radius-10;
    //printf("min_rad:%d\n",min_radius);
    float O;
    int x = maxx/2;
    int y = maxy/2;
    O=(min*(M_PI/30)-(M_PI/2)); 
    x = x+min_radius*cos(O);
    y = y+min_radius*sin(O);
    display.drawLine(maxx/2,maxy/2,x,y, EPD_BLACK);
    display.drawLine(maxx/2,maxy/2-4,x,y, EPD_BLACK);
    display.drawLine(maxx/2,maxy/2+4,x,y, EPD_BLACK);
}

void hrHand(uint8_t hr, uint8_t min)
{
    uint16_t hand_radius = clock_radius/2;
    float O;
    int x = maxx/2;
    int y = maxy/2;
    
    if(hr<=12)O=(hr*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    if(hr>12) O=((hr-12)*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    x = x+hand_radius*cos(O);
    y = y+hand_radius*sin(O);
    display.drawLine(maxx/2,maxy/2, x, y, EPD_BLACK);
    display.drawLine(maxx/2-1,maxy/2-3, x-1, y-1, EPD_BLACK);
    display.drawLine(maxx/2+1,maxy/2+3, x+1, y+1, EPD_BLACK);
}

void clockLayout(uint8_t hr, uint8_t min, uint8_t sec)
{
    printf("%02d:%02d:%02d max width:%d height:%d\n", hr, min, sec, maxx, maxy);
    // Clear up area 
    //epd_fill_rect(area, 255, fb);
    //epd_clear_area(area);

    for(uint8_t i=1;i<5;i++) {
    /* printing a round ring with outer radius of 5 pixel */
        display.drawCircle(maxx/2, maxy/2, clock_radius-i, EPD_BLACK);
    }
    // Circle in the middle
    display.fillCircle(maxx/2, maxy/2, 6, EPD_BLACK);

    display.setFont(&FONT_CLOCK);
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
        if (x < (maxx/2)) {
            fontx = x -20;
        } else {
            fontx = x +20;
        }
        if (y < (maxy/2)) {
            fonty = y -20;
        } else {
            fonty = y +20;
        }
        // Funny is missing the number 3. It's just a fun clock anyways ;)
        display.setCursor(fontx, fonty);
        //display.print(hourbuffer);
        display.fillCircle(x,y,4,EPD_BLACK);

        hourname++;
    }

    // Draw hour hands
    hrHand(hr, min);
    minHand(min);
    //secHand(sec);

    /**
     * @brief partial
     *  .x = (maxx/2) - clock_radius -25,
        .y = 0,
        .width = clock_radius*2 + 30,
        .height = display.height()
     * 
     */
}


void getClock(void *pvParameters)
{
    maxx = display.width();
    maxy = display.height();
    
    // Get RTC date and time
    while (1) {
        // Clean screen
        display.fillScreen(EPD_WHITE);

        struct tm rtcinfo;

        if (pcf8563_get_time(&dev, &rtcinfo) != ESP_OK) {
            ESP_LOGE(pcTaskGetName(0), "Could not get time.");
            while (1) { vTaskDelay(1); }
        }

        // Draw analog circular clock
        clockLayout(rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

        display.setFont(&FONT_TEXT);
        display.setCursor(1,20);
        // HH:MM
        display.printerf("%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min);
        display.setCursor(1,44);
        // Day: Mon, Tue, etc:
        display.printerf("%s", weekday_t[rtcinfo.tm_wday]);
        display.setCursor(maxx-80,20);
        // daynumber, Month name
        display.printerf("%d %s", rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
        display.update();

        ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d wday:%d", 
            rtcinfo.tm_year, rtcinfo.tm_mon + 1,
            rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday);
	
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

    // Initialize RTC
    i2c_dev_t dev;
    if (pcf8563_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

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

void touchEvent(TPoint p, TEvent e)
{
  #if defined(DEBUG_COUNT_TOUCH) && DEBUG_COUNT_TOUCH==1
    ++t_counter;
    ets_printf("e %x %d  ",e,t_counter); // Working
  #endif

  if (e != TEvent::Tap && e != TEvent::DragStart && e != TEvent::DragMove && e != TEvent::DragEnd)
    return;

  std::string eventName = "";
  switch (e)
  {
  case TEvent::Tap:
    eventName = "tap";
    break;
  case TEvent::DragStart:
    eventName = "DragStart";
    break;
  case TEvent::DragMove:
    eventName = "DragMove";
    break;
  case TEvent::DragEnd:
    eventName = "DragEnd";
    break;
  default:
    eventName = "UNKNOWN";
    break;
  }

  printf("X: %d Y: %d E: %s\n", p.x, p.y, eventName.c_str());
}

void touchLoop(void *pvParameters) {
    ts.loop();
}

void app_main()
{
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);
    printf("pcf8563 RTC test\n");  
    display.init();
    display.setMonoMode(true);
    display.setRotation(0);
    display.setFont(&FONT_TEXT);
    display.setTextColor(EPD_BLACK);



// Initialize RTC  
if (pcf8563_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t)CONFIG_SCL_GPIO) != ESP_OK) {
    ESP_LOGE(pcTaskGetName(0), "Could not init RTC I2C descriptor.");
    while (1) { vTaskDelay(1); }
}
vTaskDelay(pdMS_TO_TICKS(100));
// Instantiate touch. Important pass here the 3 required variables including display width and height
   ts.begin(FT6X36_DEFAULT_THRESHOLD, display.width(), display.height());
   ts.setRotation(display.getRotation());
   ts.registerTouchHandler(touchEvent);
   
#if CONFIG_SET_CLOCK
    xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_GET_CLOCK
    // Get clock
    xTaskCreate(getClock, "getClock", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_DIFF_CLOCK
    // Diff clock
    xTaskCreate(diffClock, "diffClock", 1024*4, NULL, 2, NULL);
#endif

   while(true) {
    ts.loop();
   }

   //xTaskCreate(touchLoop, "touchLoop", 2048, NULL, 2, NULL); // Does not like this
}

