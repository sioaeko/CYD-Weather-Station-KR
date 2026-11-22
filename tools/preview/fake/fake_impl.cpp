// PC 미리보기용 가짜 하드웨어 구현
#include "TFT_eSPI.h"
#include "TJpg_Decoder.h"
#include "WiFi.h"
#include <algorithm>
#include <filesystem>

FakeSerial Serial; FakeEsp ESP; FakeJpg TJpgDec; FakeWiFi WiFi; fs::FS LittleFS;
std::string g_dataRoot;
std::set<uint16_t> TFT_eSPI::missingGlyphs;

// ── LittleFS ──
namespace fs {
static std::string full(const String& p) { std::string s = p.s; if (s.empty() || s[0] != '/') s = "/" + s; return g_dataRoot + s; }
File FS::open(const String& path, const char*) {
  File f; const std::string p = full(path); f.path = p;
  f.base = std::filesystem::path(p).filename().string();
  if (std::filesystem::is_directory(p)) {
    f.dir = true;
    for (auto& e : std::filesystem::directory_iterator(p)) f.entries.push_back(e.path().string());
    std::sort(f.entries.begin(), f.entries.end());
  } else {
    f.f = fopen(p.c_str(), "rb");
  }
  return f;
}
bool FS::exists(const String& path) { return std::filesystem::exists(full(path)); }
File File::openNextFile() {
  File f; if (next >= entries.size()) return f;
  const std::string p = entries[next++]; f.path = p; f.base = std::filesystem::path(p).filename().string();
  if (std::filesystem::is_directory(p)) f.dir = true; else f.f = fopen(p.c_str(), "rb");
  return f;
}
}

// ── 그리기 ──
void TFT_eSPI::pushImage(int x, int y, int iw, int ih, const uint16_t* p) {
  for (int j = 0; j < ih; ++j) for (int i = 0; i < iw; ++i) drawPixel(x + i, y + j, p[j * iw + i]);
}
void TFT_eSPI::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    drawPixel(x0, y0, c); if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void TFT_eSPI::drawCircle(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    const int pts[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
    for (auto& p : pts) drawPixel(cx + p[0], cy + p[1], c);
    ++y; if (err < 0) err += 2 * y + 1; else { --x; err += 2 * (y - x) + 1; }
  }
}
void TFT_eSPI::fillCircle(int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; ++y) { const int x = static_cast<int>(sqrt(static_cast<double>(r * r - y * y)) + 0.5); drawFastHLine(cx - x, cy + y, 2 * x + 1, c); }
}
void TFT_eSPI::fillRoundRect(int x, int y, int rw, int rh, int r, uint16_t c) {
  fillRect(x + r, y, rw - 2 * r, rh, c);
  for (int j = 0; j < rh; ++j) {
    int inset = 0;
    if (j < r) inset = r - static_cast<int>(sqrt(static_cast<double>(r * r - (r - j) * (r - j))) + 0.5);
    if (j >= rh - r) { const int k = j - (rh - r - 1); inset = r - static_cast<int>(sqrt(static_cast<double>(r * r - k * k)) + 0.5); }
    drawFastHLine(x + inset, y + j, r - inset, c); drawFastHLine(x + rw - r, y + j, r - inset, c);
  }
}
void TFT_eSPI::drawRoundRect(int x, int y, int rw, int rh, int r, uint16_t c) {
  drawFastHLine(x + r, y, rw - 2 * r, c); drawFastHLine(x + r, y + rh - 1, rw - 2 * r, c);
  drawFastVLine(x, y + r, rh - 2 * r, c); drawFastVLine(x + rw - 1, y + r, rh - 2 * r, c);
  for (int a = 0; a <= 90; ++a) {
    const double t = a * 3.14159265 / 180; const int dx = static_cast<int>(r - r * cos(t) + 0.5), dy = static_cast<int>(r - r * sin(t) + 0.5);
    drawPixel(x + dx, y + dy, c); drawPixel(x + rw - 1 - dx, y + dy, c);
    drawPixel(x + dx, y + rh - 1 - dy, c); drawPixel(x + rw - 1 - dx, y + rh - 1 - dy, c);
  }
}
void TFT_eSPI::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
  const int minY = std::min({y0, y1, y2}), maxY = std::max({y0, y1, y2});
  for (int y = minY; y <= maxY; ++y) {
    double xs[3]; int n = 0; const int P[3][4] = {{x0, y0, x1, y1}, {x1, y1, x2, y2}, {x2, y2, x0, y0}};
    for (auto& e : P) {
      if ((y >= e[1] && y <= e[3]) || (y >= e[3] && y <= e[1])) {
        if (e[1] == e[3]) { xs[n++] = e[0]; if (n < 3) xs[n++] = e[2]; }
        else xs[n++] = e[0] + (double)(y - e[1]) * (e[2] - e[0]) / (e[3] - e[1]);
        if (n >= 3) break;
      }
    }
    if (!n) continue;
    double lo = xs[0], hi = xs[0]; for (int i = 1; i < n; ++i) { lo = std::min(lo, xs[i]); hi = std::max(hi, xs[i]); }
    drawFastHLine(static_cast<int>(lo + 0.5), y, static_cast<int>(hi + 0.5) - static_cast<int>(lo + 0.5) + 1, c);
  }
}

