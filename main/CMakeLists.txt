# Uncomment only one app_source
set(app_sources "tests/it8951-i2c-scan.cpp")
# Using @bitbank2/JPEGDEC
#set(app_sources "jpgdec-render.cpp")

#For SPI epaper tests
#REQUIRES CalEPD
idf_component_register(SRCS ${app_sources} 
        REQUIRES jpegdec lovyanGFX
        nvs_flash esp-tls esp_http_client bosch
        # Embed the server root certificate into the final binary
        #EMBED_TXTFILES ${project_dir}/ssl_cert/server_cert.pem
        )