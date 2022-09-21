# NOTE: This is just to encapsulate in a single C file ADC reading functions that will work in IDF 4.4 & IDF 5
#       since we use different methods to calibrate and read the values

# What a pain to use ADC in different IDF versions Espressif ;)
if (IDF_VERSION_MAJOR EQUAL 5)
    set(adc_requires "esp_adc")
    set(srcs "adc_compat.c")
elseif (IDF_VERSION_MAJOR EQUAL 4)
    set(adc_requires "esp_adc_cal")
    set(srcs "adc_compat4.c")
endif()

idf_component_register(SRCS ${srcs}
                    REQUIRES ${adc_requires}
INCLUDE_DIRS "include"
)