// ── VLW 스무스 글꼴 ──
static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
bool TFT_eSPI::parse(const uint8_t* p) {
  font.clear(); const int count = be32(p); ascent = be32(p + 16); descent = be32(p + 20);
  const uint8_t* bmp = p + 24 + count * 28; int maxDescent = descent;
  for (int i = 0; i < count; ++i) {
    const uint8_t* m = p + 24 + i * 28;
    Glyph g{static_cast<uint16_t>(be32(m)), (int)be32(m + 4), (int)be32(m + 8), (int)be32(m + 12), (int)(int32_t)be32(m + 16), (int)(int8_t)be32(m + 20), bmp};
    bmp += g.w * g.h; font.push_back(g);
    if (g.h - g.dy > maxDescent) maxDescent = g.h - g.dy;
  }
  descent = maxDescent; loaded = true; return true;
}
bool TFT_eSPI::loadFont(const uint8_t* array) { return parse(array); }
void TFT_eSPI::loadFont(const String& name, fs::FS&) {
  const std::string p = g_dataRoot + "/" + name.s + ".vlw"; FILE* f = fopen(p.c_str(), "rb");
  if (!f) { fprintf(stderr, "font missing %s\n", p.c_str()); return; }
  fseek(f, 0, SEEK_END); fileFont.resize(ftell(f)); fseek(f, 0, SEEK_SET);
  fread(fileFont.data(), 1, fileFont.size(), f); fclose(f); parse(fileFont.data());
}
const TFT_eSPI::Glyph* TFT_eSPI::find(uint16_t code) const {
  for (auto& g : font) if (g.code == code) return &g;
  return nullptr;
}
static std::vector<uint16_t> decode(const String& s) {
  std::vector<uint16_t> out; const unsigned char* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (*p) {
    if (*p < 0x80) out.push_back(*p++);
    else if ((*p & 0xE0) == 0xC0) { out.push_back(((p[0] & 0x1F) << 6) | (p[1] & 0x3F)); p += 2; }
    else { out.push_back(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F)); p += 3; }
  }
  return out;
}
int TFT_eSPI::textWidth(const String& s) {
  if (!loaded) return static_cast<int>(s.length()) * 6;
  int width = 0; const int space = (ascent + descent) * 2 / 7;
  for (uint16_t c : decode(s)) { if (c == ' ') width += space; else if (auto* g = find(c)) width += g->adv; else width += space; }
  return width;
}
static uint16_t blend(uint8_t a, uint16_t fg, uint16_t bg) {
  auto ch = [&](int sh, int mask) { const int f = (fg >> sh) & mask, b = (bg >> sh) & mask; return ((f * a + b * (255 - a)) / 255) & mask; };
  return (ch(11, 31) << 11) | (ch(5, 63) << 5) | ch(0, 31);
}
void TFT_eSPI::drawString(const String& s, int x, int y) {
  if (!loaded) return;
  if (datum == MC_DATUM) { x -= textWidth(s) / 2; y -= (ascent + descent) / 2; }
  const int space = (ascent + descent) * 2 / 7;
  for (uint16_t c : decode(s)) {
    if (c == ' ') { x += space; continue; }
    const Glyph* g = find(c);
    if (!g) { missingGlyphs.insert(c); drawRoundRect(x, y + 2, space, ascent - 2, 0, textFg); x += space + 1; continue; }
    for (int j = 0; j < g->h; ++j) for (int i = 0; i < g->w; ++i) {
      const uint8_t a = g->bmp[j * g->w + i]; if (!a) continue;
      const int px = x + g->dx + i, py = y + ascent - g->dy + j;
      drawPixel(px, py, a == 255 ? textFg : blend(a, textFg, readPixel(px, py)));
    }
    x += g->adv;
  }
}

