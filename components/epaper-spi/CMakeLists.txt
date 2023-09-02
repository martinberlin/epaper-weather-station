# ↓ return line excludes this component from the build process 
#return()
# Add only the display srcs you are going to use if you want to keep compiled srcs to a minimum:
set(srcs
    # New GOODISPLAY models will be in goodisplay directory
    "calepd/models/goodisplay/gdey0213b74.cpp"
    "calepd/models/goodisplay/gdey0154d67.cpp"
    "calepd/models/goodisplay/gdey027T91.cpp"
    "calepd/models/goodisplay/gdey029T94.cpp"
    #"calepd/models/wave12i48.cpp"
    #"calepd/models/gdew075HD.cpp"
    "calepd/models/gdew075T7.cpp"
    #"calepd/models/gdew075T7Grays.cpp"
    #"calepd/models/gdew075T8.cpp"
    #"calepd/models/gdew0583t7.cpp"
    #"calepd/models/gdew042t2.cpp"
    "calepd/models/gdew042t2Grays.cpp"
    "calepd/models/gdew027w3.cpp"
    # No touch support for now:
    #"calepd/models/gdew027w3T.cpp"
    #"calepd/models/gdew0213i5f.cpp"
    #"calepd/models/gdep015OC1.cpp"
    #"calepd/models/gdeh0154d67.cpp"
    "calepd/models/heltec0151.cpp"
    # DKE
    "calepd/models/color/dke075z83.cpp"

    # 3 colors Goodisplay
    "calepd/models/color/gdeh0154z90.cpp"
    #"calepd/models/color/gdew0583z21.cpp"
    #"calepd/models/color/gdew075z09.cpp"
    #"calepd/models/color/gdew075c64.cpp"
    "calepd/models/color/gdeh042Z96.cpp"
    "calepd/models/color/gdeh042Z21.cpp"
    #"calepd/models/color/wave12i48BR.cpp"
    "calepd/models/dke/depg1020bn.cpp"
    # 7 colors Waveshare
    #"calepd/models/color/wave4i7Color.cpp"
    #"calepd/models/color/wave5i7Color.cpp"

    # Common base classes
    "calepd/epd.cpp"
    "calepd/epd7color.cpp"
    "calepd/epdspi.cpp"
    # Only for 12.48 multi-SPI epapers (EOL)
    #"calepd/epd4spi.cpp"
    )

# If the project does not use a touch display component FT6X36-IDF can be removed or #commented
idf_component_register(SRCS ${srcs}      
                    REQUIRES adafruit-gfx
                             driver esp_timer
                    INCLUDE_DIRS "calepd/include"
)
