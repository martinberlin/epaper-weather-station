#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

// Just a quick I2C Address scan. This was only to test Seeed sensor
#ifdef SEED_HM_SENSOR
  #define ENABLE_SEEED_GPIO GPIO_NUM_48
#endif

// OPTIONAL Some touch controllers need a Reset pin to be toggled
#define EN_FL_POWER  GPIO_NUM_5

#define IMU_I2C  GPIO_NUM_11
#define TPS_WAKE  GPIO_NUM_14
#define TOUCH_RST  GPIO_NUM_9
#define TOUCH_INT  GPIO_NUM_3

// Please define the target where you are flashing this
// only one should be true:
#define TARGET_EPDIY_V5     false
#define TARGET_EPDIY_V7     true
#define TARGET_LILYGOS3     false
#define TARGET_S3_CINWRITE  false
#define TARGET_ESP32_DEFAULT false
#define TARGET_C3_WATCH     false

#define LED1 GPIO_NUM_34
#define LED2 GPIO_NUM_35

#if TARGET_EPDIY_V5
    // EPDiy board v5, check the other configs using other boards:
    #define SDA_GPIO 13
    #define SCL_GPIO 14
#endif
#if TARGET_EPDIY_V7
    // EPDiy board v7
    #define SDA_GPIO 39
    #define SCL_GPIO 40
#endif
#if TARGET_ESP32_DEFAULT
    #define SDA_GPIO 21 // 
    #define SCL_GPIO 22 //8
#endif
#if TARGET_S3_CINWRITE
    //ESP32-S3 Cinwrite PCB
    #define SDA_GPIO 7
    #define SCL_GPIO 8
#endif
#if TARGET_LILYGOS3
    // Lilygo S3 EPD047 (Sold in Tindie)
    #define SDA_GPIO 18
    #define SCL_GPIO 17
#endif
#if TARGET_C3_WATCH
    // Lilygo S3 EPD047 (Sold in Tindie)
    #define SDA_GPIO 5
    #define SCL_GPIO 4
#endif

#define I2C_MASTER_FREQ_HZ 400000                     /*!< I2C master clock frequency */
#define I2C_SCLK_SRC_FLAG_FOR_NOMAL       (0)         /*!< Any one clock source that is available for the specified frequency may be choosen*/

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  #define portTICK_RATE_MS portTICK_PERIOD_MS
#endif
// Enable on HIGH 5V boost converter
//#define GPIO_ENABLE_5V GPIO_NUM_38

static const char *TAG = "i2c test";

extern "C"
{
    void app_main();
}

// setup i2c master
static esp_err_t i2c_master_init()
{
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = SDA_GPIO;
    conf.scl_io_num = SCL_GPIO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;

    i2c_param_config(I2C_NUM_0, &conf);
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

void app_main()
{
    //return;
    printf("Hello, this is an I2C test\n\n");
    int touch;
    gpio_set_direction(IMU_I2C, GPIO_MODE_OUTPUT);
    gpio_set_level(IMU_I2C, 1);
    gpio_set_direction(TPS_WAKE, GPIO_MODE_OUTPUT);
    gpio_set_level(TPS_WAKE, 1);
    ESP_LOGI(TAG, "SCL_GPIO = %d", SCL_GPIO);
    ESP_LOGI(TAG, "SDA_GPIO = %d", SDA_GPIO);

    char * device;
    
    #ifdef RESET_TOUCH
    gpio_set_direction(TOUCH_RST, GPIO_MODE_OUTPUT);
    gpio_set_pull_mode(TOUCH_RST, GPIO_PULLUP_ONLY);
    gpio_set_level(TOUCH_RST, 1);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    // TOGGLE Reset in the Kaleido touch
    gpio_set_level(TOUCH_RST, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    gpio_set_level(TOUCH_RST, 1);
    #endif

        // i2c init & scan
    if (i2c_master_init() != ESP_OK)
        ESP_LOGE(TAG, "i2c init failed\n");

    printf("I2C scan: \n");
     
     while (true) {
     for (uint8_t i = 1; i < 127; i++)
     {
        int ret;
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, 1);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 100 / portTICK_RATE_MS);
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK)
        {
            switch (i)
            {
            case 0x20:
                device = (char *)"PCA9535";
                break;
            case 0x24:
            touch = 0x24;
                device = (char *)"TT21100 or some Touch";
                break;
            case 0x38:
                device = (char *)"FT6X36 Touch";
                break;
            case 0x51:
                device = (char *)"PCF8563 RTC";
                break;
            case 0x68:
                device = (char *)"DS3231 or TPS PMIC";
                //gpio_set_level(LED1, 1);
                break;
            default:
                device = (char *)"unknown";
                //gpio_set_level(LED2, 1);
                break;
            }
            printf("Found device at: 0x%2x %s\n", i, device);
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
vTaskDelay(1000 / portTICK_PERIOD_MS);
    
     }

    
}
