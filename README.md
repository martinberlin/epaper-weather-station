# Epaper weather station
Collection of examples to read sensors (I2C and others) and publish real-time information with minimun consumption using fast ESP32S3 and epaper controllers such as IT8951

The goal of this repository is make a deep dive into ESP32 IDF components (That are also compatible with latest Arduino-esp32) and make examples that read sensors and provide real-time data such as: 

- BME280 (Pressure, Humidity, Temperature) Note: using only first uppercase in the cases below
- BMP280 & BMP180 (P, T)
- DS3231 modules (RTC Real-time clock, additional T readings)
- Any others that you sent us or we have around our studio

## PCB breadboard and open source SPI HAT

As a display we would like to use a modern and powerful IT8951 9.7" using LovyanGFX as a component (Or similar size epaper controller) but also this could be adapted to use our [component CalEPD](https://github.com/martinberlin/CalEPD) and any SPI parallel epaper or EPDiy if you want to control paralell epapers using an EPDiy board.
At the same time since Lovyan GFX supports a pletora of displays including hi-speed 8 and 16 bit parallel TFT's this work is possible to be ported to any of them. 

Now if you are interested in making a low-consumption, hi-resolution epaper project then IT8951 controller is an affordable choice and it's sold both by:

- [Waveshare 10.3" epaper kit, 16-bit parallel](https://www.waveshare.com/product/displays/e-paper/epaper-1/10.3inch-e-paper-hat.htm), being the most expensive option. Other cheaper options are also available using the same Eink IT8951 controller
- [Goodisplay DEXA-C097 9.7" Cinread epaper controller, 8-bit parallel](https://www.good-display.com/product/425.html) which has a nice resolution at 1200*825 and it's affordable at 56 dollars per unit.

This is the demo-board we make for this project, that you can also make yourself, withouth the need to buy our PCB adapter.

![Cinwrite test board](assets/s3-hat-dexa-it8951-pcb.jpg)

And this is the Cinwrite SPI master controller that we design in order to make a product that fits standards and is compact enough to use in a nice 3D-printed case

![Cinwrite](https://github.com/martinberlin/H-cinread-it8951/raw/main/components/assets/IT8951-HAT-Front.jpg)

This will be soon available in Tindie but [Cinwrite PCB is also open source](https://github.com/martinberlin/H-cinread-it8951) so you can fabricate it yourself if you want and use it also for a commercial project (As long as credits are given as the License states)

Here we will publish the recollection of C components and demos to make this happen. Make sure to keep updated!

Hit the ⭐ button to be aware of this repository updates.