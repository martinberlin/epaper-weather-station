# Uncomment only one app_source
idf_build_get_property(target IDF_TARGET)

# 1 Example for IT8951 Boards (Adjusted for DEXA-C097 Good-Display)
message(STATUS "→ Building example for ${target}")
#set(app_sources "it8951/weather-station.cpp")
# Read hourly activities from a Vector table. Example: GYM schedule
#set(app_sources "it8951/rtc-activity/co2-gym-panel.cpp")


# Only for Asian characters (unicode)
#set(app_sources "it8951/weather-station-unicode.cpp")

# 2 Example for epdiy Boards
# Only grayscale test to check component is right added: (weather-station example still not done for this board)
#set(app_sources "epdiy/weather-station.cpp") # Only RTC
   #set(app_sources "epdiy/gym-events-station.cpp")
#set(app_sources "epdiy/CO2-station.cpp")

# 3 BLE Bluedroid example to receive JPG image (Work in progress)
#set(app_sources "BLE/gatt_server_receive.cpp")

#4 SPI displays from GOODISPLAY
#set(app_sources "SPI-displays/weather-station.cpp")
#set(app_sources "SPI-displays/weather-station-pcf8563.cpp")
#set(app_sources "SPI-displays/weather-station-wifi-img.cpp")
#set(app_sources "SPI-displays/jpeg-render.cpp")
#set(app_sources "SPI-displays/air-quality-station.cpp")
#set(app_sources "SPI-displays/particle-draw.cpp")

# Sensors: SDC40 CO2, temperature and humidity
#set(app_sources "tests/sdc40.c")

# Demo credits: nopnop2002 https://github.com/nopnop2002/esp-idf-ds3231
#set(app_sources "tests/rtc/ds3231-demo.c")

# RTC Alarm testing, should change IO6 state in next minute
#set(app_sources "tests/rtc/ds3231-alarm.c")

# I2C scan: Find your I2C addresses
#set(app_sources "tests/rtc/i2c-scan.cpp")

# I2C dust HHM330
#set(app_sources "tests/seeed-hm3301.cpp")

# Test Lilygo RTC: PCF8563
#set(app_sources "tests/Lilygo/rtc-clock.c")
set(app_sources "tests/epdiy_v7/rtc-clock.c")
# PCF8563 RTC but for SPI epaper
#set(app_sources "SPI-displays/tinypico-rtc-pcf8563-clock.cpp")
# small PCB: https://github.com/martinberlin/H-spi-adapters/tree/master/1.54-watch-pcb
# set(app_sources "SPI-displays/smart-watch/pcf8563-touch-watch.cpp")

# Only deepsleep and does nothing more to test consumtion
#set(app_sources "tests/deepsleep-consumption.cpp")

# Test 2113 STAT pin
#set(app_sources "tests/tps2113-powered-by.cpp")

# TFT Test 
#set(app_sources "tft-parallel-8/tft-test.cpp")
#set(app_sources "tests/tft-ILI9341/tft-collision-circles.cpp")

set(app_requires        
                        # Used for IT8951 but also has support for many other displays
                        "lovyanGFX"
                        "bosch"
                        "i2c"
                        "big-fonts"
                        "bt"  # esp_bt (bluetooth)
                        "nvs_flash"
                        # SCD40 Co2, temp, humidity sensor
                        "scd4x"
                        # ADC Battery voltage
                        "adc_compat"
                        # Only for epdiy boards (Check https://github.com/vroland/epdiy)
                        # epdiy will run only with set-target esp32.  Not with esp32s3 until is upgraded to IDF v5
                        # Add a starter line: return() in CMakeLists() in order to disable the component
                        "epaper-parallel"
                        # For SPI epapers
                        "epaper-spi"
                        # jpeg decoder
                        "jpegdec"
                        # Only for asian unicode fonts
                        "OpenFontRender"
                        # One LED WS2812 (EPDiy v5: Inkster, not used for now)
                        #"ws2812_led"
                        # Other IDF common components:
                        "nvs_flash"
                        "esp-tls"
                        "esp_http_client"
                        "protocol_examples_common"
)

idf_component_register(SRCS ${app_sources}
        REQUIRES ${app_requires}
        INCLUDE_DIRS "it8951/translations"
        # Embed the server root certificate into the final binary
        EMBED_TXTFILES ${project_dir}/main/SPI-displays/ssl_cert/server_cert.pem
        )

# FIX idf >= 5 complaining that %d types are not (int)
idf_component_get_property(lib "epaper-parallel" COMPONENT_LIB)
target_compile_options(${lib} PRIVATE -Wno-error=format)