# Uncomment only one app_source
idf_build_get_property(target IDF_TARGET)

# 1 Example for IT8951 Boards
message(STATUS "→ Building example for ${target}")
set(app_sources "it8951/weather-station.cpp")

# 2 Example for epdiy Boards
# Only grayscale test to check component is right added: (weather-station example still not done for this board)
#set(app_sources "epdiy/weather-station.cpp")

# 1st test I2C scan: Find your I2C addresses
#set(app_sources "tests/it8951-i2c-scan.cpp")

# Demo credits: nopnop2002 https://github.com/nopnop2002/esp-idf-ds3231
#set(app_sources "tests/ds3231-demo.c")

# Only deepsleep and does nothing more to test consumtion
#set(app_sources "tests/deepsleep-consumption.cpp")

# TFT Test 
#set(app_sources "tft-parallel-8/tft-test.cpp")
#set(app_sources "tests/tft-ILI9341/tft-collision-circles.cpp")

idf_component_register(SRCS ${app_sources} 
        REQUIRES jpegdec 
                 lovyanGFX
                 bosch
                 rtc
                 big-fonts
                 # Only for epdiy boards (Check https://github.com/vroland/epdiy)
                 # epdiy will run only with set-target esp32.  Not with esp32s2, not with esp32s3
                 # Add a starter line: return() in CMakeLists() in order to disable the component
                 epaper-parallel
                 #For SPI epapers
                 epaper-spi 
                 # Other IDF common components:
        nvs_flash esp-tls esp_http_client protocol_examples_common 
        )