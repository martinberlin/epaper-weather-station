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
#include "protocol_examples_common.h"

// WiFi related
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
// HTTP Client + time
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
// JPG decoder
#if ESP_IDF_VERSION_MAJOR >= 4 // IDF 4+
  #include "esp32/rom/tjpgd.h"
#else // ESP32 Before IDF 4.0
  #include "rom/tjpgd.h"
#endif

#include "esp_task_wdt.h"
#include <stdio.h>
#include <string.h>
#include <math.h> // round + pow

// WiFI connection
#define CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER 3

// - - - - Display configuration - - - - - - - - -
// 0 - Landscape - 1 Portrait
#define CONFIG_DISPLAY_ROTATION 0
// Your SPI epaper class
// Find yours here: https://github.com/martinberlin/cale-idf/wiki
//#include <color/dke075z83.h>
#include <gdew075T7.h>
EpdSpi io;
Gdew075T7 display(io);

// Fonts
#include <Ubuntu_M24pt8b.h>
#include <Ubuntu_M48pt8b.h>
#include <Ubuntu_L80pt8b.h>

// ADC Battery voltage reading. Disable with false if not using Cinwrite board
#define USE_CINREAD_PCB true
#define SYNC_SUMMERTIME true
#define RTC_POWER_PIN GPIO_NUM_4
#define CINREAD_BATTERY_INDICATOR false
float ds3231_temp_correction = -0.6;
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

// Note this number can be changed: Is when we consider White starts
// 0 -> Black 125 -> Gray (middle) 255 -> White
#define JPG_WHITE_THRESHOLD 180
#define JPG_COLOR_RED_THRESHOLD 100
// Image URL and jpg settings. Make sure to update EPD_WIDTH/HEIGHT if using loremflickr
// Note: Only HTTP protocol supported (Check README to use SSL secure URLs) loremflickr
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define EPD_WIDTH  800
#define EPD_HEIGHT 480
//#define IMG_URL ("https://cataas.com/cat?height=" STR(EPD_HEIGHT))
#define IMG_URL "http://img.cale.es/jpg/fasani/5f4ebbfa2cf26"
// CALE url test (should match width/height of your EPD)
//#define IMG_URL "http://img.cale.es/jpg/fasani/5ea1dec401890" // 800*480 test

// Please check the README to understand how to use an SSL Certificate
// Note: This makes a sntp time sync query for cert validation  (It's slower)
#define VALIDATE_SSL_CERTIFICATE false

// Jpeg: Adds dithering to image rendering (Makes grayscale smoother on transitions)
#define JPG_DITHERING false

// Affects the gamma to calculate gray (lower is darker/higher contrast)
// Nice test values: 0.9 1.2 1.4 higher and is too bright
double gamma_value = 0.9;

// As default is 512 without setting buffer_size property in esp_http_client_config_t
#define HTTP_RECEIVE_BUFFER_SIZE 1938

// Load the EMBED_TXTFILES. Then doing (char*) server_cert_pem_start you get the SSL certificate
// Reference: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#embedding-binary-data
extern const uint8_t server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_server_cert_pem_end");

#define DEBUG_VERBOSE true

// NVS non volatile storage
nvs_handle_t storage_handle;
// I2C descriptor
i2c_dev_t dev;
// STAT pin of TPS2113
#define TPS_POWER_MODE GPIO_NUM_5
// DS3231 INT pin is pulled High and goes to this S3 GPIO:
#define GPIO_RTC_INT GPIO_NUM_6

/**
┌───────────────────────────┐
│ CLOCK configuration       │ Device wakes up each N minutes
└───────────────────────────┘ Takes about 3.5 seconds to run the program
**/
#define DEEP_SLEEP_SECONDS 120
/**
┌───────────────────────────┐
│ NIGHT MODE configuration  │ Make the module sleep in the night to save battery power
└───────────────────────────┘
**/
// Leave NIGHT_SLEEP_START in -1 to never sleep. Example START: 22 HRS: 8  will sleep from 10PM till 6 AM
#define NIGHT_SLEEP_START 23
#define NIGHT_SLEEP_HRS   6
// sleep_mode=1 uses precise RTC wake up. RTC alarm pulls GPIO_RTC_INT low when triggered
// sleep_mode=0 wakes up every 10 min till NIGHT_SLEEP_HRS. Useful to log some sensors while epaper does not update
uint8_t sleep_mode = 0;
bool rtc_wakeup = true;
// sleep_mode=1 requires precise wakeup time and will use NIGHT_SLEEP_HRS+20 min just as a second unprecise wakeup if RTC alarm fails
// Needs menuconfig --> DS3231 Configuration -> Set clock in order to store this alarm once
uint8_t wakeup_hr = 7;
uint8_t wakeup_min= 1;

