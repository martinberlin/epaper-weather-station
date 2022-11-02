// RESEARCH FOR SPI HAT Cinwrite, PCB and Schematics            https://github.com/martinberlin/H-cinread-it895
// Note: This requires an IT8951 board and our Cinwrite PCB. It can be also adapted to work without it using an ESP32 (Any of it's models)
// If you want to help us with the project please get one here: https://www.tindie.com/stores/fasani
#include "ds3231.h"
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
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sntp.h"

#define STATION_USE_SCD40 true
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

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
// Big fonts
#include <Ubuntu_M24pt8b.h>
#include <Ubuntu_M48pt8b.h>
#include <DejaVuSans_Bold60pt7b.h>
// NVS non volatile storage
nvs_handle_t storage_handle;
// Enable on HIGH 5V boost converter
#define GPIO_ENABLE_5V GPIO_NUM_38
// STAT pin of TPS2113
#define TPS_POWER_MODE GPIO_NUM_5
// DS3231 INT pin is pulled High and goes to this S3 GPIO:
#define GPIO_RTC_INT GPIO_NUM_6

/**
┌───────────────────────────┐
│ CLOCK configuration       │ Device wakes up each N minutes
└───────────────────────────┘
**/
#define DEEP_SLEEP_SECONDS 120
/**
┌───────────────────────────┐
│ NIGHT MODE configuration  │ Make the module sleep in the night to save battery power
└───────────────────────────┘
**/
// Leave NIGHT_SLEEP_START in -1 to never sleep. Example START: 22 HRS: 8  will sleep from 10PM till 6 AM
#define NIGHT_SLEEP_START 21
#define NIGHT_SLEEP_HRS   9
// sleep_mode=1 uses precise RTC wake up. RTC alarm pulls GPIO_RTC_INT low when triggered
// sleep_mode=0 wakes up every 10 min till NIGHT_SLEEP_HRS. Useful to log some sensors while epaper does not update
uint8_t sleep_mode = 1;
bool rtc_wakeup = false;
// sleep_mode=1 requires precise wakeup time and will use NIGHT_SLEEP_HRS+20 min just as a second unprecise wakeup if RTC alarm fails
// Needs menuconfig --> DS3231 Configuration -> Set clock in order to store this alarm once
uint8_t wakeup_hr = 8;
uint8_t wakeup_min= 1;


// Avoids printing text always in same place so we don't leave marks in the epaper (Although parallel get well with that)
#define X_RANDOM_MODE true
uint64_t USEC = 1000000;
// Weekdays and months translatables (Select one only)
//#include <catala.h>
#include <english.h>
//#include <spanish.h>
//#include <chinese-mandarin.h> // Please use weather-station-unicode.cpp

uint8_t powered_by = 0;
uint8_t DARK_MODE = 1;
// You have to set these CONFIG value using: idf.py menuconfig --> DS3231 Configuration
#if 0
#define CONFIG_SCL_GPIO		7
#define CONFIG_SDA_GPIO		15
#define	CONFIG_TIMEZONE		9
#define NTP_SERVER 		"pool.ntp.org"
#endif
// Display configuration. Adjust to yours if you use a different one:
#define EPD_WIDTH  1200
#define EPD_HEIGHT 825

esp_err_t ds3231_initialization_status = ESP_OK;
#if CONFIG_SET_CLOCK
    #define NTP_SERVER CONFIG_NTP_SERVER
#endif
#if CONFIG_GET_CLOCK
    #define NTP_SERVER " "
#endif

static const char *TAG = "WeatherST";

// I2C descriptor
i2c_dev_t dev;


extern "C"
{
    void app_main();
}

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_IT8951   _panel_instance;
  lgfx::Bus_SPI       _bus_instance;

