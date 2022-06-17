# Uncomment only one app_source
#set(app_sources "tests/it8951-i2c-scan.cpp")

# Demo credits: nopnop2002 https://github.com/nopnop2002/esp-idf-ds3231
set(app_sources "tests/ds3231-demo.c")

# Using @bitbank2/JPEGDEC
#set(app_sources "jpgdec-render.cpp")

#For SPI epaper tests
#REQUIRES CalEPD
idf_component_register(SRCS ${app_sources} 
        REQUIRES jpegdec 
                 lovyanGFX
                 bosch
                 rtc
                 # Other IDF common components:
        nvs_flash esp-tls esp_http_client protocol_examples_common
        # Embed the server root certificate into the final binary
        #EMBED_TXTFILES ${project_dir}/ssl_cert/server_cert.pem
        )