uint64_t USEC = 1000000;

// Weekdays and months translatables (Select one only)
//#include <catala.h>
//#include <english.h>
//#include <spanish.h>
#include <deutsch.h>


uint8_t powered_by = 0;

// You have to set these CONFIG value using: idf.py menuconfig --> DS3231 Configuration
#if 0
#define CONFIG_SCL_GPIO		7
#define CONFIG_SDA_GPIO		15
#define	CONFIG_TIMEZONE		9
#define NTP_SERVER 		"pool.ntp.org"
#endif

// JPEG decoder
JDEC jd; 
JRESULT rc;
// Buffers
uint8_t *fb;            // EPD 2bpp buffer
uint8_t *source_buf;    // JPG download buffer
uint8_t *decoded_image; // RAW decoded image GRAY value

uint8_t *decoded_image_color_r; // RAW decoded image RED
uint8_t *decoded_image_color_g; // RAW decoded image GREEN
static uint8_t tjpgd_work[4096]; // tjpgd 4Kb buffer

uint32_t buffer_pos = 0;
uint32_t time_download = 0;
uint32_t time_decomp = 0;
uint32_t time_render = 0;
static const char * jd_errors[] = {
    "Succeeded",
    "Interrupted by output function",
    "Device error or wrong termination of input stream",
    "Insufficient memory pool for the image",
    "Insufficient stream input buffer",
    "Parameter error",
    "Data format error",
    "Right format but not supported",
    "Not supported JPEG standard"
};

uint16_t ep_width = 0;
uint16_t ep_height = 0;
uint8_t gamme_curve[256];

static const char *TAG = "WiFi JPG clock";
uint16_t countDataEventCalls = 0;
uint32_t countDataBytes = 0;
uint32_t img_buf_pos = 0;
uint32_t dataLenTotal = 0;
uint64_t startTime = 0;

#if VALIDATE_SSL_CERTIFICATE == true
  /* Time aware for ESP32: Important to check SSL certs validity */
  void time_sync_notification_cb(struct timeval *tv)
  {
      ESP_LOGI(TAG, "Notification of a time synchronization event");
  }

  static void initialize_sntp(void)
  {
      ESP_LOGI(TAG, "Initializing SNTP");
      sntp_setoperatingmode(SNTP_OPMODE_POLL);
      sntp_setservername(0, "pool.ntp.org");
      sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  #ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
      sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
  #endif
      sntp_init();
  }

  static void obtain_time(void)
  {
      initialize_sntp();

      // wait for time to be set
      time_t now = 0;
      struct tm timeinfo = { 0 };
      int retry = 0;
      const int retry_count = 10;
      while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
          ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", (int)retry, (int)retry_count);
          vTaskDelay(2000 / portTICK_PERIOD_MS);
      }
      time(&now);
      localtime_r(&now, &timeinfo);
  }
#endif
//====================================================================================
// This sketch contains support functions to render the Jpeg images
//
// Created by Bodmer 15th Jan 2017
// Refactored by @martinberlin for EPDiy as a Jpeg download and render example
//====================================================================================

// Return the minimum of two values a and b
#define minimum(a,b)     (((a) < (b)) ? (a) : (b))

uint8_t find_closest_palette_color(uint8_t oldpixel)
{
  return oldpixel & 0xF0;
}

