# Make sure to check what sensor model you have
set(srcs 
    #"ds3231/main/main.c"
    "ds3231/main/ds3231.c"
    "ds3231/main/i2cdev2.c"
)

idf_component_register(SRCS ${srcs}
                    
INCLUDE_DIRS "ds3231/main"
)