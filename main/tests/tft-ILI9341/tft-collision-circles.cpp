/**
 * @file tft-collision-circles.cpp
 * @author lovyan03
 * @brief  https://github.com/lovyan03/LovyanGFX/blob/master/examples/Sprite/CollisionCircles/CollisionCircles.ino
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TFT_BACKLIGHT_PIN 5

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
        cfg.freq_write = 20000000;    // 送信クロック (5 MHz 最大20MHz, 80MHzを整数で割った値に丸められます)
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
      auto cfg = _light_instance.config();

      cfg.pin_bl = TFT_BACKLIGHT_PIN;
      cfg.invert = false;
      cfg.freq   = 20000; //40000
      cfg.pwm_channel = 1;

      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
      }

      // TFT config CS, RST & dimensions
      {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           =    10;  // Chip Select
      cfg.pin_rst          =    12;  // RST  (-1 = disable)
      cfg.pin_busy         =    -1;  // BUSY (-1 = disable)

      cfg.panel_width      =   320;  // 240
      cfg.panel_height     =   240;  // 320 trimmed screen
      cfg.offset_x         =     0;  // パネルのX方向オフセット量
      cfg.offset_y         =     80;  // パネルのY方向オフセット量
      cfg.offset_rotation  =     0;  // 回転方向の値のオフセット 0~7 (4~7は上下反転)
      cfg.dummy_read_pixel =     8;  // ピクセル読出し前のダミーリードのビット数
      cfg.dummy_read_bits  =     1;  // ピクセル以外のデータ読出し前のダミーリードのビット数
      cfg.readable         = true;  // データ読出しが可能な場合 trueに設定
      cfg.invert           = false;  // パネルの明暗が反転してしまう場合 trueに設定
      cfg.rgb_order        = true;  // パネルの赤と青が入れ替わってしまう場合 trueに設定
      cfg.dlen_16bit       = false;  // 16bitパラレルやSPIでデータ長を16bit単位で送信するパネルの場合 trueに設定
      cfg.bus_shared       = false;  // SDカードとバスを共有している場合 trueに設定(drawJpgFile等でバス制御を行います)
      _panel_instance.config(cfg);
      }

      setPanel(&_panel_instance);
    }
  };


static LGFX lcd;
static LGFX_Sprite _sprites[2];

struct ball_info_t {
  int32_t x;
  int32_t y;
  int32_t dx;
  int32_t dy;
  int32_t r;
  int32_t m;
  uint32_t color;
};

static constexpr std::uint32_t SHIFTSIZE = 8;
static constexpr std::uint32_t BALL_MAX = 256;

static ball_info_t _balls[2][BALL_MAX];
static std::uint32_t _ball_count = 0, _fps = 0;
static std::uint32_t ball_count = 0;
static std::uint32_t sec, psec;
static std::uint32_t fps = 0, frame_count = 0;

static std::uint32_t _width;
static std::uint32_t _height;

volatile bool _is_running;
volatile std::uint32_t _draw_count;
volatile std::uint32_t _loop_count;

static void diffDraw(LGFX_Sprite* sp0, LGFX_Sprite* sp1)
{
  union
  {
    std::uint32_t* s32;
    std::uint8_t* s;
  };
  union
  {
    std::uint32_t* p32;
    std::uint8_t* p;
  };
  s32 = (std::uint32_t*)sp0->getBuffer();
  p32 = (std::uint32_t*)sp1->getBuffer();

  auto width  = sp0->width();
  auto height = sp0->height();

  auto w32 = (width+3) >> 2;
  std::int32_t y = 0;
  do
  {
    std::int32_t x32 = 0;
    do
    {
      while (s32[x32] == p32[x32] && ++x32 < w32);
      if (x32 == w32) break;

      std::int32_t xs = x32 << 2;
      while (s[xs] == p[xs]) ++xs;

      while (++x32 < w32 && s32[x32] != p32[x32]);

      std::int32_t xe = (x32 << 2) - 1;
      if (xe >= width) xe = width - 1;
      while (s[xe] == p[xe]) --xe;

      lcd.pushImage(xs, y, xe - xs + 1, 1, &s[xs]);
    } while (x32 < w32);
    s32 += w32;
    p32 += w32;
  } while (++y < height);
  lcd.display();
}

static void drawfunc(void)
{
  ball_info_t *balls;
  ball_info_t *a;
  LGFX_Sprite *sprite;

  auto width  = _sprites[0].width();
  auto height = _sprites[0].height();

  std::size_t flip = _draw_count & 1;
  balls = &_balls[flip][0];

  sprite = &(_sprites[flip]);
  sprite->clear();

  for (int32_t i = 8; i < width; i += 16) {
    sprite->drawFastVLine(i, 0, height, 0x1F);
  }
  for (int32_t i = 8; i < height; i += 16) {
    sprite->drawFastHLine(0, i, width, 0x1F);
  }
  for (std::uint32_t i = 0; i < _ball_count; i++) {
    a = &balls[i];
    sprite->fillCircle( a->x >> SHIFTSIZE
                      , a->y >> SHIFTSIZE
                      , a->r >> SHIFTSIZE
                      , a->color);
  }

  sprite->setCursor(1,1);
  sprite->setTextColor(TFT_BLACK);
  sprite->printf("obj:%d fps:%d", _ball_count, _fps);
  sprite->setCursor(0,0);
  sprite->setTextColor(TFT_WHITE);
  sprite->printf("obj:%d fps:%d", _ball_count, _fps);
  diffDraw(&_sprites[flip], &_sprites[!flip]);
  ++_draw_count;
}

static void mainfunc(void)
{
  static constexpr float e = 0.999; // Coefficient of friction

  sec = lgfx::millis() / 1000;
  if (psec != sec) {
    psec = sec;
    fps = frame_count;
    frame_count = 0;

    if (++ball_count >= BALL_MAX) { ball_count = 1; }
    auto a = &_balls[_loop_count & 1][ball_count - 1];
    a->color = lgfx::color888(100+(rand()%155), 100+(rand()%155), 100+(rand()%155));
    a->x = 0;
    a->y = 0;
    a->dx = (rand() & (3 << SHIFTSIZE)) + 1;
    a->dy = (rand() & (3 << SHIFTSIZE)) + 1;
    a->r = (4 + (ball_count & 0x07)) << SHIFTSIZE;
    a->m =  4 + (ball_count & 0x07);
    vTaskDelay(1);
  }

  frame_count++;
  _loop_count++;

  ball_info_t *a, *b, *balls;
  int32_t rr, len, vx2vy2;
  float vx, vy, distance, t;

  size_t f = _loop_count & 1;
  balls = a = &_balls[f][0];
  b = &_balls[!f][0];
  memcpy(a, b, sizeof(ball_info_t) * ball_count);

  for (int i = 0; i != ball_count; i++) {
    a = &balls[i];
    a->dy += 2; // gravity

    a->x += a->dx;
    if (a->x < a->r) {
      a->x = a->r;
      if (a->dx < 0) a->dx = - a->dx*e;
    } else if (a->x >= _width - a->r) {
      a->x = _width - a->r -1;
      if (a->dx > 0) a->dx = - a->dx*e;
    }
    a->y += a->dy;
    if (a->y < a->r) {
      a->y = a->r;
      if (a->dy < 0) a->dy = - a->dy*e;
    } else if (a->y >= _height - a->r) {
      a->y = _height - a->r -1;
      if (a->dy > 0) a->dy = - a->dy*e;
    }
    for (int j = i + 1; j != ball_count; j++) {
      b = &balls[j];

      rr = a->r + b->r;
      vx = a->x - b->x;
      if (abs(vx) > rr) continue;
      vy = a->y - b->y;
      if (abs(vy) > rr) continue;

      len = sqrt(vx * vx + vy * vy);
      if (len >= rr) continue;
      if (len == 0.0) continue;
      distance = (rr - len) >> 1;
      vx *= distance / len;
      vy *= distance / len;

      a->x += vx;
      b->x -= vx;
      vx = b->x - a->x;

      a->y += vy;
      b->y -= vy;
      vy = b->y - a->y;

      vx2vy2 = vx * vx + vy * vy;

      t = -(vx * a->dx + vy * a->dy) / vx2vy2;
      float arx = a->dx + vx * t;
      float ary = a->dy + vy * t;

      t = -(-vy * a->dx + vx * a->dy) / vx2vy2;
      float amx = a->dx - vy * t;
      float amy = a->dy + vx * t;

      t = -(vx * b->dx + vy * b->dy) / vx2vy2;
      float brx = b->dx + vx * t;
      float bry = b->dy + vy * t;

      t = -(-vy * b->dx + vx * b->dy) / vx2vy2;
      float bmx = b->dx - vy * t;
      float bmy = b->dy + vx * t;

      float adx = (a->m * amx + b->m * bmx + bmx * e * b->m - amx * e * b->m) / (a->m + b->m);
      float bdx = - e * (bmx - amx) + adx;
      float ady = (a->m * amy + b->m * bmy + bmy * e * b->m - amy * e * b->m) / (a->m + b->m);
      float bdy = - e * (bmy - amy) + ady;

      a->dx = roundf(adx + arx);
      a->dy = roundf(ady + ary);
      b->dx = roundf(bdx + brx);
      b->dy = roundf(bdy + bry);
    }
  }

  _fps = fps;
  _ball_count = ball_count;
}

static void taskDraw(void*)
{
  while ( _is_running )
  {
    while (_loop_count == _draw_count) { taskYIELD(); }
    drawfunc();
  }
  vTaskDelete(NULL);
}

void app_main(void)
{
  lcd.begin();
  lcd.setRotation(4);
/*   lcd.startWrite();
lcd.writeCommand(0xF6);  //MADCTL
lcd.writeData(0x80);    // 
lcd.endWrite(); */


  lcd.startWrite();
  lcd.setColorDepth(8);
  if (lcd.width() < lcd.height()) lcd.setRotation(lcd.getRotation() ^ 1);

  auto lcd_width = lcd.width();
  auto lcd_height = lcd.height();

  for (std::uint32_t i = 0; i < 2; ++i)
  {
    _sprites[i].setTextSize(2);
    _sprites[i].setColorDepth(8);
  }

  bool fail = false;
  for (std::uint32_t i = 0; !fail && i < 2; ++i)
  {
    fail = !_sprites[i].createSprite(lcd_width, lcd_height);
  }

  if (fail)
  {
    fail = false;
    for (std::uint32_t i = 0; !fail && i < 2; ++i)
    {
      _sprites[i].setPsram(true);
      fail = !_sprites[i].createSprite(lcd_width, lcd_height);
    }

    if (fail)
    {
      fail = false;
      if (lcd_width > 320) lcd_width = 320;
      if (lcd_height > 240) lcd_height = 240;

      for (std::uint32_t i = 0; !fail && i < 2; ++i)
      {
        _sprites[i].setPsram(true);
        fail = !_sprites[i].createSprite(lcd_width, lcd_height);
      }
      if (fail)
      {
        lcd.print("createSprite fail...");
        lgfx::delay(3000);
      }
    }
  }

  _width = lcd_width << SHIFTSIZE;
  _height = lcd_height << SHIFTSIZE;

  for (std::uint32_t i = 0; i < ball_count; ++i)
  {
    auto a = &_balls[_loop_count & 1][i];
    a->color = lgfx::color888(100+(rand()%155), 100+(rand()%155), 100+(rand()%155));
    a->x = 0;
    a->y = 0;
    a->dx = (rand() & (3 << SHIFTSIZE)) + 1;
    a->dy = (rand() & (3 << SHIFTSIZE)) + 1;
    a->r = (4 + (i & 0x07)) << SHIFTSIZE;
    a->m =  4 + (i & 0x07);
  }

  _is_running = true;
  _draw_count = 0;
  _loop_count = 0;

  // Using tasks is slower:
  //xTaskCreate(taskDraw, "taskDraw", 2048, NULL, 0, NULL);

  while(1) {
    mainfunc();
    //while (_loop_count != _draw_count) { taskYIELD(); }
    drawfunc();
  }
}