//====================================================================================
//   Decode and paint onto the Epaper screen
//====================================================================================
void jpegRender(int xpos, int ypos, int width, int height) {
 #if JPG_DITHERING
 unsigned long pixel=0;
 for (uint16_t by=0; by<ep_height;by++)
  {
    for (uint16_t bx=0; bx<ep_width;bx++)
    {
        int oldpixel = decoded_image[pixel];
        int newpixel = find_closest_palette_color(oldpixel);
        int quant_error = oldpixel - newpixel;
        decoded_image[pixel]=newpixel;
        if (bx<(ep_width-1))
          decoded_image[pixel+1] = minimum(255,decoded_image[pixel+1] + quant_error * 7 / 16);

        if (by<(ep_height-1))
        {
          if (bx>0)
            decoded_image[pixel+ep_width-1] =  minimum(255,decoded_image[pixel+ep_width-1] + quant_error * 3 / 16);

          decoded_image[pixel+ep_width] =  minimum(255,decoded_image[pixel+ep_width] + quant_error * 5 / 16);
          if (bx<(ep_width-1))
            decoded_image[pixel+ep_width+1] = minimum(255,decoded_image[pixel+ep_width+1] + quant_error * 1 / 16);
        }
        pixel++;
    }
  }
  #endif

  // Write to display
  uint64_t drawTime = esp_timer_get_time();
  uint32_t padding_x = 0;
  uint32_t padding_y = 0;
  if (display.getRotation() == 0 || display.getRotation() == 2) {
    padding_x = (ep_width - width) / 2;
    padding_y = (ep_height - height) / 2;
  } else {
    padding_x = (ep_height - width) / 2;
    padding_y = (ep_width - height) / 2;
  }

uint16_t color = EPD_WHITE;

int red_count = 0;

for (uint32_t by=0; by<height;by++) {
    for (uint32_t bx=0; bx<width;bx++) {

        if (decoded_image_color_r[by * width + bx]>JPG_COLOR_RED_THRESHOLD &&
            decoded_image_color_g[by * width + bx]<200) {
          color = EPD_RED;
          red_count++;
        } 
        else if (decoded_image[by * width + bx]>JPG_WHITE_THRESHOLD){
          color = EPD_WHITE;
        } else {
          color = EPD_BLACK;
        }
        display.drawPixel(bx + padding_x, by + padding_y, color);
    }
  }
  printf("RED total pix: %d\n", red_count);
  // calculate how long it took to draw the image
  time_render = (esp_timer_get_time() - drawTime)/1000;
  ESP_LOGI("render", "%d ms - jpeg draw", (int)time_render);
}

