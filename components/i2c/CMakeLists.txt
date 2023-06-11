# Make sure to setup your I2C GPIOs in menuconfig -> DS3231 Configuration
set(srcs 
    "ds3231/main/ds3231.c"
    # Shared between both
    "i2cdev.c"
    # PCF8563 used by Lilygo S3 parallel EPD047
    "pcf8563/main/pcf8563.c"
    # Seeed dust sensor HHM330x (our own take from Arduino library)
    "hm3301/hm3301.cpp"
    # Touch capacitive panel
    "touch/FT6X36.cpp"
)

idf_component_register(SRCS ${srcs}
                    REQUIRES driver
INCLUDE_DIRS "ds3231/main"
             "pcf8563/main"
             "hm3301"
             "include"
             "touch/include"
)