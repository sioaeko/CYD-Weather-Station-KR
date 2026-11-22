/* 세로 240x320 · 가로 320x240 화면 레이아웃 (한국어판).
 * 원작: Daniel Eichhorn / ThingPulse 날씨 스테이션, Bodmer 의 TFT_eSPI·LittleFS 이식,
 * Rau7han 의 ESP32-CYD-Weather-Station. MIT 고지는 THIRD_PARTY_NOTICES.txt 에 있습니다.
 *
 * 화면 4개: 0 홈(대시보드) · 1 4일 예보 · 2 상세 · 3 시스템. 좌우로 밀거나 탭해서 넘깁니다.
 * 세로(240x320)와 가로(320x240) 레이아웃을 모두 갖고 있고, 실행 중에 바꿀 수 있습니다.
 *
 * 글꼴 (한글 글꼴은 플래시 배열이라 자주 바꿔도 빠릅니다):
 *   S  나눔고딕 Bold 12px  – 라벨·본문
 *   M  나눔고딕 Bold 16px  – 값
 *   G  나눔고딕 Bold 22px  – 날씨 설명·날짜 (필요한 글자만)
 *   C  나눔고딕 Bold 46px  – 세로 화면 시계 숫자만
 *   CB 나눔고딕 Bold 66px  – 가로 화면 시계 숫자만 (머리맡에서도 잘 보이게)
 *   L  NSBold36.vlw (LittleFS) – 상세 화면의 큰 기온 (숫자와 ° 만) */
#include "DisplayUI.h"
#include "All_Settings.h"
#include "NTP_Time.h"
#include "WeatherNetwork.h"
#include "KoreanFonts.h"
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <math.h>