void deepsleep(){
    vTaskDelay(8000);
    printf("Going to deepsleep %d minutes\n", CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
    esp_deep_sleep(1000000LL * 60 * CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
}

static UINT feed_buffer(JDEC *jd,      
               uint8_t *buff, // Pointer to the read buffer (NULL:skip) 
               UINT nd 
) {
    uint32_t count = 0;

    while (count < nd) {
      if (buff != NULL) {
            *buff++ = source_buf[buffer_pos];
        }
        count ++;
        buffer_pos++;
    }

  return count;
}

/* User defined call-back function to output decoded RGB bitmap in decoded_image buffer */
static unsigned int tjd_output(JDEC *jd,     /* Decompressor object of current session */
           void *bitmap, /* Bitmap data to be output */
           JRECT *rect   /* Rectangular region to output */
) {
  //esp_task_wdt_reset();

  uint32_t w = rect->right - rect->left + 1;
  uint32_t h = rect->bottom - rect->top + 1;
  uint32_t image_width = jd->width;
  uint8_t *bitmap_ptr = (uint8_t*)bitmap;
  
  for (uint32_t i = 0; i < w * h; i++) {

    uint8_t r = *(bitmap_ptr++);
    uint8_t g = *(bitmap_ptr++);
    uint8_t b = *(bitmap_ptr++);

    // Calculate weighted grayscale
    //uint32_t val = ((r * 30 + g * 59 + b * 11) / 100); // original formula
    uint32_t val = (r*38 + g*75 + b*15) >> 7; // @vroland recommended formula

    int xx = rect->left + i % w;
    if (xx < 0 || xx >= image_width) {
      continue;
    }
    int yy = rect->top + i / w;
    if (yy < 0 || yy >= jd->height) {
      continue;
    }
    
    decoded_image[yy * image_width + xx] = gamme_curve[val];
    decoded_image_color_r[yy * image_width + xx] = r;
    decoded_image_color_g[yy * image_width + xx] = g;
  }

  return 1;
}

//====================================================================================
//   This function opens source_buf Jpeg image file and primes the decoder
//====================================================================================
int drawBufJpeg(uint8_t *source_buf, int xpos, int ypos) {
  rc = jd_prepare(&jd, feed_buffer, tjpgd_work, sizeof(tjpgd_work), &source_buf);
  if (rc != JDR_OK) {    
    ESP_LOGE(TAG, "JPG jd_prepare error: %s", jd_errors[rc]);
    return ESP_FAIL;
  }

  uint32_t decode_start = esp_timer_get_time();

  // Last parameter scales        v 1 will reduce the image
  rc = jd_decomp(&jd, tjd_output, 0);
  if (rc != JDR_OK) {
    ESP_LOGE(TAG, "JPG jd_decomp error: %s", jd_errors[rc]);
    return ESP_FAIL;
  }

  time_decomp = (esp_timer_get_time() - decode_start)/1000;

  ESP_LOGI("JPG", "width: %d height: %d\n", (int)jd.width, (int)jd.height);
  ESP_LOGI("decode", "%d ms . image decompression", (int)time_decomp);

  // Render the image onto the screen at given coordinates
  jpegRender(xpos, ypos, jd.width, jd.height);

  return 1;
}
// Round clock draw functions
uint16_t clock_x_shift = 238;
uint16_t clock_y_shift = 20;
uint16_t clock_radius = 150;
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
    int min_radius = clock_radius-20;
    float O;
    int x = maxx/2+clock_x_shift;
    int y = maxy/2+clock_y_shift;
    O=(min*(M_PI/30)-(M_PI/2)); 
    x = x+min_radius*cos(O);
    y = y+min_radius*sin(O);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+clock_y_shift,x,y, EPD_BLACK);
    display.drawLine(maxx/2+clock_x_shift,maxy/2-4+clock_y_shift,x,y, EPD_BLACK);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+4+clock_y_shift,x,y, EPD_BLACK);
    display.drawLine(maxx/2+clock_x_shift,maxy/2+3+clock_y_shift,x,y-1, EPD_BLACK);
}

