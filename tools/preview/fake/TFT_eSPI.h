#pragma once
// PC 미리보기용 TFT_eSPI 대체: 8비트(RGB332) 스프라이트 + VLW 스무스 글꼴 렌더링.
#include "Arduino.h"
#include "LittleFS.h"
#include <vector>
#include <set>
#define USER_SETUP_ID 801
#define SMOOTH_FONT
#define TFT_BL 21
constexpr uint16_t TFT_BLACK = 0x0000, TFT_WHITE = 0xFFFF, TFT_RED = 0xF800, TFT_CYAN = 0x07FF;
enum { TL_DATUM = 0, MC_DATUM = 4 };

class TFT_eSPI {
 public:
  int w = 240, h = 320;
  std::vector<uint16_t> fb;  // RGB565 (실제 화면)
  void begin() { fb.assign(w * h, 0); }
  void invertDisplay(bool) {}
  void setRotation(uint8_t r) { w = r % 2 ? 320 : 240; h = r % 2 ? 240 : 320; fb.assign(w * h, 0); }
  int width() const { return w; }
  int height() const { return h; }
  void fillScreen(uint16_t c) { fb.assign(w * h, c); }
  void pushImage(int x, int y, int iw, int ih, const uint16_t* p);
  // 글꼴
  bool loadFont(const uint8_t* array);
  void loadFont(const String& name, fs::FS&);
  void unloadFont() { font.clear(); loaded = false; }
  void setTextSize(int) {}
  void setTextDatum(int d) { datum = d; }
  void setTextPadding(int) {}
  void setTextColor(uint16_t fg, uint16_t bg) { textFg = fg; textBg = bg; }
  int textWidth(const String& s);
  int fontHeight() const { return loaded ? ascent + descent : 8; }
  void drawString(const String& s, int x, int y);
  // 도형
  uint16_t drawPixel(int x, int y, uint16_t c, uint8_t alpha, uint32_t) { blendPixel(x, y, c, alpha); return c; }
  virtual void drawPixel(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < w && y < h) fb[y * w + x] = c; }
  virtual uint16_t readPixel(int x, int y) { return x >= 0 && y >= 0 && x < w && y < h ? fb[y * w + x] : 0; }
  void drawFastHLine(int x, int y, int l, uint16_t c) { for (int i = 0; i < l; ++i) drawPixel(x + i, y, c); }
  void drawFastVLine(int x, int y, int l, uint16_t c) { for (int i = 0; i < l; ++i) drawPixel(x, y + i, c); }
  void fillRect(int x, int y, int rw, int rh, uint16_t c) { for (int j = 0; j < rh; ++j) drawFastHLine(x, y + j, rw, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c);
  void drawCircle(int cx, int cy, int r, uint16_t c);
  void fillCircle(int cx, int cy, int r, uint16_t c);
  void drawRoundRect(int x, int y, int rw, int rh, int r, uint16_t c);
  void fillRoundRect(int x, int y, int rw, int rh, int r, uint16_t c);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);
  void fillSmoothRoundRect(int x, int y, int rw, int rh, int r, uint16_t c, uint32_t bg = 0x00FFFFFF);
  void fillSmoothCircle(int cx, int cy, int r, uint16_t c, uint32_t bg = 0x00FFFFFF);
  void blendPixel(int x, int y, uint16_t c, uint8_t alpha);
  void drawWideLine(float ax, float ay, float bx, float by, float wd, uint16_t c, uint32_t bg = 0x00FFFFFF);
  void drawWedgeLine(float ax, float ay, float bx, float by, float ar, float br, uint16_t c, uint32_t bg = 0x00FFFFFF);
  static std::set<uint16_t> missingGlyphs;

 protected:
  struct Glyph { uint16_t code; int h, w, adv, dy, dx; const uint8_t* bmp; };
  std::vector<Glyph> font; std::vector<uint8_t> fileFont;
  bool loaded = false; int ascent = 0, descent = 0, datum = 0;
  uint16_t textFg = 0xFFFF, textBg = 0;
  bool parse(const uint8_t* p);
  const Glyph* find(uint16_t code) const;
};

class TFT_eSprite : public TFT_eSPI {
 public:
  explicit TFT_eSprite(TFT_eSPI* parent) : parent(parent) {}
  void setColorDepth(int) {}
  void* createSprite(int sw, int sh) { w = sw; h = sh; px.assign(sw * sh, 0); return px.data(); }
  void deleteSprite() { px.clear(); }
  bool created() const { return !px.empty(); }
  // 뷰포트 기준점: TFT_eSPI 와 같이 그리기 좌표에 (dx, dy) 를 더해 버퍼에 그림
  void setViewport(int x, int y, int, int, bool datum = true) { dx = datum ? x : 0; dy = datum ? y : 0; }
  void resetViewport() { dx = dy = 0; }
  void fillSprite(uint16_t c) { std::fill(px.begin(), px.end(), to332(c)); }
  void pushSprite(int ox, int oy) {
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
      const int X = ox + x, Y = oy + y;
      if (X >= 0 && Y >= 0 && X < parent->w && Y < parent->h) parent->fb[Y * parent->w + X] = to565(px[y * w + x]);
    }
  }
  void drawPixel(int x, int y, uint16_t c) override { x += dx; y += dy; if (x >= 0 && y >= 0 && x < w && y < h) px[y * w + x] = to332(c); }
  uint16_t readPixel(int x, int y) override { x += dx; y += dy; return x >= 0 && y >= 0 && x < w && y < h ? to565(px[y * w + x]) : 0; }
  static uint8_t to332(uint16_t c) { return ((c & 0xE000) >> 8) | ((c & 0x0700) >> 6) | ((c & 0x0018) >> 3); }
  static uint16_t to565(uint8_t c) {
    // TFT_eSPI 와 같은 방식으로 3-3-2 비트를 5-6-5 로 늘림
    const uint16_t r = (c & 0xE0) >> 5, g = (c & 0x1C) >> 2, b = c & 0x03;
    return ((r * 31 / 7) << 11) | ((g * 63 / 7) << 5) | (b * 31 / 3);
  }

 private:
  TFT_eSPI* parent; std::vector<uint8_t> px; int dx = 0, dy = 0;
};
