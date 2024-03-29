# ↓ return line excludes this component from the build process 
#return()

# ↓ if target not working as intended, will still include this if S3
if(IDF_TARGET EQUAL "esp32s3")
    idf_build_get_property(target IDF_TARGET)
    message(STATUS "Excluding epdiy for ${target}")
    return()
endif()
message(STATUS "Building epdiy for ${target}")

set(basedir "epdiy/src/epdiy")
set(app_sources "${basedir}/epdiy.c"
                "${basedir}/render.c"
                "${basedir}/output_lcd/render_lcd.c"
                "${basedir}/output_lcd/lcd_driver.c"
                "${basedir}/output_i2s/render_i2s.c"
                "${basedir}/output_i2s/rmt_pulse.c"
                "${basedir}/output_i2s/i2s_data_bus.c"
                "${basedir}/output_common/lut.c"
                "${basedir}/output_common/line_queue.c"
                "${basedir}/output_common/render_context.c"
                "${basedir}/font.c"
                "${basedir}/displays.c"
                "${basedir}/board_specific.c"
                "${basedir}/builtin_waveforms.c"
                "${basedir}/highlevel.c"
                "${basedir}/board/tps65185.c"
                "${basedir}/board/pca9555.c"
                "${basedir}/board/epd_board.c"
                "${basedir}/board/epd_board_common.c"
                "${basedir}/board/epd_board_lilygo_t5_47.c"
                "${basedir}/board/epd_board_v2_v3.c"
                "${basedir}/board/epd_board_v4.c"
                "${basedir}/board/epd_board_v5.c"
                "${basedir}/board/epd_board_v6.c"
                "${basedir}/board/epd_board_v7.c"
)


# Can also use IDF_VER for the full esp-idf version string but that is harder to parse. i.e. v4.1.1, v5.0-beta1, etc
if (${IDF_VERSION_MAJOR} GREATER 4)
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "${basedir}/include" REQUIRES driver esp_timer esp_adc esp_lcd)
else()
    idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "${basedir}/include" REQUIRES esp_adc_cal esp_timer)
endif()