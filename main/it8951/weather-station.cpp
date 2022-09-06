// Don't really understand why it does not reference functions correctly using C++
#include "i2cdev2.c"
#include "ds3231.c"

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
#define STATION_USE_SDC40 false

#if STATION_USE_SDC40
#include "scd4x_i2c.h"
#include "sensirion_common.h"
#include "sensirion_i2c_hal.h"
uint16_t scd4x_co2 = 0;
float scd4x_tem = 0;
float scd4x_hum = 0;
#endif
// RESEARCH FOR SPI HAT Cinread:  https://github.com/martinberlin/H-cinread-it895
// Goal: Read temperature from Bosch sensor and print it on the epaper display
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
// Big fonts
#include <Ubuntu_M24pt8b.h>
#include <Ubuntu_M48pt8b.h>
#include <DejaVuSans_Bold60pt7b.h>

// Enable on HIGH 5V boost converter
#define GPIO_ENABLE_5V GPIO_NUM_38
// STAT pin of TPS2113
#define TPS_POWER_MODE GPIO_NUM_5

// Clock will refresh every:
#define DEEP_SLEEP_SECONDS 120
// Avoids printing text always in same place so we don't leave marks in the epaper (Although parallel get well with that)
#define X_RANDOM_MODE true
uint64_t USEC = 1000000;
// Weekdays and months translatables
#include <catala.h>
//#include <english.h>
//#include <spanish.h>

uint8_t powered_by = 0;
uint8_t DARK_MODE = 1;
// You have to set these CONFIG value using: idf.py menuconfig --> DS3231 Configuration
#if 0
#define CONFIG_SCL_GPIO		15
#define CONFIG_SDA_GPIO		16
#define	CONFIG_TIMEZONE		9
#define NTP_SERVER 		"pool.ntp.org"
#endif
// Display configuration. Adjust to yours if you use a different one:
#define EPD_WIDTH  1200
#define EPD_HEIGHT 825

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
      cfg.pin_sclk = CONFIG_EINK_SPI_CLK;            // SPIのSCLKピン番号を設定
      cfg.pin_mosi = CONFIG_EINK_SPI_MOSI;           // SPIのMOSIピン番号を設定
      cfg.pin_miso = CONFIG_EINK_SPI_MISO;           // SPIのMISOピン番号を設定 (-1 = disable)
      cfg.pin_dc   = -1;            // SPIのD/Cピン番号を設定  (-1 = disable)
 
      _bus_instance.config(cfg);    // 設定値をバスに反映します。
      _panel_instance.setBus(&_bus_instance);      // バスをパネルにセットします。
    }

    { // 表示パネル制御の設定を行います。
      auto cfg = _panel_instance.config();    // 表示パネル設定用の構造体を取得します。

      cfg.pin_cs           =    CONFIG_EINK_SPI_CS;  // CSが接続されているピン番号   (-1 = disable)
      cfg.pin_rst          =    -1;
      cfg.pin_busy         =    CONFIG_EINK_BUSY;    // BUSYが接続されているピン番号 (-1 = disable)

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
    // Turn off the 3.7 to 5V step-up and put all IO pins in INPUT mode
    uint8_t EP_CONTROL[] = {CONFIG_EINK_SPI_CLK, CONFIG_EINK_SPI_MOSI, CONFIG_EINK_SPI_MISO, CONFIG_EINK_SPI_CS, GPIO_ENABLE_5V};
    for (int io = 0; io < 5; io++) {
        gpio_set_level((gpio_num_t) EP_CONTROL[io], 0);
        gpio_set_direction((gpio_num_t) EP_CONTROL[io], GPIO_MODE_INPUT);
    }
    ESP_LOGI(pcTaskGetName(0), "DEEP_SLEEP_SECONDS: %d seconds to wake-up", DEEP_SLEEP_SECONDS);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_SECONDS * USEC);
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

    display.setFont(&DejaVuSans_Bold60pt7b);
    display.println("Initial date time\nis saved on RTC\n");

    display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    // Wait some time to see if disconnecting all changes background color
    delay_ms(50); 
    // goto deep sleep
    deep_sleep();
}

