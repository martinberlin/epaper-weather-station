# Make sure to setup your I2C GPIOs in menuconfig -> DS3231 Configuration
set(srcs 
    "ds3231/main/ds3231.c"
    # Shared between both
    "i2cdev.c"
    # PCF8563 used by Lilygo S3 parallel EPD047
    "pcf8563/main/pcf8563.c"
)

idf_component_register(SRCS ${srcs}
                    REQUIRES driver
INCLUDE_DIRS "ds3231/main"
             "pcf8563/main"
             "include"
)