// ── 안티앨리어싱 도형 (TFT_eSPI 의 fillSmooth* 흉내: 4x4 표본으로 덮인 비율만큼 섞음) ──
static uint16_t mix(uint8_t a, uint16_t fg, uint16_t bg) {
  auto ch = [&](int sh, int mask) { const int f = (fg >> sh) & mask, b = (bg >> sh) & mask; return ((f * a + b * (255 - a)) / 255) & mask; };
  return (ch(11, 31) << 11) | (ch(5, 63) << 5) | ch(0, 31);
}
void TFT_eSPI::blendPixel(int x, int y, uint16_t c, uint8_t a) {
  if (a >= 250) drawPixel(x, y, c); else if (a > 8) drawPixel(x, y, mix(a, c, readPixel(x, y)));
}
template <class Inside> static void coverage(TFT_eSPI& t, int x0, int y0, int x1, int y1, uint16_t c, Inside inside) {
  for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
    int n = 0;
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) n += inside(x + (i + 0.5) / 4.0, y + (j + 0.5) / 4.0);
    if (n) t.blendPixel(x, y, c, static_cast<uint8_t>(n * 255 / 16));
  }
}
void TFT_eSPI::fillSmoothCircle(int cx, int cy, int r, uint16_t c, uint32_t) {
  const double ccx = cx + 0.5, ccy = cy + 0.5, rr = (r + 0.5) * (r + 0.5);
  coverage(*this, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, c,
           [&](double px, double py) { return (px - ccx) * (px - ccx) + (py - ccy) * (py - ccy) <= rr; });
}
void TFT_eSPI::fillSmoothRoundRect(int x, int y, int rw, int rh, int r, uint16_t c, uint32_t) {
  if (r > rw / 2) r = rw / 2; if (r > rh / 2) r = rh / 2;
  coverage(*this, x, y, x + rw - 1, y + rh - 1, c, [&](double px, double py) {
    const double qx = px < x + r ? x + r - px : px > x + rw - r ? px - (x + rw - r) : 0;
    const double qy = py < y + r ? y + r - py : py > y + rh - r ? py - (y + rh - r) : 0;
    return qx * qx + qy * qy <= double(r) * r;
  });
}
void TFT_eSPI::drawWideLine(float ax, float ay, float bx, float by, float wd, uint16_t c, uint32_t bg) {
  drawWedgeLine(ax, ay, bx, by, wd / 2, wd / 2, c, bg);
}
void TFT_eSPI::drawWedgeLine(float ax, float ay, float bx, float by, float ar, float br, uint16_t c, uint32_t) {
  // TFT_eSPI 와 같이 픽셀 중심 좌표 기준. 선분 위 가장 가까운 점에서 반지름을 선형 보간합니다.
  const double rmax = std::max(ar, br), dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
  const int x0 = static_cast<int>(floor(std::min(ax, bx) - rmax - 1)), x1 = static_cast<int>(ceil(std::max(ax, bx) + rmax + 1));
  const int y0 = static_cast<int>(floor(std::min(ay, by) - rmax - 1)), y1 = static_cast<int>(ceil(std::max(ay, by) + rmax + 1));
  coverage(*this, x0, y0, x1, y1, c, [&](double px, double py) {
    px -= 0.5; py -= 0.5;
    double t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double qx = ax + t * dx - px, qy = ay + t * dy - py, r = ar + t * (br - ar);
    return qx * qx + qy * qy <= r * r;
  });
}
