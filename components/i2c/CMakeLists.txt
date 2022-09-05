idf_component_register(
    SRCS sdc40/scd4x.c
         i2cdev.c
    INCLUDE_DIRS include
    REQUIRES log driver
)
