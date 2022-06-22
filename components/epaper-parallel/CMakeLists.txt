# ↓ return line excludes this component from the build process 
return()

# ↓ if target not working as intended, will still include this if S3
if(IDF_TARGET EQUAL "esp32s3")
    idf_build_get_property(target IDF_TARGET)
    message(STATUS "Excluding epdiy for ${target}")
    return()
endif()
#message(STATUS "Building epdiy for ${target}")

set(base_folder, "epdiy/src/epd_driver/")
set(app_sources "epdiy/src/epd_driver/epd_driver.c"
"epdiy/src/epd_driver/epd_board.c"
"epdiy/src/epd_driver/render.c"
"epdiy/src/epd_driver/display_ops.c"
"epdiy/src/epd_driver/tps65185.c"
"epdiy/src/epd_driver/pca9555.c"
"epdiy/src/epd_driver/font.c"
"epdiy/src/epd_driver/lut.c"
"epdiy/src/epd_driver/board_specific.c"
"epdiy/src/epd_driver/builtin_waveforms.c"
"epdiy/src/epd_driver/i2s_data_bus.c"
"epdiy/src/epd_driver/rmt_pulse.c"
"epdiy/src/epd_driver/highlevel.c"
"epdiy/src/epd_driver/epd_temperature.c"
"epdiy/src/epd_driver/board/epd_board_common.c"
"epdiy/src/epd_driver/board/epd_board_lilygo_t5_47.c"
"epdiy/src/epd_driver/board/epd_board_v2_v3.c"
"epdiy/src/epd_driver/board/epd_board_v4.c"
"epdiy/src/epd_driver/board/epd_board_v5.c"
"epdiy/src/epd_driver/board/epd_board_v6.c"
)

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "epdiy/src/epd_driver/include" REQUIRES esp_adc_cal)

set_source_files_properties("lut.c" PROPERTIES COMPILE_OPTIONS -mno-fix-esp32-psram-cache-issue)
