// FastEPD component 
#include "FastEPD.h"
FASTEPD epaper;

uint8_t* fb;

extern "C" {
   void app_main();
}

void app_main() {
    epaper.initPanel(BB_PANEL_EPDIY_V7);
    epaper.setPanelSize(1024, 758);
    epaper.setMode(BB_MODE_4BPP);
    epaper.fillScreen(0xf);
    fb = epaper.currentBuffer();
    printf("FastEPD @bitbank2\n");
}