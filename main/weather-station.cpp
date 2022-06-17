// Don't really understand why it does not reference functions correctly using C++
#include "i2cdev2.c"
#include "ds3231.c"

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
// RESEARCH FOR SPI HAT Cinread:  https://github.com/martinberlin/H-cinread-it895
// Goal: Read temperature from Bosch sensor and print it on the epaper display
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

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

RTC_DATA_ATTR static int boot_count = 0;
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
  lgfx::Light_PWM     _light_instance;

public:
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

void delay_ms(uint32_t period_ms) {
    ets_delay_us(period_ms * 1000);
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
    ESP_ERROR_CHECK( nvs_flash_init() );
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
        .tm_year = timeinfo.tm_year + 1900
    };

    if (ds3231_set_time(&dev, &time) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not set time.");
        while (1) { vTaskDelay(1); }
    }
    ESP_LOGI(pcTaskGetName(0), "Set initial date time done");

    // goto deep sleep
    const int deep_sleep_sec = 10;
    ESP_LOGI(pcTaskGetName(0), "Entering deep sleep for %d seconds", deep_sleep_sec);
    esp_deep_sleep(1000000LL * deep_sleep_sec);
}

void getClock(void *pvParameters)
{
    // Initialise the xLastWakeTime variable with the current time.
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Get RTC date and time
    while (1) {
        float temp;
        struct tm rtcinfo;

        if (ds3231_get_temp_float(&dev, &temp) != ESP_OK) {
            ESP_LOGE(pcTaskGetName(0), "Could not get temperature.");
            while (1) { vTaskDelay(1); }
        }

        if (ds3231_get_time(&dev, &rtcinfo) != ESP_OK) {
            ESP_LOGE(pcTaskGetName(0), "Could not get time.");
            while (1) { vTaskDelay(1); }
        }
        display.setCursor(100,EPD_HEIGHT/2);
        display.printf("%02d:%02d:%02d", rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec);
        
        ESP_LOGI(pcTaskGetName(0), "%04d-%02d-%02d %02d:%02d:%02d, %.2f deg Cel", 
            rtcinfo.tm_year, rtcinfo.tm_mon + 1,
            rtcinfo.tm_mday, rtcinfo.tm_hour, rtcinfo.tm_min, rtcinfo.tm_sec, temp);
	vTaskDelayUntil(&xLastWakeTime, 1000);
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

void app_main()
{
    display.init();
    display.clearDisplay();
	// epd_fast:    LovyanGFX uses a 4×4 16pixel tile pattern to display a pseudo 17level grayscale.
	// epd_quality: Uses 16 levels of grayscale
	display.setEpdMode(epd_mode_t::epd_fast);
	display.setFont(&AsciiFont24x48);

    // Initialize RTC
    if (ds3231_init_desc(&dev, I2C_NUM_0, (gpio_num_t) CONFIG_SDA_GPIO, (gpio_num_t) CONFIG_SCL_GPIO) != ESP_OK) {
        ESP_LOGE(pcTaskGetName(0), "Could not init device descriptor.");
        while (1) { vTaskDelay(1); }
    }

    ++boot_count;
    ESP_LOGI(TAG, "CONFIG_SCL_GPIO = %d", CONFIG_SCL_GPIO);
    ESP_LOGI(TAG, "CONFIG_SDA_GPIO = %d", CONFIG_SDA_GPIO);
    ESP_LOGI(TAG, "CONFIG_TIMEZONE= %d", CONFIG_TIMEZONE);
    ESP_LOGI(TAG, "Boot count: %d", boot_count);

#if CONFIG_SET_CLOCK
    // Set clock & Get clock
    if (boot_count == 1) {
        xTaskCreate(setClock, "setClock", 1024*4, NULL, 2, NULL);
    } else {
        xTaskCreate(getClock, "getClock", 1024*4, NULL, 2, NULL);
    }
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