void getClock(void *pvParameters)
{
    // Initialise the xLastWakeTime variable with the current time.
    // TickType_t xLastWakeTime = xTaskGetTickCount();

    // Get RTC date and time
    float temp;
    struct tm rtcinfo;

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
    // Start Y line:
    uint16_t y_start = EPD_HEIGHT/2-340;
    // Turn on black background if Dark mode
    if (DARK_MODE) {
      display.fillScreen(display.color888(0,0,0));
    }
    // Print day
    uint16_t x_cursor = 100;
    if (X_RANDOM_MODE) {
        x_cursor = 100 + generateRandom(400);
    }
    display.setCursor(x_cursor, y_start-20);
    display.setTextColor(display.color888(200,200,200));
    display.setFont(&DejaVuSans_Bold60pt7b);
    display.printf("%s", weekday_t[rtcinfo.tm_wday]);
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
    y_start+=200;
    x_cursor = 100;
    if (X_RANDOM_MODE) {
        x_cursor = 100 + generateRandom(320);
    }
    display.setCursor(x_cursor, y_start);
    display.setTextColor(display.color888(70,70,70));
    display.setTextSize(1);
    // N month, year
    display.printf("%d %s, %d", rtcinfo.tm_mday, month_t[rtcinfo.tm_mon], rtcinfo.tm_year);
    // If you want YYYY-MM-DD basic example:
    //display.printf("%04d-%02d-%02d", rtcinfo.tm_year, rtcinfo.tm_mon + 1, rtcinfo.tm_mday);

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

    #if STATION_USE_SDC40
    display.setFont(&Ubuntu_M48pt8b);
    uint16_t left_margin = 400;
    display.setCursor(EPD_WIDTH-left_margin,y_start);
    display.printf("%d CO2", scd4x_co2);
    y_start+=130;
    display.setFont(&Ubuntu_M24pt8b);
    display.setCursor(EPD_WIDTH-left_margin,y_start);
    display.printf("%.1f C", scd4x_tem);
    y_start+=80;
    display.setCursor(EPD_WIDTH-left_margin,y_start);
    display.printf("%.1f %% H", scd4x_hum);
    #endif
    /*
    uint16_t vcom = display.getVCOM(); // getVCOM: Not used for now
    display.printf("vcom:%d", vcom);
    */

    /* 
    // Print credits:
    y_start+=120;
    display.setCursor(100,y_start);
    display.setTextColor(display.color888(120,120,120));
    display.print("Epaper weather station"); */

    ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, Week day:%d, %.2f °C", 
        rtcinfo.tm_year, rtcinfo.tm_mon + 1,
        rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, rtcinfo.tm_wday, temp);
    
    // Not needed if we go to sleep and it has a load switch
    //display.powerSaveOn();
    
    deep_sleep();
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

#if STATION_USE_SDC40
int16_t sdc40_read() {
    int16_t error = 0;
    //int16_t sensirion_i2c_hal_init(int gpio_sda, int gpio_scl);
    sensirion_i2c_hal_init(CONFIG_SDA_GPIO, CONFIG_SCL_GPIO);

    // Clean up potential SCD40 states
    scd4x_wake_up();
    scd4x_stop_periodic_measurement();
    scd4x_reinit();

    error = scd4x_start_periodic_measurement();
    if (error) {
        ESP_LOGE(TAG, "Error executing scd4x_start_periodic_measurement(): %i\n", error);
    }
    printf("Waiting for first measurement... (5 sec)\n");
    uint8_t read_nr = 0;
    uint8_t reads_till_snapshot = 1;

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
        int32_t temperature;
        int32_t humidity;
        error = scd4x_read_measurement(&scd4x_co2, &temperature, &humidity);
        if (error) {
            ESP_LOGE(TAG, "Error executing scd4x_read_measurement(): %i\n", error);
        } else if (scd4x_co2 == 0) {
            ESP_LOGI(TAG, "Invalid sample detected, skipping.\n");
        } else {
            scd4x_tem = (float)temperature/1000;
            scd4x_hum = (float)humidity/1000;  
            ESP_LOGI(TAG, "CO2 : %u", scd4x_co2);
            ESP_LOGI(TAG, "Temp: %d m°C %.1f C", temperature, scd4x_tem);
            ESP_LOGI(TAG, "Humi: %d mRH %.1f %%\n", humidity, scd4x_hum);
            read_nr++;
            if (read_nr == reads_till_snapshot) break;
        }
    }
    scd4x_power_down();
    // Release I2C
    sensirion_i2c_hal_free();

    return error;
}

#endif

// Flag to know that we've synced the hour with timeQuery request
int16_t nvs_boots = 0;

void app_main()
{
    #if STATION_USE_SDC40
    sdc40_read();
    #endif

    gpio_set_direction(TPS_POWER_MODE, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_ENABLE_5V ,GPIO_MODE_OUTPUT);
    // Turn on the 3.7 to 5V step-up
    gpio_set_level(GPIO_ENABLE_5V, 1);
    // Wait until board is fully powered
    delay_ms(80);

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

    display.init();

    // Commenting this VCOM is set as default to 2600 (-2.6) which is too high for most epaper displays
    // Leaving that value you might see gray background since it's the top reference voltage
    if (nvs_boots%2 == 0) {
        display.clearDisplay();
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
        // If I don't wait here at least 3 seconds after busy release more it still hangs SPI
        vTaskDelay(pdMS_TO_TICKS(4800)); 
        */
    }
	// epd_fast:    LovyanGFX uses a 4×4 16pixel tile pattern to display a pseudo 17level grayscale.
	// epd_quality: Uses 16 levels of grayscale
	display.setEpdMode(epd_mode_t::epd_fast);

    // Initialize RTC
    if (ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);

    powered_by = gpio_get_level(TPS_POWER_MODE);

#if CONFIG_SET_CLOCK
    // Set clock & Get clock
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
}