void hrHand(uint8_t hr, uint8_t min)
{
    uint16_t hand_radius = 60;
    float O;
    int x = maxx/2+clock_x_shift;
    int y = maxy/2+clock_y_shift;
    
    if(hr<=12)O=(hr*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    if(hr>12) O=((hr-12)*(M_PI/6)-(M_PI/2))+((min/12)*(M_PI/30));
    x = x+hand_radius*cos(O);
    y = y+hand_radius*sin(O);
    
    display.drawLine(maxx/2-1+clock_x_shift,maxy/2-3+clock_y_shift-1, x-1, y-1, EPD_BLACK);
    display.drawLine(maxx/2-3+clock_x_shift,maxy/2+3+clock_y_shift-1, x+1, y-1, EPD_BLACK);
    display.drawLine(maxx/2-1+clock_x_shift,maxy/2+clock_y_shift-1, x-1, y-1, EPD_BLACK);
    display.drawLine(maxx/2-1+clock_x_shift,maxy/2+clock_y_shift-1, x+1, y-1, EPD_BLACK);
    display.drawLine(maxx/2-2+clock_x_shift,maxy/2+clock_y_shift-1, x-1, y-1, EPD_BLACK);
    display.drawLine(maxx/2-2+clock_x_shift,maxy/2+clock_y_shift-1, x+1, y-1, EPD_BLACK);

    display.drawLine(maxx/2+2+clock_x_shift+1,maxy/2+3+clock_y_shift+1, x+1, y+1, EPD_BLACK);
    display.drawLine(maxx/2-2+clock_x_shift-2,maxy/2+3+clock_y_shift, x-1, y+1, EPD_BLACK);
    display.drawLine(maxx/2+2+clock_x_shift+1,maxy/2+3+clock_y_shift+2, x+1, y+2, EPD_BLACK);
    display.drawLine(maxx/2-2+clock_x_shift-2,maxy/2+3+clock_y_shift, x-1, y+2, EPD_BLACK);
    display.drawLine(maxx/2+2+clock_x_shift+1,maxy/2+3+clock_y_shift+3, x+1, y+3, EPD_BLACK);
    display.drawLine(maxx/2-2+clock_x_shift-2,maxy/2+3+clock_y_shift, x-1, y+3, EPD_BLACK);
}

void clockLayout(uint8_t hr, uint8_t min, uint8_t sec)
{
    //printf("%02d:%02d:%02d\n", hr, min, sec);    
    // for(uint8_t i=1;i<9;i++) {
        /* printing a round ring with outer radius of 5 pixel */
    //    display.drawCircle(maxx/2+clock_x_shift, maxy/2+clock_y_shift, clock_radius-i, 0);
    //}
    // Circle in the middleRound clock dra
    display.drawCircle(maxx/2+clock_x_shift, maxy/2+clock_y_shift, 6, EPD_DARKGREY);

    uint16_t x=maxx/2+clock_x_shift;
    uint16_t y=maxy/2+clock_y_shift;

    for(float j=M_PI/6;j<=(2*M_PI);j+=(M_PI/6)) {    /* marking the hours for every 30 degrees */
        x=(maxx/2)+clock_x_shift+clock_radius*cos(j);
        y=(maxy/2)+clock_y_shift+clock_radius*sin(j);        
        display.fillCircle(x,y,6,0);
    }

    // Draw hour hands
    hrHand(hr, min);
    minHand(min);
    //secHand(sec);
}

void getClock() {
    // Get RTC date and time
    float temp;
    if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
        ESP_LOGE(TAG, "Could not get temperature.");
        return;
    }
    // Already got it in main() but otherwise could be done here
    /* if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(TAG, "Could not get time.");
        return;
    } */
    ESP_LOGI("CLOCK", "\n%s\n%02d:%02d", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_hour, rtcinfo.tm_min);

    // Starting coordinates:
    uint16_t y_start = 110;
    uint16_t x_cursor = 10;
    
    // Print day number and month
    display.setFont(&Ubuntu_M24pt8b);

    display.setTextColor(EPD_BLACK);
    display.setCursor(x_cursor+20, y_start+2);
    display.printerf("%s %d %s", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
    display.setTextColor(EPD_WHITE);
    display.setCursor(x_cursor+20, y_start);
    display.printerf("%s %d %s", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_mday, month_t[rtcinfo.tm_mon]);
    
    // HH:MM
    y_start += 212;
    display.setTextColor(EPD_RED);
    display.setFont(&Ubuntu_L80pt8b);
    display.setCursor(x_cursor, y_start);
    // Print clock HH:MM (Seconds excluded: rtcinfo.tm_sec)
    display.printerf("%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min);
    display.setFont(&Ubuntu_M24pt8b);
    
    // Print temperature
    y_start += 90;
    x_cursor+= 26;
    display.setFont(&Ubuntu_M48pt8b);
    display.setTextColor(EPD_WHITE);
    display.setCursor(x_cursor, y_start);
    display.printerf("%.1f °C", temp+ds3231_temp_correction);
    display.setTextColor(EPD_BLACK);
    display.setCursor(x_cursor-1, y_start-1);
    display.printerf("%.1f °C", temp+ds3231_temp_correction);
    
    #if CINREAD_BATTERY_INDICATOR
        display.setFont(&Ubuntu_M24pt8b);
        uint16_t raw_voltage = adc_battery_voltage(ADC_CHANNEL);
        uint16_t batt_volts = raw_voltage*raw2batt_multi;
        uint16_t percentage = round((batt_volts-3500) * 100 / 700);// 4200 is top charged -3500 remains latest 700mV 

        display.setCursor(display.width() - 160, 30);
        display.printerf("%dmV %d%%", batt_volts, percentage);
    #endif
    clockLayout(rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);

    ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp);
}

// Handles Htpp events and is in charge of buffering source_buf (jpg compressed image)
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        #if DEBUG_VERBOSE
          ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        #endif
        break;
    case HTTP_EVENT_ON_DATA:
        ++countDataEventCalls;
        #if DEBUG_VERBOSE
          if (countDataEventCalls%10==0) {
            ESP_LOGI(TAG, "%d len:%d\n", (int)countDataEventCalls, (int)evt->data_len);
          }
        #endif
        dataLenTotal += evt->data_len;

        if (countDataEventCalls == 1) startTime = esp_timer_get_time();
        // Append received data into source_buf
        memcpy(&source_buf[img_buf_pos], evt->data, evt->data_len);
        img_buf_pos += evt->data_len;

        // Optional hexa dump
        //ESP_LOG_BUFFER_HEX(TAG, source_buf, 100);
        break;

    case HTTP_EVENT_ON_FINISH:
        // Do not draw if it's a redirect (302)
        if (esp_http_client_get_status_code(evt->client) == 200) {
          printf("%d bytes read from %s\nIMG_BUF size: %d\n", (int)img_buf_pos, IMG_URL, (int)img_buf_pos);
          drawBufJpeg(source_buf, 0, 0);
          time_download = (esp_timer_get_time()-startTime)/1000;
          ESP_LOGI("www-dw", "%d ms - download", (int)time_download);

          #if CONFIG_GET_CLOCK
            getClock();
          #endif
          // Refresh display
          display.update();

          ESP_LOGI("total", "%d ms - total time spent\n", (int)(time_download+time_decomp+time_render));
        } else {
          printf("HTTP on finish got status code: %d\n", (int)esp_http_client_get_status_code(evt->client));
        }
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
        
        break;
    
    default:
    // Unhandled
    break;
    }
    return ESP_OK;
}