public:
// Provide method to access VCOM (https://github.com/lovyan03/LovyanGFX/issues/269)
  void setVCOM(uint16_t v) { _panel_instance.setVCOM(v); }
  uint16_t getVCOM(void) { return _panel_instance.getVCOM(); }
  
  LGFX(void)
  {
    {
      auto cfg = _bus_instance.config();    // バス設定用の構造体を取得します。

// SPIバスの設定
      cfg.spi_host = SPI2_HOST;     // 使用するSPIを選択  (VSPI_HOST or HSPI_HOST)
      cfg.spi_mode = 0;             // SPI通信モードを設定 (0 ~ 3)
      cfg.freq_write = 40000000;    // 送信時のSPIクロック (最大80MHz, 80MHzを整数で割った値に丸められます)
      cfg.freq_read  = 16000000;    // 受信時のSPIクロック
      cfg.spi_3wire  = false;        // 受信をMOSI IMPORTANT use it on false to read from MISO!
      cfg.use_lock   = true;        // トランザクションロックを使用する場合はtrueを設定
      cfg.dma_channel = 1;          // Set the DMA channel (1 or 2. 0=disable)   使用するDMAチャンネルを設定 (0=DMA不使用)
      cfg.pin_sclk = CONFIG_IT8951_SPI_CLK;            // SPIのSCLKピン番号を設定
      cfg.pin_mosi = CONFIG_IT8951_SPI_MOSI;           // SPIのMOSIピン番号を設定
      cfg.pin_miso = CONFIG_IT8951_SPI_MISO;           // SPIのMISOピン番号を設定 (-1 = disable)
      cfg.pin_dc   = -1;            // SPIのD/Cピン番号を設定  (-1 = disable)
 
      _bus_instance.config(cfg);    // 設定値をバスに反映します。
      _panel_instance.setBus(&_bus_instance);      // バスをパネルにセットします。
    }

    { // 表示パネル制御の設定を行います。
      auto cfg = _panel_instance.config();    // 表示パネル設定用の構造体を取得します。

      cfg.pin_cs           =    CONFIG_IT8951_SPI_CS;  // CSが接続されているピン番号   (-1 = disable)
      cfg.pin_rst          =    -1;
      cfg.pin_busy         =    CONFIG_IT8951_BUSY;    // BUSYが接続されているピン番号 (-1 = disable)

      // ※ 以下の設定値はパネル毎に一般的な初期値が設定されていますので、不明な項目はコメントアウトして試してみてください。

      cfg.memory_width     =   EPD_WIDTH;  // ドライバICがサポートしている最大の幅
      cfg.memory_height    =   EPD_HEIGHT;  // ドライバICがサポートしている最大の高さ
      cfg.panel_width      =   EPD_WIDTH;  // 実際に表示可能な幅
      cfg.panel_height     =   EPD_HEIGHT;  // 実際に表示可能な高さ
      cfg.offset_x         =     0;  // パネルのX方向オフセット量
      cfg.offset_y         =     0;  // パネルのY方向オフセット量
      cfg.offset_rotation  =     0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.dummy_read_pixel =     8;  // ピクセル読出し前のダミーリードのビット数
      cfg.dummy_read_bits  =     1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
      cfg.readable         =  true;  // データ読出しが可能な場合 trueに設定
      cfg.invert           =  true;  // パネルの明暗が反転してしまう場合 trueに設定
      cfg.rgb_order        = false;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
      cfg.dlen_16bit       = false;  // データ長を16bit単位で送信するパネルの場合 trueに設定
      cfg.bus_shared       =  true;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)

      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance); // 使用するパネルをセットします。
  }
};
LGFX display;

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
    uint8_t EP_CONTROL[] = {CONFIG_IT8951_SPI_CLK, CONFIG_IT8951_SPI_MOSI, CONFIG_IT8951_SPI_MISO, CONFIG_IT8951_SPI_CS, GPIO_ENABLE_5V};
    for (int io = 0; io < 5; io++) {
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

    display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    if (sleep_mode) {
        // Set RTC alarm
        time.tm_hour = wakeup_hr;
        time.tm_min  = wakeup_min;
        display.println("RTC alarm set to this hour:");
        display.printf("%02d:%02d", time.tm_hour, time.tm_min);
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

void getClock(void *pvParameters)
{
    // Get RTC date and time
    float temp;

    srand(esp_timer_get_time());
    // Random Dark mode?
    //DARK_MODE = rand() %2;

    if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get temperature.");
        while (1) { vTaskDelay(1); }
    }
    if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not get time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI("CLOCK", "\n%s\n%02d:%02d", weekday_t[rtcinfo.tm_wday], rtcinfo.tm_hour, rtcinfo.tm_min);
    
    // Once in a while the display stays white and becomes unresponsibe
    // If at this point there is no communication: Abort and reset
    if (display.isReadable() == false) {
      deep_sleep(30);
    }

    // Start Y line:
    uint16_t y_start = EPD_HEIGHT/2-340;

    // Turn on black background if Dark mode
    if (DARK_MODE) {
      display.fillScreen(display.color888(0,0,0));
    }
    // Print day
    char text_buffer[50];
    sprintf(text_buffer, "%s", weekday_t[rtcinfo.tm_wday]);
    display.setFont(&DejaVuSans_Bold60pt7b);
    int text_width = display.textWidth(text_buffer);
    //printf("text_buffer width:%d\n", text_width); // Correct

    uint16_t x_cursor = 100;
    if (X_RANDOM_MODE) {
        x_cursor += generateRandom(EPD_WIDTH-text_width)-100;
    }
    
    display.setCursor(x_cursor, y_start-20);
    display.setTextColor(display.color888(200,200,200));   
    display.print(text_buffer);
    text_buffer[0] = 0;
    display.setTextColor(display.color888(0,0,0));
    
    // Delete old clock
    y_start+=100;
    unsigned int color = display.color888(255,255,255);
    if (DARK_MODE) {
        color = display.color888(0,0,0);
    }
    x_cursor = 100;
    if (X_RANDOM_MODE) {
        x_cursor = 100 + generateRandom(350);
    }
    display.setCursor(x_cursor, y_start);
    display.fillRect(100, y_start+10, EPD_WIDTH-100 , 200, color);

    // Print clock HH:MM (Seconds excluded: rtcinfo.tm_sec)
    // Makes font x2 size (Loosing resolution) till set back to 1
    display.setTextSize(2);
    if (DARK_MODE) {
        display.setTextColor(display.color888(255,255,255));
    }
    display.printf("%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min);

    // Print date YYYY-MM-DD update format as you want
    display.setFont(&Ubuntu_M48pt8b);
    display.setTextSize(1);
    y_start += 200;
    x_cursor = 100;
    sprintf(text_buffer, "%d %s, %d", rtcinfo.tm_mday, month_t[rtcinfo.tm_mon], rtcinfo.tm_year);
    text_width = display.textWidth(text_buffer);
    if (X_RANDOM_MODE) {
        x_cursor += generateRandom(EPD_WIDTH-text_width)-100;
    }

    display.setCursor(x_cursor, y_start);
    display.setTextColor(display.color888(70,70,70));
    // N month, year
    display.print(text_buffer);

    // Print temperature
    y_start += 130;
    x_cursor = 100;
    if (X_RANDOM_MODE) {
        x_cursor = 100 + generateRandom(250);
    }
    display.fillRect(x_cursor, y_start+20, EPD_WIDTH/2 , 200, color);
    display.setTextColor(display.color888(170,170,170));
    display.setCursor(x_cursor, y_start);
    display.printf("%.2f C", temp);
    display.setFont(&Ubuntu_M24pt8b);
    display.setCursor(x_cursor, y_start+85);
    display.print("RTC");
    if (rtc_wakeup) {
        display.print(" WAKEUP");
    }

    #if STATION_USE_SCD40
    uint16_t left_margin = 400;
    if (scd4x_read_error == 0) {
        if (DARK_MODE) {
            display.setTextColor(display.color888(255,255,255));
        }
        display.setFont(&Ubuntu_M48pt8b);
        display.setCursor(EPD_WIDTH-left_margin,y_start);
        display.printf("%d CO2", scd4x_co2);


        y_start+=120;
        display.setFont(&Ubuntu_M24pt8b);
        ESP_LOGD(TAG, "Displaying SDC40 Temp:%.1f °C Hum:%.1f %% X:%d Y:%d", scd4x_tem, scd4x_hum, EPD_WIDTH-left_margin,y_start);
        display.setCursor(EPD_WIDTH-left_margin, y_start);
        display.printf("%.1f C", scd4x_tem);
        y_start+=70;
        display.setCursor(EPD_WIDTH-left_margin, y_start);
        display.printf("%.1f %% H", scd4x_hum);
    } else {
        display.setFont(&Ubuntu_M24pt8b);
        display.setCursor(100, EPD_HEIGHT - 110);
        display.print("Sensor SCD4x could not be read");
    }
    #endif

    // Print "Powered by" message
    if (gpio_get_level(TPS_POWER_MODE)==0) {
        display.setCursor(100, EPD_HEIGHT-65);
        display.print(":=   Powered by USB");
        display.fillRect(128, EPD_HEIGHT-49, 20, 32, display.color888(200,200,200));
    }
    
    #ifdef CINREAD_BATTERY_INDICATOR
        uint16_t raw_voltage = adc_battery_voltage(ADC_CHANNEL);
        uint16_t batt_volts = raw_voltage*raw2batt_multi;
        uint16_t percentage = round((batt_volts-3500) * 100 / 700);// 4200 is top charged -3500 remains latest 700mV 
        display.drawRect(EPD_WIDTH - 350, EPD_HEIGHT-51, 100, 30); // |___|
        display.fillRect(EPD_WIDTH - 350, EPD_HEIGHT-51, percentage, 30);
        display.drawRect(EPD_WIDTH - 250, EPD_HEIGHT-39, 6, 8);    //      =
        display.setCursor(EPD_WIDTH - 220, EPD_HEIGHT-65);
        display.printf("%d mV", batt_volts);
    #endif
    /*
    uint16_t vcom = display.getVCOM(); // getVCOM: Not used for now
    display.printf("vcom:%d", vcom);
    */
    
    ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp);
    // Wait some millis before switching off IT8951 otherwise last lines might not be printed
    delay_ms(200);
    // Not needed if we go to sleep and it has a load switch
    //display.powerSaveOn();
    
    deep_sleep(DEEP_SLEEP_SECONDS);
}

#if STATION_USE_SCD40
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
            ESP_LOGI(TAG, "CO2 : %u", scd4x_co2);
            ESP_LOGI(TAG, "Temp: %d mC", scd4x_temperature);
            ESP_LOGI(TAG, "Humi: %d mRH", scd4x_humidity);
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

void display_print_sleep_msg() {
    nvs_set_u8(storage_handle, "sleep_msg", 1);
    // Turn on the 3.7 to 5V step-up
    gpio_set_level(GPIO_ENABLE_5V, 1);
    // Wait until board is fully powered
    delay_ms(80);
    display.init();
    display.setEpdMode(epd_mode_t::epd_fast);
    display.setFont(&DejaVuSans_Bold60pt7b);
    unsigned int color = display.color888(255,255,255);
    if (DARK_MODE) {
        color = display.color888(0,0,0);
        display.setTextColor(display.color888(255,255,255));
    }
    display.fillRect(0, 0, EPD_WIDTH , EPD_HEIGHT, color);
    uint16_t y_start = EPD_HEIGHT/2-240;
    display.setCursor(100, y_start);
    display.print("NIGHT SLEEP");
    display.setCursor(100, y_start+94);
    display.printf("%d:00 + %d Hrs.", NIGHT_SLEEP_START, NIGHT_SLEEP_HRS);

    float temp;
    if (ds3231_get_temp_float(&dev, &temp) == ESP_OK) {
        y_start+= 384;
        display.setTextColor(display.color888(200,200,200));
        display.setCursor(100, y_start);
        display.setTextSize(2);
        display.printf("%.2f C", temp);
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
    gpio_set_direction(GPIO_ENABLE_5V, GPIO_MODE_OUTPUT);
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
                scd4x_co2, scd4x_temperature, scd4x_humidity, scd4x_tem, scd4x_hum);
    #endif
    // Turn on the 3.7 to 5V step-up
    gpio_set_level(GPIO_ENABLE_5V, 1);
    // Wait until board is fully powered
    delay_ms(80);
    display.init();

    if (nvs_boots%2 == 0) {
        display.clearDisplay();
        // Commenting this VCOM is set as default to 2600 (-2.6) which is too high for most epaper displays
        // Leaving that value you might see gray background since it's the top reference voltage
        // Uncomment if you want to see the VCOM difference one boot yes, one no.
        /*
        uint64_t startTime = esp_timer_get_time();
        uint16_t vcom = 1780;
        printf("setVCOM(%d)\n", vcom);
        display.setVCOM(vcom);          // 1780 -1.78 V
        // waitDisplay() 4210 millis after VCOM. DEXA-C097 fabricated by Cinread.com
        display.waitDisplay();
        printf("waitDisplay() %llu millis after VCOM\n", (esp_timer_get_time()-startTime)/1000);
        // Please be aware that all this wait should not be added for another controllers:
        // If I don't wait here at least 5 seconds after busy release more it still hangs SPI
        vTaskDelay(pdMS_TO_TICKS(4800)); 
        */
    }
	// epd_fast:    LovyanGFX uses a 4×4 16pixel tile pattern to display a pseudo 17level grayscale.
	// epd_quality: Uses 16 levels of grayscale
	display.setEpdMode(epd_mode_t::epd_fast);

    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);

    powered_by = gpio_get_level(TPS_POWER_MODE);

#if CONFIG_GET_CLOCK
    // Get clock
    xTaskCreate(getClock, "getClock", 1024*4, NULL, 2, NULL);
#endif
}

