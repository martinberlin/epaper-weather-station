# Make sure to setup your I2C GPIOs in menuconfig -> DS3231 Configuration
set(srcs 
    "ds3231/main/ds3231.c"
    # Original i2cdev.c won't work in C++ (forked it and adapted i2cdev2 for that reason)
    "ds3231/main/i2cdev2.c"
)

idf_component_register(SRCS ${srcs}
                    REQUIRES driver
INCLUDE_DIRS "ds3231/main"
)