// Handles http request
static void http_post(void)
{    
    /**
     * NOTE: All the configuration parameters for http_client must be specified
     * either in URL or as host and path parameters.
     * FIX: Uncommenting cert_pem restarts even if providing the right certificate
     */
    esp_http_client_config_t config = {
        .url = IMG_URL,
        .disable_auto_redirect = false,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE,
        #if VALIDATE_SSL_CERTIFICATE == true
        .cert_pem = (char *)server_cert_pem_start
        #endif
        };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    #if DEBUG_VERBOSE
      if (esp_http_client_get_transport_type(client) == HTTP_TRANSPORT_OVER_SSL && config.cert_pem) {
        printf("SSL CERT:\n%s\n\n", (char *)server_cert_pem_start);
      }
    #endif
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %d\n",
                 IMG_URL,
                 (int)esp_http_client_get_status_code(client),
                 (int)esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "\nHTTP GET request failed: %s", esp_err_to_name(err));
    }

    
    esp_http_client_cleanup(client);
    deepsleep();
}

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < 5)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Connect to the AP failed %d times. Going to deepsleep %d minutes", 5, (int)CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
            deepsleep();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initializes WiFi the ESP-IDF way
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    // C++ wifi config
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    sprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), CONFIG_EXAMPLE_WIFI_SSID);
    sprintf(reinterpret_cast<char *>(wifi_config.sta.password), CONFIG_EXAMPLE_WIFI_PASSWORD);
    wifi_config.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to ap SSID:%s", CONFIG_EXAMPLE_WIFI_SSID);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", CONFIG_EXAMPLE_WIFI_PASSWORD);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

esp_err_t ds3231_initialization_status = ESP_OK;
#if CONFIG_SET_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif
#if CONFIG_GET_CLOCK
    #define NTP_SERVER " "
#endif

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


/**
 * @brief Turn back 1 hr in last sunday October or advance 1 hr in end of March for EU summertime
 * 
 * @param rtcinfo 
 * @param correction 
 */
