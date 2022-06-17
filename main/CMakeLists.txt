# Uncomment only one app_source
set(app_sources "weather-station.cpp")

# Demo credits: nopnop2002 https://github.com/nopnop2002/esp-idf-ds3231
#set(app_sources "tests/ds3231-demo.c")

set(EXTRA_COMPONENT_DIRS i2cdev)

#For SPI epaper tests
#REQUIRES CalEPD
idf_component_register(SRCS ${app_sources} 
        REQUIRES jpegdec 
                 lovyanGFX
                 bosch
                 rtc
                 # Other IDF common components:
        nvs_flash esp-tls esp_http_client protocol_examples_common
        )