TFT_eSPI tft;
namespace {
TFT_eSprite screen(&tft);
enum FontId { FONT_S = 1, FONT_M = 2, FONT_L = 3, FONT_G = 4, FONT_C = 5, FONT_CB = 6 };
int W = 240, H = 320;  // 현재 방향의 화면 크기 (setDisplayLandscape 에서 바뀜)
constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t WHITE = 0xF7BF, MUTED = 0xADFA, DIM = 0x7BEF, LINE = 0x2124;
constexpr uint16_t CLOCK = 0xDF94, PEACH = 0xFD8E, SKY = 0x969F, LILAC = 0xE55F, LIME = 0xAF0E;
constexpr uint16_t YELLOW = 0xF727, CYAN = 0x667F, GREEN = 0x47B0, ORANGE = 0xFD20, RED = 0xFA08;
constexpr uint16_t BLUE = 0x34DF, PURPLE = 0xA19F;
int selectedFont = 0, fileCount = 0, badFiles = 0, clippingWarnings = 0;

void font(int id) {
  if (selectedFont == id) return;
  screen.unloadFont(); screen.setTextSize(1);
  if (id == FONT_S) screen.loadFont(KR_FONT_SMALL);
  else if (id == FONT_M) screen.loadFont(KR_FONT_MEDIUM);
  else if (id == FONT_G) screen.loadFont(KR_FONT_LARGE);
  else if (id == FONT_C) screen.loadFont(KR_FONT_CLOCK);
  else if (id == FONT_CB) screen.loadFont(KR_FONT_CLOCK_BIG);
  else if (id == FONT_L) screen.loadFont("fonts/NSBold36", LittleFS);
  selectedFont = id;
}
int textWidth(const String& value, int fontId) { font(fontId); return screen.textWidth(value); }
// align: 0 왼쪽, 1 가운데, 2 오른쪽. 폭을 넘으면 말줄임표로 자릅니다. 끝난 x 좌표를 돌려줍니다.
int text(String value, int x, int y, int width, uint16_t color = WHITE, int fontId = FONT_S, int align = 0) {
  font(fontId);
  if (x < 0 || x + width > W || y < 0 || y + screen.fontHeight() > H) ++clippingWarnings;
  if (screen.textWidth(value) > width) {
    while (value.length() && screen.textWidth(value + "…") > width) {
      int cut = value.length() - 1;  // UTF-8 한 글자(1~3바이트)를 통째로 지웁니다.
      while (cut > 0 && (static_cast<uint8_t>(value[cut]) & 0xC0) == 0x80) --cut;
      value.remove(cut);
    }
    value += "…";
  }
  const int w = screen.textWidth(value);
  int start = x;
  if (align == 1) start += (width - w) / 2;
  if (align == 2) start += width - w;
  screen.setTextDatum(TL_DATUM); screen.setTextPadding(0); screen.setTextColor(color, BG);
  screen.drawString(value, start, y);
  return start + w;
}
void divider(int y) { screen.drawFastHLine(8, y, W - 16, LINE); }
String number(float n, int precision = 0) {
  if (precision == 0 && n > -0.5f && n < 0) n = 0;  // "-0" 표시 방지
  return String(n, precision);
}
String celsius(float n, int precision = 0) { return number(n, precision) + "°"; }
uint16_t dustColor(int grade) { const uint16_t c[] = {BLUE, GREEN, ORANGE, RED}; return grade >= 0 && grade < 4 ? c[grade] : MUTED; }
uint16_t uvColor(int grade) { const uint16_t c[] = {GREEN, YELLOW, ORANGE, RED, PURPLE}; return grade >= 0 && grade < 5 ? c[grade] : MUTED; }
uint16_t adviceColor(AdviceLevel level) { return level == ADVICE_WARNING ? ORANGE : level == ADVICE_CAUTION ? YELLOW : LIME; }
AdviceThresholds thresholds() {
  AdviceThresholds t; t.bigDailyRange = BIG_DAILY_RANGE_C; t.heatFeelsLike = HEAT_FEELS_LIKE_C;
  t.coldTemperature = COLD_TEMPERATURE_C; t.umbrellaPop = UMBRELLA_POP; return t;
}
const Place& place(const WeatherState& s) { return s.place; }
String koreanDate(time_t utc, bool weekday = true) {
  if (utc <= 0) return "--";
  struct tm t; localtime_r(&utc, &t); char out[32];
  if (weekday) snprintf(out, sizeof(out), "%d월 %d일 (%s)", t.tm_mon + 1, t.tm_mday, koreanWeekday(t.tm_wday));
  else snprintf(out, sizeof(out), "%d월 %d일", t.tm_mon + 1, t.tm_mday);
  return String(out);
}
time_t dayNoonUtc(int32_t localDay) { return static_cast<time_t>(localDay) * 86400 + 43200 - LOCAL_UTC_OFFSET_SECONDS; }
bool stale(const WeatherState& s) { return s.current.valid && (time(nullptr) - s.current.fetched > 1800 || s.currentHttp != 200); }
int worseDustGrade(const WeatherState& s) { return pm25Grade(s.pm25) > pm10Grade(s.pm10) ? pm25Grade(s.pm25) : pm10Grade(s.pm10); }

// ───────────────────────── 비트맵 (LittleFS 24비트 BMP) ─────────────────────────
uint16_t read16(File& f) { uint8_t b[2]; if (f.read(b, 2) != 2) return 0; return b[0] | b[1] << 8; }
uint32_t read32(File& f) { uint8_t b[4]; if (f.read(b, 4) != 4) return 0; return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24; }
bool bitmap(const String& path, int x, int y, int size) {
  if (x < 0 || y < 0 || x + size > W || y + size > H) { ++clippingWarnings; return false; }
  File file = LittleFS.open(path, "r");
  if (!file || read16(file) != 0x4d42) return false;
  read32(file); read32(file); const uint32_t offset = read32(file); read32(file);
  const int32_t width = read32(file), height = read32(file);
  if (read16(file) != 1 || read16(file) != 24 || read32(file) != 0 || width <= 0 || height <= 0 || width > 120 || height > 120) return false;
  const int stride = (width * 3 + 3) & ~3;
  uint8_t row[360]; int previousRow = -1;
  for (int dy = 0; dy < size; ++dy) {
    const int sourceY = dy * height / size;
    if (sourceY != previousRow) {
      file.seek(offset + (height - 1 - sourceY) * stride);
      if (file.read(row, stride) != stride) return false;
      previousRow = sourceY;
    }
    for (int dx = 0; dx < size; ++dx) {
      const uint8_t* p = row + (dx * width / size) * 3;
      const uint16_t color = (p[2] & 0xF8) << 8 | (p[1] & 0xFC) << 3 | p[0] >> 3;
      if (color) screen.drawPixel(x + dx, y + dy, color);
    }
  }
  return true;
}
void weatherIcon(const WeatherPoint& point, int x, int y, int size) {
  // 100px 원본(/icon)과 50px 원본(/icon50) 중 크기에 맞는 쪽을 써서 선명하게 그립니다.
  const String path = String(size > 50 ? "/icon/" : "/icon50/") + weatherIconName(point.conditionId, point.icon) + ".bmp";
  if (!bitmap(path, x, y, size)) bitmap("/icon50/unknown.bmp", x, y, size);
}

// ───────────────────────── 코드로 그리는 작은 아이콘 (32x32 칸) ─────────────────────────
// 온도계: 유리관 + 수은주 + 하이라이트 + 눈금 (안티앨리어싱 도형).
// 수은주 높이와 색이 현재 기온을 따라갑니다: -15℃ 바닥 ~ 35℃ 꼭대기, 추우면 파랑 · 더우면 빨강.
uint16_t mercuryColor(float t) { return t <= 0 ? BLUE : t <= 12 ? CYAN : t <= 24 ? ORANGE : RED; }
void iconThermometer(int x, int y, float temperature, bool valid) {
  constexpr uint16_t GLASS = 0xC618, TUBE = 0x18E3, SHINE = 0x6B6D;
  const int cx = x + 14, bulbY = y + 26;
  const uint16_t mercury = valid ? mercuryColor(temperature) : DIM;
  // 유리 (바깥) → 관 안쪽 (어두움)
  screen.fillSmoothRoundRect(cx - 6, y, 13, 26, 6, GLASS);
  screen.fillSmoothCircle(cx, bulbY, 7, GLASS);
  screen.fillSmoothRoundRect(cx - 4, y + 2, 9, 24, 4, TUBE);
  screen.fillSmoothCircle(cx, bulbY, 5, TUBE);
  // 수은주
  float level = valid ? (temperature + 15) / 50 : 0.45f;
  level = level < 0.1f ? 0.1f : level > 1 ? 1 : level;
  const int top = y + 4 + static_cast<int>((1 - level) * 17 + 0.5f);
  screen.fillSmoothCircle(cx, bulbY, 4, mercury);
  screen.fillSmoothRoundRect(cx - 2, top, 5, bulbY - top, 2, mercury);
  // 유리 반사광
  screen.drawFastVLine(cx - 3, y + 4, 12, SHINE);
  screen.fillSmoothCircle(cx - 2, bulbY - 2, 1, WHITE);
  // 눈금 (긴 것 / 짧은 것 번갈아)
  for (int i = 0; i < 5; ++i) screen.drawFastHLine(cx + 9, y + 3 + i * 4, i % 2 ? 3 : 5, i % 2 ? DIM : MUTED);
}
// 안티앨리어싱 쐐기선(양 끝 반지름 ar, br, 둥근 끝). TFT_eSPI 의 drawWedgeLine 은 뷰포트 기준점을
// 옮기면 좌표가 섞여서, 픽셀 단위로 drawPixel(알파 섞기)만 쓰는 버전을 씁니다.
void wedge(float ax, float ay, float bx, float by, float ar, float br, uint16_t color) {
  const float dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy, rmax = ar > br ? ar : br;
  const int x0 = floorf(fminf(ax, bx) - rmax - 1), x1 = ceilf(fmaxf(ax, bx) + rmax + 1);
  const int y0 = floorf(fminf(ay, by) - rmax - 1), y1 = ceilf(fmaxf(ay, by) + rmax + 1);
  for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
    float t = len2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
    const float qx = ax + t * dx - x, qy = ay + t * dy - y, r = ar + t * (br - ar);
    const float cover = r + 0.5f - sqrtf(qx * qx + qy * qy);
    if (cover <= 0.03f) continue;
    if (cover >= 0.97f) screen.drawPixel(x, y, color);
    else screen.TFT_eSPI::drawPixel(x, y, color, static_cast<uint8_t>(cover * 255), 0x00FFFFFF);  // 스프라이트가 이 오버로드를 가려서 기본 클래스로 호출
  }
}
void wideLine(float ax, float ay, float bx, float by, float width, uint16_t color) { wedge(ax, ay, bx, by, width / 2, width / 2, color); }
// 매끈한 물방울: 둥근 아랫부분 + 위로 가늘어지는 쐐기선
void drop(float cx, float cy, float r, uint16_t color) {
  screen.fillSmoothCircle(cx, cy, r, color);
  wedge(cx, cy - r * 2.1f, cx, cy - r * 0.2f, 0.4f, r * 0.92f, color);
}
// 습도: 큰 물방울(반사광) + 작은 물방울 두 개
void iconHumidity(int x, int y) {
  drop(x + 13, y + 22, 8, SKY);
  const float shine[][2] = {{-4.5f, 0.5f}, {-4, 3.5f}, {-2, 5.5f}};
  for (int i = 0; i < 2; ++i)
    wideLine(x + 13 + shine[i][0], y + 22 + shine[i][1], x + 13 + shine[i + 1][0], y + 22 + shine[i + 1][1], 1.8f, WHITE);
  drop(x + 26, y + 11, 3, SKY);
  drop(x + 27, y + 25, 3.5f, SKY);
}
// 바람 깃발(윈드삭): 바람이 셀수록 수평으로 뜹니다. 0m/s 는 60° 처짐, 8m/s 이상은 수평.
void iconWindsock(int x, int y, float wind) {
  wideLine(x + 3, y + 3, x + 3, y + 32, 2.5f, MUTED);
  screen.fillSmoothCircle(x + 3, y + 2, 2, WHITE);
  float droop = 1 - wind / 8; droop = droop < 0 ? 0 : droop > 1 ? 1 : droop;
  const float angle = droop * 1.0472f, dx = cosf(angle), dy = sinf(angle);
  const float radii[] = {6, 5, 4.1f, 3.3f, 2.5f};
  float px = x + 6, py = y + 9;
  for (int i = 0; i < 4; ++i) {
    const float nx = px + dx * 6.2f, ny = py + dy * 6.2f;
    wedge(px, py, nx, ny, radii[i], radii[i + 1], i % 2 ? WHITE : RED);
    px = nx; py = ny;
  }
}
// 해: 지평선 위 반쪽 해(주황 테두리 + 노란 속) + 햇살 + 화살표(일출 ↑ / 일몰 ↓)
void iconSun(int x, int y, bool rising) {
  const float cx = x + 16, horizon = y + 26;
  const float rays[] = {3.40f, 3.93f, 5.50f, 6.02f};  // 195° 225° 315° 345° (위쪽 가운데는 화살표 자리)
  for (float a : rays)
    wideLine(cx + cosf(a) * 12.5f, horizon + sinf(a) * 12.5f, cx + cosf(a) * 16, horizon + sinf(a) * 16, 2, PEACH);
  screen.fillSmoothCircle(cx, horizon, 10, ORANGE);
  screen.fillSmoothCircle(cx, horizon, 7, YELLOW);
  screen.fillRect(x, horizon + 1, 33, 12, BG);
  wideLine(x + 1, horizon + 1, x + 31, horizon + 1, 2, PEACH);
  wideLine(x + 8, horizon + 5, x + 24, horizon + 5, 1.6f, DIM);
  const float tip = rising ? y + 1 : y + 11, tail = rising ? y + 11 : y + 1, back = rising ? 4.5f : -4.5f;
  wideLine(cx, tail, cx, tip, 2, PEACH);
  wideLine(cx - 4, tip + back, cx, tip, 2, PEACH);
  wideLine(cx + 4, tip + back, cx, tip, 2, PEACH);
}
// 미세먼지 얼굴: 등급마다 표정이 바뀝니다 (안티앨리어싱 도형).
//   0 좋음 = 웃는 얼굴 · 1 보통 = 무표정 · 2 나쁨 = 찡그린 얼굴 · 3 매우나쁨 = 방독면 · -1 = 데이터 없음
void stroke(const float (*pts)[2], int n, float cx, float cy, float width, uint16_t color) {
  for (int i = 0; i + 1 < n; ++i)
    wideLine(cx + pts[i][0], cy + pts[i][1], cx + pts[i + 1][0], cy + pts[i + 1][1], width, color);
}
void iconDust(int x, int y, int grade) {
  const float cx = x + 16, cy = y + 17;
  const uint16_t face = grade < 0 ? DIM : dustColor(grade);
  if (grade == 3) {
    // 방독면: 끈 → 마스크 → 큰 눈 렌즈 → 정화통(어두운 테두리 + 구멍)
    screen.fillSmoothRoundRect(x, y + 12, 32, 4, 2, face);
    screen.fillSmoothRoundRect(x + 5, y + 2, 22, 24, 10, face);
    screen.fillSmoothCircle(cx - 5, cy - 6, 4, BG); screen.fillSmoothCircle(cx + 5, cy - 6, 4, BG);
    screen.fillSmoothCircle(cx - 6, cy - 7, 1, DIM); screen.fillSmoothCircle(cx + 4, cy - 7, 1, DIM);
    screen.fillSmoothCircle(cx, cy + 8, 8, BG);
    screen.fillSmoothCircle(cx, cy + 8, 6, face);
    const float holes[][2] = {{0, -3}, {-3, 0}, {3, 0}, {0, 3}};
    for (auto& h : holes) screen.fillSmoothCircle(cx + h[0], cy + 8 + h[1], 1, BG);
    return;
  }
  screen.fillSmoothCircle(cx, cy, 14, face);
  if (grade == 0) {  // 웃는 눈(∩) + 활짝 웃는 입
    const float eyeL[][2] = {{-8, -3}, {-6, -6}, {-3.5f, -3}}, eyeR[][2] = {{3.5f, -3}, {6, -6}, {8, -3}};
    stroke(eyeL, 3, cx, cy, 2.2f, BG); stroke(eyeR, 3, cx, cy, 2.2f, BG);
    const float smile[][2] = {{-7, 3}, {-4, 7}, {0, 8.5f}, {4, 7}, {7, 3}};
    stroke(smile, 5, cx, cy, 2.6f, BG);
    return;
  }
  // 보통 · 나쁨 · 없음: 동그란 눈
  screen.fillSmoothCircle(cx - 5, cy - 3, 2, BG); screen.fillSmoothCircle(cx + 5, cy - 3, 2, BG);
  if (grade == 2) {  // 찡그린 눈썹 + 시무룩한 입
    wideLine(cx - 9, cy - 9, cx - 3, cy - 7, 2.2f, BG);
    wideLine(cx + 3, cy - 7, cx + 9, cy - 9, 2.2f, BG);
    const float frown[][2] = {{-6, 9}, {-3, 6}, {0, 5.5f}, {3, 6}, {6, 9}};
    stroke(frown, 5, cx, cy, 2.6f, BG);
  } else {           // 보통(무표정) · 데이터 없음
    wideLine(cx - 5, cy + 6, cx + 5, cy + 6, 2.6f, BG);
  }
}
// 우산: 물결 모양 덮개 + 살 + 손잡이. 강수확률이 높으면 빗방울도 그립니다.
void iconUmbrella(int x, int y, uint16_t color, bool rainy) {
  const float cx = x + 16, rim = y + 18;
  screen.fillSmoothCircle(cx, rim, 15, color);
  screen.fillRect(x, rim + 1, 33, 16, BG);
  // 덮개 아래 물결: 살 끝 사이를 살짝 파냅니다
  for (int i = -1; i <= 1; ++i) screen.fillSmoothCircle(cx + i * 10, rim + 3, 4, BG);
  screen.fillSmoothCircle(cx, y + 2, 1, WHITE);
  const float handle[][2] = {{0, -2}, {0, 11}, {-1, 13.5f}, {-3.5f, 14}, {-5.5f, 12}};
  for (int i = 0; i < 4; ++i)
    wideLine(cx + handle[i][0], rim + handle[i][1], cx + handle[i + 1][0], rim + handle[i + 1][1], 2, WHITE);
  if (rainy) { drop(x + 4, y + 27, 2, SKY); drop(x + 27, y + 29, 2, SKY); }
}