void summertimeClock(tm rtcinfo, int correction) {
    struct tm time = {
        .tm_sec  = rtcinfo.tm_sec,
        .tm_min  = rtcinfo.tm_min,
        .tm_hour = rtcinfo.tm_hour + correction,
        .tm_mday = rtcinfo.tm_mday,
        .tm_mon  = rtcinfo.tm_mon,
        .tm_year = rtcinfo.tm_year,
        .tm_wday = rtcinfo.tm_wday
    };

    if (ds3231_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not set time for summertime correction (%d)", correction);
        return;
    }
    ESP_LOGI(pcTaskGetName(0), "Set summertime correction time done");
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

    display.setFont(&Ubuntu_M48pt8b);
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

void display_print_sleep_msg() {
    nvs_set_u8(storage_handle, "sleep_msg", 1);

    // Wait until board is fully powered
    delay_ms(80);
    display.init();
    
    display.setFont(&Ubuntu_M48pt8b);
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
// Flag to know if summertime is active
uint8_t summertime = 0;

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
        // :=[] Charging mode
    #if USE_CINREAD_PCB == true
        gpio_set_direction(TPS_POWER_MODE, GPIO_MODE_INPUT);
        powered_by = gpio_get_level(TPS_POWER_MODE);
        #else
        
        gpio_set_direction(RTC_POWER_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(RTC_POWER_PIN, 1);
    #endif
    
    gpio_set_direction(GPIO_RTC_INT, GPIO_MODE_INPUT);

    ep_width = display.width();
  ep_height = display.height();

  // MALLOC_CAP_SPIRAM as last param if you have external RAM.
  // Using external RAM for big buffers is a must or it will stop here:
  decoded_image = (uint8_t *)heap_caps_malloc(ep_width * ep_height, MALLOC_CAP_SPIRAM);

  decoded_image_color_r = (uint8_t *)heap_caps_malloc(ep_width * ep_height, MALLOC_CAP_SPIRAM);
  decoded_image_color_g = (uint8_t *)heap_caps_malloc(ep_width * ep_height, MALLOC_CAP_SPIRAM);
  if (decoded_image == NULL) {
      ESP_LOGE("main", "Initial alloc back_buf failed. Allocating %d * %d", ep_width, ep_height);
  }
  memset(decoded_image, 255, ep_width * ep_height);
  memset(decoded_image_color_r, 255, ep_width * ep_height);
  memset(decoded_image_color_g, 255, ep_width * ep_height);

  // Should be big enough to allocate the JPEG file size, width * height should suffice
  source_buf = (uint8_t *)heap_caps_malloc(ep_width * ep_height, MALLOC_CAP_SPIRAM);
  if (source_buf == NULL) {
      ESP_LOGE("main", "Initial alloc source_buf failed!");
  }

  display.init();
  display.setRotation(CONFIG_DISPLAY_ROTATION);
  display.setFont(&Ubuntu_M48pt8b);
  maxx = display.width();
  maxy = display.height();

  double gammaCorrection = 1.0 / gamma_value;
  for (int gray_value =0; gray_value<256;gray_value++)
    gamme_curve[gray_value]= round (255*pow(gray_value/255.0, gammaCorrection));

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // WiFi log level set only to Error otherwise outputs too much
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  wifi_init_sta();
  #if VALIDATE_SSL_CERTIFICATE == true
    obtain_time();
  #endif
  


    esp_err_t err = nvs_open("storage", NVS_READWRITE, &storage_handle);
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

    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);
    // Handle clock update for EU summertime
    #if SYNC_SUMMERTIME
    nvs_get_u8(storage_handle, "summertime", &summertime);
    // IMPORTANT: Do not forget to set summertime initially to 0 (leave it ready before march) or 1 (between March & October)
    //nvs_set_u8(storage_handle, "summertime", 0);
    //printf("mday:%d mon:%d, wday:%d hr:%d summertime:%d\n\n",rtcinfo.tm_mday,rtcinfo.tm_mon,rtcinfo.tm_wday,rtcinfo.tm_hour,summertime);
    // Just debug adding fake hour: if (rtcinfo.tm_mday > 6 && rtcinfo.tm_mon == 1 && rtcinfo.tm_wday == 2 && rtcinfo.tm_hour == 8 && summertime == 1) {
    
    // EU Summertime
    // Last sunday of March -> Forward 1 hour
    if (rtcinfo.tm_mday > 24 && rtcinfo.tm_mon == 2 && rtcinfo.tm_wday == 0 && rtcinfo.tm_hour == 8 && summertime == 0) {
        nvs_set_u8(storage_handle, "summertime", 1);
        summertimeClock(rtcinfo, 1);
        // Alternatively do this with internet sync (Only if there is fixed WiFi)
        //xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
    }
    // Last sunday of October -> Back 1 hour
    if (rtcinfo.tm_mday > 24 && rtcinfo.tm_mon == 9 && rtcinfo.tm_wday == 0 && rtcinfo.tm_hour == 8 && summertime == 1) {
        nvs_set_u8(storage_handle, "summertime", 0);
        summertimeClock(rtcinfo, -1);
        //xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
    }
    #endif

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
      http_post();

}
