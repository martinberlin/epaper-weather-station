# Make sure to check what sensor model you have
set(srcs 
    #"ds3231/main/main.c"
    "ds3231/main/ds3231.c"
    "ds3231/main/i2cdev.c"
)

# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "ds3231/main"
)