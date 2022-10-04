#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "pcf8563.h"

#define CHECK_ARG(ARG) do { if (!ARG) return ESP_ERR_INVALID_ARG; } while (0)

uint8_t bcd2dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0f);
}

uint8_t dec2bcd(uint8_t val)
{
    return ((val / 10) << 4) + (val % 10);
}


esp_err_t pcf8563_init_desc(i2c_dev_t *dev, i2c_port_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio)
{
    CHECK_ARG(dev);

    dev->port = port;
    dev->addr = PCF8563_ADDR;
    dev->sda_io_num = sda_gpio;
    dev->scl_io_num = scl_gpio;
    dev->clk_speed = I2C_FREQ_HZ;
    return i2c_master_init(port, sda_gpio, scl_gpio);
}

esp_err_t pcf8563_reset(i2c_dev_t *dev)
{
	CHECK_ARG(dev);

	uint8_t data[2];
	data[0] = 0;
	data[1] = 0;
	
	return i2c_dev_write_reg(dev, PCF8563_ADDR_STATUS1, data, 2);
}

esp_err_t pcf8563_set_time(i2c_dev_t *dev, struct tm *time)
{
    CHECK_ARG(dev);
    CHECK_ARG(time);

    uint8_t data[7];

    /* time/date data */
    data[0] = dec2bcd(time->tm_sec);
    data[1] = dec2bcd(time->tm_min);
    data[2] = dec2bcd(time->tm_hour);
    data[3] = dec2bcd(time->tm_mday);
    data[4] = dec2bcd(time->tm_wday);		// tm_wday is 0 to 6
    data[5] = dec2bcd(time->tm_mon + 1);	// tm_mon is 0 to 11
    data[6] = dec2bcd(time->tm_year - 2000);

    return i2c_dev_write_reg(dev, PCF8563_ADDR_TIME, data, 7);
}

esp_err_t pcf8563_get_time(i2c_dev_t *dev, struct tm *time)
{
    CHECK_ARG(dev);
    CHECK_ARG(time);

    uint8_t data[7];

    /* read time */
    esp_err_t res = i2c_dev_read_reg(dev, PCF8563_ADDR_TIME, data, 7);
        if (res != ESP_OK) return res;

    /* convert to unix time structure */
    ESP_LOGD("", "data=%02x %02x %02x %02x %02x %02x %02x",
                 data[0],data[1],data[2],data[3],data[4],data[5],data[6]);
    time->tm_sec = bcd2dec(data[0] & 0x7F);
    time->tm_min = bcd2dec(data[1] & 0x7F);
    time->tm_hour = bcd2dec(data[2] & 0x3F);
    time->tm_mday = bcd2dec(data[3] & 0x3F);
    time->tm_wday = bcd2dec(data[4] & 0x07);		// tm_wday is 0 to 6
    time->tm_mon  = bcd2dec(data[5] & 0x1F) - 1;	// tm_mon is 0 to 11
    time->tm_year = bcd2dec(data[6]) + 2000;
    time->tm_isdst = 0;

    return ESP_OK;
}