// 홈 화면 정보 칸: 아이콘 + 라벨(작게) + 값(크게) + 보조 값
void cell(int x, int y, int w, const String& label, uint16_t labelColor, const String& value, uint16_t valueColor, const String& extra = "") {
  text(label, x + 38, y + 1, w - 38, labelColor, FONT_S);
  const int end = text(value, x + 38, y + 16, w - 38, valueColor, FONT_M);
  // 보조 값은 다 들어갈 때만 표시 (가로 화면처럼 칸이 좁으면 생략)
  if (extra.length() && textWidth(extra, FONT_S) <= x + w - end - 3) text(extra, end + 3, y + 20, x + w - end - 3, DIM, FONT_S);
}
void pageDots(int page) {
  for (int i = 0; i < PAGE_COUNT; ++i) {
    const int cx = W / 2 - 15 + i * 10;
    if (i == page) screen.fillCircle(cx, H - 5, 2, CYAN); else screen.drawCircle(cx, H - 5, 2, DIM);
  }
}
void header(const String& title) { text(title, 8, 4, W - 16, WHITE, FONT_M, 1); divider(25); }
}

uint8_t moon_phase(int year, int month, int day, double hour, int* phase);
double Julian(int year, int month, double day);
double sun_position(double j);
double moon_position(double j, double longitude);

