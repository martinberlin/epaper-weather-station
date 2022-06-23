#define LGFX_USE_V1
#include <LovyanGFX.hpp>
extern "C" {
    void app_main();
}

class LGFX : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_Parallel8 _bus_instance;
    lgfx::Light_PWM     _light_instance;
   public:
    LGFX(void)
    {
      {
        auto cfg = _bus_instance.config();
        // Bus_Parallel8 config
        //cfg.i2s_port = I2S_NUM_0;    // Not for S3
        cfg.freq_write = 10000000;    // 送信クロック (最大20MHz, 80MHzを整数で割った値に丸められます)
        cfg.pin_wr = 11;              // WR を接続しているピン番号
        cfg.pin_rd = 14;              // RD を接続しているピン番号
        cfg.pin_rs = 13;              // RS(D/C)を接続しているピン番号

        cfg.pin_d0 = 36;
        cfg.pin_d1 = 37;
        cfg.pin_d2 = 38;
        cfg.pin_d3 = 39;
        cfg.pin_d4 = 40;
        cfg.pin_d5 = 41;
        cfg.pin_d6 = 42;
        cfg.pin_d7 = 2;
        _bus_instance.config(cfg);    // 設定値をバスに反映します。
        _panel_instance.setBus(&_bus_instance);      // バスをパネルにセットします。
      }
      // Backlight BL pin in TFT
      { 
      auto cfg = _light_instance.config();    // バックライト設定用の構造体を取得します。

      cfg.pin_bl = 46;              // バックライトが接続されているピン番号
      cfg.invert = false;           // バックライトの輝度を反転させる場合 true
      cfg.freq   = 44100;           // バックライトのPWM周波数
      cfg.pwm_channel = 7;          // 使用するPWMのチャンネル番号

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
      }

      // TFT config CS, RST & dimensions
      {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           =    10;  // Chip Select
      cfg.pin_rst          =    12;  // RST  (-1 = disable)
      cfg.pin_busy         =    -1;  // BUSY (-1 = disable)

      cfg.panel_width      =   320;  // 実際に表示可能な幅
      cfg.panel_height     =   240;  // 実際に表示可能な高さ
      cfg.offset_x         =     0;  // パネルのX方向オフセット量
      cfg.offset_y         =     0;  // パネルのY方向オフセット量
      cfg.offset_rotation  =     0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.dummy_read_pixel =     8;  // ピクセル読出し前のダミーリードのビット数
      cfg.dummy_read_bits  =     1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
      cfg.readable         =  true;  // データ読出しが可能な場合 trueに設定
      cfg.invert           = false;  // パネルの明暗が反転してしまう場合 trueに設定
      cfg.rgb_order        = false;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
      cfg.dlen_16bit       = false;  // 16bitパラレルやSPIでデータ長を16bit単位で送信するパネルの場合 trueに設定
      cfg.bus_shared       = false;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)
      _panel_instance.config(cfg);
      }

      setPanel(&_panel_instance);
    }
  };

LGFX display;

void app_main() {
    printf("Testing an ILI8341 using Lovyan!\n");
    display.init();
    display.fillScreen(TFT_WHITE);
    display.setTextSize((std::max(display.width(), display.height()) + 255) >> 8);
    
    display.startWrite();
    display.setTextColor(0xFF0000U);
    display.drawString("R", 30, 16);
    display.setTextColor(0x00FF00U);
    display.drawString("G", 40, 16);
    display.setTextColor(0x0000FFU);
    display.drawString("B", 50, 16);
    display.endWrite();
    printf("Try to draw a silly RGB string\n");
}
