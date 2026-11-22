// PC 에서 실제 DisplayUI.cpp 를 그대로 컴파일해 화면을 PPM 이미지로 저장합니다.
// 빌드·실행: python tools/preview/render.py
#include "../../CYD_Weather_KR/DisplayUI.cpp"
#include "../../CYD_Weather_KR/WeatherModel.cpp"
#include "../../CYD_Weather_KR/MoonPhase.ino"
#include <string>

static time_t g_now = 0;
static uint32_t g_millis = 3 * 86400000UL + 5 * 3600000UL;
time_t fake_time(time_t* out) { if (out) *out = g_now; return g_now; }
uint32_t millis() { return g_millis; }
uint32_t weatherNextRefreshSeconds() { return 612; }


// 2026-11-22 (일) 08:15 KST
static time_t kst(int y, int mo, int d, int h, int mi) {
  struct tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi;
  return _mkgmtime(&t) - 9 * 3600;
}
static WeatherPoint point(time_t ts, float temp, float feels, int hum, int id, const char* icon, float wind, int deg, int clouds) {
  WeatherPoint p; p.timestamp = ts; p.temperature = temp; p.feelsLike = feels; p.low = temp - 3; p.high = temp + 3;
  p.humidity = hum; p.pressure = 1021; p.conditionId = id; strlcpy(p.icon, icon, sizeof(p.icon));
  p.wind = wind; p.windDegrees = deg; p.clouds = clouds; p.visibility = 10000; return p;
}
static WeatherState sample(bool rainy) {
  WeatherState s; s.activeKey = 1; s.currentHttp = s.forecastHttp = s.airHttp = 200;
  s.current.valid = true; s.current.fetched = g_now - 240;
  s.current.sunrise = kst(2026, 11, 22, 7, 20); s.current.sunset = kst(2026, 11, 22, 17, 15);
  s.current.point = rainy ? point(g_now, 6.8f, 4.1f, 88, 500, "10d", 4.4f, 135, 100)
                          : point(g_now, 4.2f, 1.3f, 58, 803, "04d", 3.4f, 315, 75);
  if (rainy) s.current.point.rain = 0.6f;
  struct { int id; const char* icon; float hi, lo, pop, rain; int deg; float wind; } days[4] = {
    {800, "01d", 9, -2, 0.0f, 0, 315, 2.8f}, {802, "02d", 11, 1, 0.1f, 0, 270, 2.1f},
    {500, "10d", 8, 3, 0.7f, 6.5f, 135, 4.9f}, {600, "13d", 2, -4, 0.6f, 3.2f, 0, 5.6f}};
  for (int i = 0; i < 4; ++i) {
    DayForecast& d = s.days[i]; d.valid = true; d.localDay = localDayNumber(g_now, 32400) + 1 + i; d.samples = 8;
    d.high = days[i].hi; d.low = days[i].lo; d.probability = days[i].pop; d.rain = days[i].rain;
    d.representative = point(kst(2026, 11, 23 + i, 12, 0), days[i].hi, days[i].hi - 2, 55 + i * 8, days[i].id, days[i].icon, days[i].wind, days[i].deg, 30);
  }
  s.outlook.valid = true; s.outlook.pop12h = rainy ? 0.9f : 0.2f; s.outlook.high24h = 9; s.outlook.low24h = rainy ? 4 : -1;
  s.airValid = true; s.airTimestamp = g_now - 900; s.airFetched = g_now - 240;
  s.pm10 = rainy ? 18 : 42; s.pm25 = rainy ? 9 : 23; s.uv = 1.8f;
  strlcpy(s.status, "모든 정보가 최신입니다", sizeof(s.status));
  return s;
}
static void save(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb"); fprintf(f, "P6\n%d %d\n255\n", tft.w, tft.h);
  for (uint16_t c : tft.fb) { const uint8_t rgb[3] = {uint8_t((c >> 11) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63), uint8_t((c & 31) * 255 / 31)}; fwrite(rgb, 1, 3, f); }
  fclose(f);
}
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: preview <data dir> <out dir>\n"); return 2; }
  g_dataRoot = argv[1]; const std::string out = argv[2];
  g_now = kst(2026, 11, 22, 8, 15);
  beginDisplay(false); auditAssets();
  const WeatherState normal = sample(false), rainy = sample(true);
  UiState ui; ui.autoCycle = true;
  for (int o = 0; o < 2; ++o) {
    setDisplayLandscape(o == 1);
    const std::string pre = out + (o ? "/l-" : "/");
    renderPage(normal, 0, ui); save(pre + "0-home.ppm");
    renderPage(rainy, 0, ui); save(pre + "0-home-rain.ppm");
    renderPage(normal, 1, ui); save(pre + "1-forecast.ppm");
    renderPage(normal, 2, ui); save(pre + "2-details.ppm");
    renderPage(normal, 3, ui); save(pre + "3-system.ppm");
    renderCalibration(1); save(pre + "4-calibration.ppm");
    WeatherState empty; strlcpy(empty.place.name, "부산", sizeof(empty.place.name)); UiState busy; busy.refreshing = true;
    renderPage(empty, 0, busy); save(pre + "5-boot.ppm");
    UiState setup; setup.apMode = true; strlcpy(setup.apName, "CYD-Weather-1A2B", sizeof(setup.apName));
    renderSetupScreen(setup); save(pre + "7-setup.ppm");
  }
  // 아이콘 견본: 미세먼지 등급별 얼굴 / 기온별 온도계
  setDisplayLandscape(true); screen.deleteSprite(); screen.createSprite(W, H); screen.fillSprite(BG);
  text("미세먼지 표정", 8, 6, 304, WHITE, FONT_M, 1);
  const char* grades[] = {"좋음", "보통", "나쁨", "매우나쁨", "없음"};
  for (int g = 0; g < 5; ++g) {
    const int gx = 12 + g * 62;
    iconDust(gx + 6, 34, g < 4 ? g : -1);
    text(grades[g], gx - 6, 74, 56, g < 4 ? dustColor(g) : DIM, FONT_S, 1);
  }
  text("기온별 온도계", 8, 98, 304, WHITE, FONT_M, 1);
  const float temps[] = {-12, -2, 6, 18, 27, 34};
  for (int i = 0; i < 6; ++i) {
    const int tx = 10 + i * 52;
    iconThermometer(tx + 6, 122, temps[i], true);
    text(celsius(temps[i]) + "C", tx - 2, 162, 48, WHITE, FONT_S, 1);
  }
  // 습도 · 바람(약함/보통/강함) · 일출 · 일몰 · 강수(낮음/높음)
  iconHumidity(6, 200); iconWindsock(52, 200, 0); iconWindsock(98, 200, 4); iconWindsock(144, 200, 10);
  iconSun(190, 200, true); iconSun(232, 200, false);
  iconUmbrella(274, 200, SKY, false);
  screen.pushSprite(0, 0); save(out + "/6-icons.ppm");
  if (!TFT_eSPI::missingGlyphs.empty()) {
    fprintf(stderr, "MISSING GLYPHS:");
    for (uint16_t c : TFT_eSPI::missingGlyphs) fprintf(stderr, " U+%04X", c);
    fprintf(stderr, "\n"); return 1;
  }
  fprintf(stderr, "OK\n");
  return 0;
}