namespace {
struct MoonInfo { int bitmap = 0, phase = 0, illumination = 0; };
MoonInfo moonInfo() {
  MoonInfo info; if (!clockValid()) return info;
  const time_t stamp = time(nullptr); struct tm utc; gmtime_r(&stamp, &utc);
  const double h = utc.tm_hour + utc.tm_min / 60.0;
  info.bitmap = moon_phase(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, h, &info.phase);
  const double j = Julian(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday + h / 24.0) - 2444238.5;
  const double sun = sun_position(j), elongation = moon_position(j, sun) - sun;
  info.illumination = lround(50 * (1 - cos(elongation * 3.141592653589793 / 180)));
  return info;
}

// ───────────────────────────── 0. 홈 (대시보드) ─────────────────────────────
// 세로: 아이콘 | 시계·날짜·요일 → 날씨 설명 → 정보 칸 2열x3줄 → 안내 한 줄
// 가로: 아이콘 | 시계 / 날짜 요일 / 날씨 설명 → 정보 칸 3열x2줄 → 안내 한 줄
void home(const WeatherState& s, bool refreshing) {
  const CurrentWeather& current = s.current; const WeatherPoint& p = current.point;
  const time_t now = time(nullptr);
  const bool valid = current.valid, air = airIsFresh(s, now), wide = screenLandscape();

  // 윗줄: 지역 / 갱신 상태
  text(place(s).name, 10, wide ? 4 : 6, wide ? 150 : 120, WHITE, FONT_M);
  String note; uint16_t noteColor = DIM;
  if (refreshing) { note = "업데이트 중…"; noteColor = CYAN; }
  else if (!valid) note = "날씨 기다리는 중";
  else if (stale(s)) { note = "지난 정보 " + localTimeText(current.fetched); noteColor = ORANGE; }
  else note = localTimeText(current.fetched) + " 업데이트";
  text(note, W - 120, wide ? 3 : 9, 112, noteColor, FONT_S, 2);

  // 큰 날씨 아이콘 + 시계 · 날짜 · 요일 · 날씨 설명
  const char* condition = valid ? koreanWeather(p.conditionId) : (WiFi.status() == WL_CONNECTED ? "날씨 받는 중" : "Wi-Fi 연결 중");
  struct tm t; localtime_r(&now, &t);
  const String weekday = String(koreanWeekday(t.tm_wday)) + "요일";
  if (wide) {
    if (valid) weatherIcon(p, 6, 26, 96); else screen.drawRoundRect(14, 34, 80, 80, 12, LINE);
    // 가로 화면은 머리맡에서 보기 좋게 시계를 66px 로 크게 그립니다.
    text(clockValid() ? localTimeText(now) : "--:--", 106, 22, W - 114, CLOCK, FONT_CB, 2);
    text(clockValid() ? koreanDate(now, false) + " " + weekday : String("시간 맞추는 중"), 106, 75, W - 114, WHITE, FONT_G, 2);
    text(condition, 106, 100, W - 114, valid ? LIME : MUTED, FONT_G, 2);
    divider(125);
  } else {
    if (valid) weatherIcon(p, 4, 28, 100); else screen.drawRoundRect(14, 38, 80, 80, 12, LINE);
    text(clockValid() ? localTimeText(now) : "--:--", 104, 28, 128, CLOCK, FONT_C, 2);
    if (clockValid()) {
      text(koreanDate(now, false), 104, 76, 128, WHITE, FONT_G, 2);
      text(weekday, 104, 102, 128, WHITE, FONT_G, 2);
    } else {
      text("시간 맞추는 중", 104, 84, 128, MUTED, FONT_S, 2);
    }
    text(condition, 8, 134, W - 16, WHITE, FONT_G, 1);
  }

  // 정보 칸 6개: 위치는 방향에 따라 (세로 2열x3줄 / 가로 3열x2줄)
  const int cellW = wide ? 102 : 110, columns = wide ? 3 : 2, top = wide ? 130 : 166, rowH = 40;
  auto cx = [&](int i) { return (wide ? 6 : 8) + (i % columns) * (wide ? 104 : 116); };
  auto cy = [&](int i) { return top + (i / columns) * rowH; };
  iconThermometer(cx(0), cy(0), p.temperature, valid);
  cell(cx(0), cy(0), cellW, "기온", PEACH, valid ? celsius(p.temperature) + "C" : String("--"), PEACH,
       valid ? String("체감" + celsius(p.feelsLike)) : String(""));
  iconHumidity(cx(1), cy(1));
  cell(cx(1), cy(1), cellW, "습도", SKY, valid ? String(p.humidity) + "%" : String("--"), SKY);
  iconWindsock(cx(2), cy(2), valid ? p.wind : 3);
  cell(cx(2), cy(2), cellW, valid ? String("바람 · ") + koreanWindDirection(p.windDegrees) + "풍" : String("바람"), LILAC,
       valid ? number(p.wind, 1) + "m/s" : String("--"), LILAC);
  const SunEvent sun = nextSunEvent(current.sunrise, current.sunset, now);
  iconSun(cx(3), cy(3), sun.sunrise);
  cell(cx(3), cy(3), cellW, sun.sunrise ? "일출" : "일몰", PEACH, valid ? localTimeText(sun.at) : String("--:--"), PEACH);
  const int dust = worseDustGrade(s);
  iconDust(cx(4), cy(4), air ? dust : -1);
  cell(cx(4), cy(4), cellW, "미세먼지", air ? dustColor(dust) : MUTED, air ? String(dustGradeText(dust)) : String("--"),
       air ? dustColor(dust) : MUTED, air ? String(lround(s.pm10)) + "/" + String(lround(s.pm25)) : String(""));
  const int pop = s.outlook.valid ? lround(s.outlook.pop12h * 100) : -1;
  iconUmbrella(cx(5), cy(5), pop >= 60 ? BLUE : SKY, pop >= 60);
  cell(cx(5), cy(5), cellW, "강수확률", SKY, pop >= 0 ? String(pop) + "%" : String("--"), pop >= 60 ? BLUE : SKY);

  // 맨 아랫줄: 생활 안내가 있으면 안내, 없으면 구름 양 (영상 속 "Sky covered by 87% Clouds" 자리)
  const int lineY = wide ? 210 : 288;
  divider(lineY);
  const Advice advice = buildAdvice(s, now, thresholds());
  if (valid && advice.level != ADVICE_NONE) {
    weatherIcon(p, 8, lineY + 3, 20);
    text(advice.text, 32, lineY + 7, W - 40, adviceColor(advice.level), FONT_S);
  } else if (valid) {
    bitmap("/icon50/cloudy.bmp", 8, lineY + 3, 20);
    text(String("구름 ") + p.clouds + "% · " + cloudSentence(p.clouds), 32, lineY + 7, W - 40, LIME, FONT_S);
  } else {
    text(s.status, 8, lineY + 7, W - 16, DIM, FONT_S, 1);
  }
}

// ───────────────────────────── 1. 4일 예보 ─────────────────────────────
String dayLabel(int i, const DayForecast& d) {
  struct tm t; const time_t noon = dayNoonUtc(d.localDay); localtime_r(&noon, &t);
  return String(i == 0 ? "내일" : i == 1 ? "모레" : "") + (i < 2 ? String("(") + koreanWeekday(t.tm_wday) + ")"
                                                       : String(t.tm_mday) + "일(" + koreanWeekday(t.tm_wday) + ")");
}
// 가로: 하루를 세로 칸 하나로 (4열)
void forecastColumns(const WeatherState& s) {
  for (int i = 0; i < 4; ++i) {
    const DayForecast& d = s.days[i]; const WeatherPoint& p = d.representative; const int x = i * 80;
    if (i) screen.drawFastVLine(x, 32, 196, LINE);
    if (!d.valid) { if (i == 0) text("예보를 기다리는 중", 8, 110, W - 16, MUTED, FONT_M, 1); continue; }
    const int pop = lround(d.probability * 100);
    struct tm t; const time_t noon = dayNoonUtc(d.localDay); localtime_r(&noon, &t);
    text(dayLabel(i, d), x + 2, 31, 76, YELLOW, FONT_S, 1);
    text(String(t.tm_mon + 1) + "/" + t.tm_mday, x + 2, 46, 76, DIM, FONT_S, 1);
    weatherIcon(p, x + 15, 62, 50);
    const String hi = celsius(d.high), lo = celsius(d.low);
    const int total = textWidth(hi, FONT_M) + textWidth(" " + lo, FONT_M), start = x + (80 - total) / 2;
    const int end = text(hi, start, 116, 40, PEACH, FONT_M);
    text(" " + lo, end, 116, 40, SKY, FONT_M);
    text(koreanWeather(p.conditionId), x + 2, 138, 76, WHITE, FONT_S, 1);
    text("강수 " + String(pop) + "%", x + 2, 156, 76, pop >= 60 ? BLUE : pop >= 30 ? SKY : DIM, FONT_S, 1);
    text(String(koreanWindDirection(p.windDegrees)) + " " + number(p.wind, 1) + "m/s", x + 2, 172, 76, LILAC, FONT_S, 1);
    text("습도 " + String(p.humidity) + "%", x + 2, 188, 76, MUTED, FONT_S, 1);
    text(d.rain >= 0.1f ? String(number(d.rain, 1) + "mm") : String("-"), x + 2, 204, 76, d.rain >= 0.1f ? CYAN : DIM, FONT_S, 1);
  }
}
// 세로: 하루를 가로 줄 하나로 (4줄)
void forecastRows(const WeatherState& s) {
  for (int i = 0; i < 4; ++i) {
    const DayForecast& d = s.days[i]; const WeatherPoint& p = d.representative; const int y = 31 + i * 70;
    if (!d.valid) { if (i == 0) text("예보를 기다리는 중", 8, y + 20, W - 16, MUTED, FONT_M, 1); continue; }
    weatherIcon(p, 6, y + 6, 50);
    const int pop = lround(d.probability * 100);
    String day = i == 0 ? "내일 · " : i == 1 ? "모레 · " : "";
    text(day + koreanDate(dayNoonUtc(d.localDay)), 64, y, 120, YELLOW);
    text("강수 " + String(pop) + "%", 176, y, 56, pop >= 60 ? BLUE : pop >= 30 ? SKY : DIM, FONT_S, 2);
    const int end = text(celsius(d.high), 64, y + 15, 50, PEACH, FONT_M);
    text(String(" / ") + celsius(d.low), end, y + 15, 50, SKY, FONT_M);
    text(koreanWeather(p.conditionId), 140, y + 18, 92, WHITE, FONT_S, 2);
    text(String(koreanWindDirection(p.windDegrees)) + "풍 " + number(p.wind, 1) + "m/s · 습도 " + p.humidity + "%",
         64, y + 36, 168, MUTED);
    if (d.rain >= 0.1f) text("예상 강수량 " + number(d.rain, 1) + "mm", 64, y + 51, 168, CYAN);
    else text("기압 " + String(p.pressure) + "hPa", 64, y + 51, 168, DIM);
    if (i < 3) divider(y + 67);
  }
}
void forecastPage(const WeatherState& s) {
  header(String("4일 예보 · ") + place(s).name);
  if (screenLandscape()) forecastColumns(s); else forecastRows(s);
}

// ───────────────────────────── 2. 상세 ─────────────────────────────
void conditionsPage(const WeatherState& s) {
  header(String("현재 날씨 · ") + place(s).name);
  const WeatherPoint& p = s.current.point; const bool valid = s.current.valid, wide = screenLandscape();
  const time_t now = time(nullptr); const bool air = airIsFresh(s, now);
  // 위쪽: 아이콘 · 큰 기온 · 풍향 화살표 (NSBold36 에는 C 가 없어서 16px 글꼴로 붙임)
  const int topY = wide ? 28 : 32, arrowX = W - 50;
  if (valid) weatherIcon(p, 6, topY, 50);
  const String large = (valid ? number(p.temperature, 1) : String("--")) + "°";
  const int lw = textWidth(large, FONT_L), cw = textWidth("C", FONT_M), start = 60 + (arrowX - 70 - lw - cw - 2) / 2;
  text(large, start, topY + 6, lw, WHITE, FONT_L); text("C", start + lw + 2, topY + 23, cw, WHITE, FONT_M);
  if (valid) {
    bitmap(String("/wind/") + windDirection(p.windDegrees) + ".bmp", arrowX, topY - 2, 40);
    text(String(koreanWindDirection(p.windDegrees)) + "풍", arrowX - 10, topY + 40, 60, LILAC, FONT_S, 1);
  }
  const int descY = wide ? 84 : 88;
  text(valid ? String(koreanWeather(p.conditionId)) + " · 체감 " + number(p.feelsLike, 1) + "°" : String(s.status),
       8, descY, W - 16, YELLOW, FONT_S, 1);
  const Advice advice = buildAdvice(s, now, thresholds());
  text(advice.level != ADVICE_NONE ? String(advice.text) : String(valid ? "특별한 주의사항 없음" : ""), 8, descY + 15, W - 16,
       advice.level != ADVICE_NONE ? adviceColor(advice.level) : DIM, FONT_S, 1);
  const int gridTop = wide ? 116 : 122;
  divider(gridTop);

  const int g10 = pm10Grade(s.pm10), g25 = pm25Grade(s.pm25), gUv = uvGrade(s.uv);
  const bool precipitation = valid && (p.rain > 0 || p.snow > 0);
  const MoonInfo moon = moonInfo();
  const String DASH = "--";
  const String labels[] = {"바람", "습도", "기압", "자외선 지수", "미세먼지 PM10", "초미세 PM2.5",
                           precipitation ? (p.snow > 0 ? "적설 (1시간)" : "강수 (1시간)") : "구름 · 가시거리",
                           "일출 · 일몰", clockValid() ? String("달 · ") + moonPhaseName(moon.phase) : String("달")};
  const String values[] = {
    valid ? String(number(p.wind, 1) + "m/s " + windStrengthText(p.wind)) : DASH,
    valid ? String(String(p.humidity) + "%") : DASH,
    valid ? String(String(p.pressure) + "hPa") : DASH,
    air ? String(number(s.uv, 1) + " " + uvGradeText(gUv)) : DASH,
    air ? String(String(lround(s.pm10)) + "㎍/㎥ " + dustGradeText(g10)) : DASH,
    air ? String(String(lround(s.pm25)) + "㎍/㎥ " + dustGradeText(g25)) : DASH,
    !valid ? DASH : precipitation ? String(number(p.snow > 0 ? p.snow : p.rain, 1) + "mm")
                                  : String(String(p.clouds) + "% · " + number(p.visibility / 1000.0, p.visibility >= 10000 ? 0 : 1) + "km"),
    valid ? String(localTimeText(s.current.sunrise) + (wide ? "~" : " · ") + localTimeText(s.current.sunset)) : DASH,
    clockValid() ? String(String("밝기 ") + moon.illumination + "%") : DASH};
  const uint16_t colors[] = {LILAC, SKY, WHITE, air ? uvColor(gUv) : MUTED, air ? dustColor(g10) : MUTED,
                             air ? dustColor(g25) : MUTED, precipitation ? CYAN : WHITE, PEACH, WHITE};
  // 세로 2열 / 가로 3열 격자
  const int columns = wide ? 3 : 2, colW = wide ? 103 : 120, rowH = wide ? 37 : 36;
  for (int c = 1; c < columns; ++c) screen.drawFastVLine(c * colW + (wide ? 4 : 0), gridTop + 6, H - gridTop - 22, LINE);
  for (int i = 0; i < 9; ++i) {
    const int x = 10 + (i % columns) * colW, y = gridTop + 5 + (i / columns) * rowH;
    const int textW = wide ? colW - 7 : colW - 14;
    text(labels[i], x, y, textW, YELLOW);
    // 값이 칸보다 길면 작은 글꼴로 (말줄임 대신)
    const bool fits = textWidth(values[i], FONT_M) <= textW;
    text(values[i], x, y + (fits ? 14 : 16), textW, colors[i], fits ? FONT_M : FONT_S);
  }
  if (clockValid()) {
    if (wide) bitmap("/moon/moonphase_L" + String(moon.bitmap) + ".bmp", 288, gridTop + 5 + 2 * rowH + 2, 26);
    else bitmap("/moon/moonphase_L" + String(moon.bitmap) + ".bmp", 140, 270, 32);
  }
}

// ───────────────────────────── 3. 시스템 ─────────────────────────────
String uptimeText() {
  const uint32_t seconds = millis() / 1000; char out[32];
  if (seconds >= 86400) snprintf(out, sizeof(out), "%lu일 %lu시간", static_cast<unsigned long>(seconds / 86400), static_cast<unsigned long>(seconds % 86400 / 3600));
  else snprintf(out, sizeof(out), "%02lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600), static_cast<unsigned long>(seconds % 3600 / 60), static_cast<unsigned long>(seconds % 60));
  return String(out);
}
String shortHttp(int code) { return code == 200 ? String("정상") : code == 0 ? String("대기") : String(code); }
String scheduleText(const UiState& ui) {
  if (!ui.scheduleOn || ui.onHour == ui.offHour) return "항상 켜짐";
  char out[40]; snprintf(out, sizeof(out), "%02u:00 켜짐 · %02u:00 %s", ui.onHour, ui.offHour, ui.nightBrightness ? "어둡게" : "꺼짐");
  return String(out);
}
void systemPage(const WeatherState& s, const UiState& ui) {
  header("시스템 정보");
  const bool wide = screenLandscape();
  const Place& where = place(s);
  const uint32_t remaining = weatherNextRefreshSeconds();
  char countdown[24];
  snprintf(countdown, sizeof(countdown), "%02lu:%02lu 후", static_cast<unsigned long>(remaining / 60), static_cast<unsigned long>(remaining % 60));
  const bool online = WiFi.status() == WL_CONNECTED;
  const String labels[] = {"지역", "현재 시각", "화면 일정", "Wi-Fi", "설정 웹", "날씨 · 예보", "대기질", "파일 · 메모리", "가동 · 다음 갱신"};
  const String values[] = {
    String(String(where.name) + " " + String(where.latitude).substring(0, 5) + ", " + String(where.longitude).substring(0, 6)),
    clockValid() ? String(localTimeText(time(nullptr), "%H:%M:%S ") + TIMEZONE_LABEL) : String("NTP 동기화 중"),
    scheduleText(ui),
    online ? String(String(WiFi.RSSI()) + "dBm " + signalQualityText(WiFi.RSSI())) : String("연결 안 됨"),
    online ? String("http://" + WiFi.localIP().toString()) : ui.apMode ? String("설정 모드 192.168.4.1") : String("-"),
    String(shortHttp(s.currentHttp) + " / " + shortHttp(s.forecastHttp) + " · 키" + String(s.activeKey)),
    String(shortHttp(s.airHttp) + (s.airValid ? String(" · " + localTimeText(s.airTimestamp) + " 기준") : String(""))),
    String(String(fileCount) + "개" + (badFiles ? String("(오류" + String(badFiles) + ")") : String("")) + " · " + String(ESP.getFreeHeap() / 1024) + "KB"),
    String(uptimeText() + " · " + (ui.refreshing ? String("받는 중") : remaining ? String(countdown) : String("곧")))};
  const int rowTop = wide ? 29 : 31, pitch = wide ? 14 : 16;
  for (int i = 0; i < 9; ++i) {
    const int y = rowTop + i * pitch;
    text(labels[i], 10, y, 92, YELLOW);
    text(values[i], 100, y, W - 108, i == 7 && badFiles ? ORANGE : WHITE, FONT_S, 2);
  }
  // 상태 문구: 세로는 길면 " · " 에서 두 줄로, 가로는 앞부분 한 줄만
  String status = s.status, second = "";
  const uint16_t statusColor = stale(s) || s.currentHttp < 0 || s.currentHttp > 200 ? ORANGE : CYAN;
  if (textWidth(status, FONT_S) > W - 16) {
    const int split = status.lastIndexOf(" · ");
    if (split > 0) { second = status.substring(split + 4); status = status.substring(0, split); }
  }
  const int statusY = rowTop + 9 * pitch + 3;
  if (wide || !second.length()) text(status, 8, statusY + (wide ? 0 : 7), W - 16, statusColor, FONT_S, 1);
  else { text(status, 8, statusY, W - 16, statusColor, FONT_S, 1); text(second, 8, statusY + 14, W - 16, statusColor, FONT_S, 1); }

  const String buttons[] = {ui.refreshing ? "받는 중…" : "새로고침", ui.autoCycle ? "자동 넘김 켬" : "자동 넘김 끔", "지역 변경",
                            wide ? "세로로 회전" : "가로로 회전", "터치 보정", ui.scheduleOn ? "화면 일정 켬" : "화면 일정 끔"};
  for (int i = 0; i < SYSTEM_BUTTON_COUNT; ++i) {
    const Rect r = systemButtonRect(i);
    const bool active = (i == BUTTON_AUTO && ui.autoCycle) || (i == BUTTON_SCHEDULE && ui.scheduleOn);
    screen.fillRoundRect(r.x, r.y, r.w, r.h, 7, active ? 0x0320 : 0x10A2);
    screen.drawRoundRect(r.x, r.y, r.w, r.h, 7, active ? GREEN : LINE);
    const int font = wide ? FONT_S : FONT_M;
    text(buttons[i], r.x + 2, r.y + (r.h - (wide ? 13 : 17)) / 2 + 1, r.w - 4,
         i == BUTTON_REFRESH && ui.refreshing ? MUTED : active ? GREEN : WHITE, font, 1);
  }
}
bool jpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* pixels) {
  if (y >= H) return false;
  tft.pushImage(x, y, w, h, pixels); return true;
}
void scanDirectory(const char* path) {
  File directory = LittleFS.open(path);
  if (!directory || !directory.isDirectory()) { ++badFiles; return; }
  for (File f = directory.openNextFile(); f; f = directory.openNextFile()) {
    if (f.isDirectory()) continue;
    ++fileCount;
    const String name = f.name();
    if (f.size() == 0) { ++badFiles; continue; }
    if (name.endsWith(".bmp")) {
      const bool header = read16(f) == 0x4d42; read32(f); read32(f); const uint32_t offset = read32(f); read32(f);
      const uint32_t w = read32(f), h = read32(f);
      const bool format = read16(f) == 1 && read16(f) == 24 && read32(f) == 0;
      const uint32_t bytes = offset + ((w * 3 + 3) & ~3U) * h;
      if (!header || !format || w == 0 || w > 120 || h == 0 || h > 120 || f.size() < bytes) ++badFiles;
    } else if (name.endsWith(".jpg")) {
      if (f.read() != 0xff || f.read() != 0xd8) ++badFiles;
    } else if (name.endsWith(".vlw")) {
      uint8_t first[4]; if (f.read(first, 4) != 4 || first[0] || first[1] || first[2] || first[3] == 0) ++badFiles;
    }
  }
}
}
// ── 글꼴에 글자가 있는지: VLW 헤더(24바이트) 뒤 글자마다 28바이트, 첫 4바이트가 유니코드(빅엔디언) ──
static uint32_t be32(const uint8_t* p) { return uint32_t(pgm_read_byte(p)) << 24 | uint32_t(pgm_read_byte(p + 1)) << 16 | uint32_t(pgm_read_byte(p + 2)) << 8 | pgm_read_byte(p + 3); }
static bool fontHasGlyph(const uint8_t* font, uint32_t code) {
  if (code == ' ') return true;
  int lo = 0, hi = static_cast<int>(be32(font)) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2; const uint32_t c = be32(font + 24 + mid * 28);
    if (c == code) return true;
    if (c < code) lo = mid + 1; else hi = mid - 1;
  }
  return false;
}
static uint32_t nextCodepoint(const char*& p) {
  const uint8_t c = *p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && *p) return ((c & 0x1F) << 6) | (*p++ & 0x3F);
  if ((c & 0xF0) == 0xE0 && p[0] && p[1]) { const uint32_t v = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; return v; }
  while ((*p & 0xC0) == 0x80) ++p;  // 4바이트 글자(이모지 등)는 지원하지 않음
  return 0xFFFF;
}
bool fontHasAll(const uint8_t* font, const char* utf8) {
  for (const char* p = utf8; *p;) if (!fontHasGlyph(font, nextCodepoint(p))) return false;
  return true;
}
String missingGlyphs(const char* utf8) {
  String missing;
  for (const char* p = utf8; *p;) {
    const char* start = p; const uint32_t code = nextCodepoint(p);
    if (!fontHasGlyph(KR_FONT_MEDIUM, code)) {
      String glyph; for (const char* q = start; q < p; ++q) glyph += *q;
      if (missing.indexOf(glyph) < 0) missing += glyph;
    }
  }
  return missing;
}
// 화면 버퍼는 화면 높이의 절반(8비트 색, 38.4KB)만 계속 잡아 두고, 위 절반과 아래 절반을 차례로 그립니다.
// 전체(76.8KB)를 잡으면 HTTPS 접속(TLS 핸드셰이크 약 60KB)에 쓸 메모리가 모자라고,
// 그릴 때만 잡으면 날씨를 받는 동안 화면·터치가 멈췄기 때문입니다.
// 뷰포트 기준점을 반 화면만큼 옮기면 같은 그리기 코드를 좌표 그대로 두 번 실행할 수 있습니다.
template <class Draw> static bool drawFrame(Draw draw) {
  if (!screen.created()) {
    screen.setColorDepth(8);
    if (!screen.createSprite(W, H / 2)) { Serial.println("SCREEN buffer alloc failed"); return false; }
  }
  clippingWarnings = 0;
  for (int half = 0; half < 2; ++half) {
    const int top = half * (H / 2);
    screen.setViewport(0, -top, W, H, true);   // 그리기 좌표 y 에서 top 을 빼서 버퍼에 그림
    screen.fillRect(0, top, W, H / 2, BG);
    draw();
    screen.resetViewport();
    screen.pushSprite(0, top);
  }
  if (clippingWarnings) Serial.printf("Display bounds errors: %d\n", clippingWarnings / 2);
  return true;
}
bool renderSetupScreen(const UiState& ui) {
  return drawFrame([&] {
  const bool wide = screenLandscape();
  header("Wi-Fi 설정 모드");
  int y = wide ? 36 : 44;
  text("휴대폰으로 아래 Wi-Fi 에 연결하세요", 8, y, W - 16, MUTED, FONT_S, 1);
  text(ui.apName, 8, y + 18, W - 16, CLOCK, FONT_M, 1);
  y += wide ? 48 : 60;
  const char* steps[] = {"1  휴대폰 Wi-Fi 목록에서 위 이름을 누르기",
                         "2  설정 페이지가 저절로 열려요",
                         "    (안 열리면 브라우저에서 192.168.4.1)",
                         "3  집 Wi-Fi 를 고르고 비밀번호 입력"};
  for (int i = 0; i < 4; ++i) text(steps[i], wide ? 24 : 12, y + i * (wide ? 18 : 22), W - (wide ? 40 : 20), i == 2 ? DIM : WHITE, FONT_S);
  text("저장하면 기기가 다시 시작하고 날씨를 받아요", 8, H - (wide ? 34 : 44), W - 16, LIME, FONT_S, 1);
  });
}
bool setDisplayLandscape(bool landscape) {
  setScreenLandscape(landscape);
  W = screenWidth(); H = screenHeight();
  tft.setRotation(landscape ? LANDSCAPE_ROTATION : PORTRAIT_ROTATION); tft.fillScreen(BG);
  // 방향이 바뀌면 반 화면 버퍼 크기도 바뀝니다 (가로 320x120 / 세로 240x160, 둘 다 38.4KB)
  screen.deleteSprite(); screen.setColorDepth(8);
  return screen.createSprite(W, H / 2) != nullptr;
}
void setDisplayInvert(bool invert) { tft.invertDisplay(invert); }
bool beginDisplay(bool landscape) {
  tft.begin();
  return setDisplayLandscape(landscape);
}
void showMessage(const char* line1, const char* line2, uint16_t color) {
  tft.fillScreen(BG); tft.loadFont(KR_FONT_MEDIUM); tft.setTextDatum(MC_DATUM);
  tft.setTextColor(color, BG); tft.drawString(line1, W / 2, H / 2 - 14);
  tft.setTextColor(TFT_WHITE, BG); tft.drawString(line2, W / 2, H / 2 + 14);
  tft.unloadFont();
}
void showBootSplash(const char* placeName) {
  const bool wide = screenLandscape();
  TJpgDec.setJpgScale(1); TJpgDec.setSwapBytes(true); TJpgDec.setCallback(jpgOutput);
  uint16_t jw = 0, jh = 0;
  if (LittleFS.exists("/splash/OpenWeather.jpg") && TJpgDec.getFsJpgSize(&jw, &jh, "/splash/OpenWeather.jpg", LittleFS) == JDR_OK)
    TJpgDec.drawFsJpg((W - jw) / 2, wide ? 26 : 70, "/splash/OpenWeather.jpg", LittleFS);
  // 22px 글꼴에 없는 글자가 있는 지역 이름(직접 입력)은 16px 글꼴로
  tft.loadFont(fontHasAll(KR_FONT_LARGE, placeName) ? KR_FONT_LARGE : KR_FONT_MEDIUM);
  tft.setTextColor(CLOCK, BG); tft.setTextDatum(MC_DATUM);
  tft.drawString(placeName, W / 2, wide ? 178 : 228);
  tft.unloadFont();
  tft.loadFont(KR_FONT_SMALL); tft.setTextColor(MUTED, BG);
  tft.drawString("CYD 날씨 스테이션 · 한국판", W / 2, wide ? 206 : 256);
  tft.unloadFont();
}
void auditAssets() {
  fileCount = badFiles = 0;
  for (const char* path : {"/icon", "/icon50", "/moon", "/wind", "/fonts", "/splash"}) scanDirectory(path);
  Serial.printf("ASSETS files=%d errors=%d\n", fileCount, badFiles);
}
bool renderPage(const WeatherState& weather, int page, const UiState& ui) {
  return drawFrame([&] {
    switch (wrapPage(page)) {
      case 0: home(weather, ui.refreshing); break;
      case 1: forecastPage(weather); break;
      case 2: conditionsPage(weather); break;
      case 3: systemPage(weather, ui); break;
    }
    pageDots(wrapPage(page));
  });
}
bool renderCalibration(int step, bool failed) {
  return drawFrame([&] {
  header(screenLandscape() ? "터치 보정 · 가로 화면" : "터치 보정 · 세로 화면");
  const int y = H / 2 - 40;
  text(failed ? "다시 해 주세요" : "노란 표적을 꾹 누르고", 8, y, W - 16, failed ? ORANGE : WHITE, FONT_M, 1);
  text(failed ? "표적을 꾹 누르고 있다가 떼세요" : "잠시 뒤에 손을 떼세요", 8, y + 22, W - 16, WHITE, FONT_M, 1);
  text(String(step + 1) + " / 4 번째 점 · 끝나면 자동 저장", 8, y + 54, W - 16, CYAN, FONT_S, 1);
  text("화면을 2초 길게 누르면 언제든 다시 보정", 8, y + 74, W - 16, MUTED, FONT_S, 1);
  int x, ty; calibrationTarget(step, x, ty);
  screen.drawCircle(x, ty, 10, YELLOW);
  screen.drawFastHLine(x - 14, ty, 29, YELLOW);
  screen.drawFastVLine(x, ty - 14, 29, YELLOW);
  });
}
