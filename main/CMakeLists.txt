# Uncomment only one app_source

# Example for IT8951 Boards
set(app_sources "it8951/weather-station.cpp")

# 1st test I2C scan: Find your I2C addresses
#set(app_sources "tests/it8951-i2c-scan.cpp")

# Demo credits: nopnop2002 https://github.com/nopnop2002/esp-idf-ds3231
#set(app_sources "tests/ds3231-demo.c")

#For SPI epaper tests
#REQUIRES CalEPD
idf_component_register(SRCS ${app_sources} 
        REQUIRES jpegdec 
                 lovyanGFX
                 bosch
                 rtc
                 big-fonts
                 # Only for epdiy boards (Check https://github.com/vroland/epdiy)
                 epaper-parallel
                 # Other IDF common components:
        nvs_flash esp-tls esp_http_client protocol_examples_common
        )