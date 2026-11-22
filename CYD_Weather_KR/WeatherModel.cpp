#include "WeatherModel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ─────────────────────────── 예보 가공 ───────────────────────────
int32_t localDayNumber(time_t utc, int32_t offset) {
  const int64_t local = static_cast<int64_t>(utc) + offset;
  return static_cast<int32_t>(local >= 0 ? local / 86400 : (local - 86399) / 86400);
}
static int secondsFromLocalNoon(time_t utc, int32_t offset) {
  const int64_t local = static_cast<int64_t>(utc) + offset;
  const int secondOfDay = static_cast<int>(((local % 86400) + 86400) % 86400);
  return abs(secondOfDay - 43200);
}
void buildDailyForecast(const WeatherPoint* points, size_t count, time_t nowUtc, int32_t offset, DayForecast (&days)[4]) {
  const int32_t today = localDayNumber(nowUtc, offset);
  for (int i = 0; i < 4; ++i) { days[i] = DayForecast{}; days[i].localDay = today + 1 + i; }
  for (size_t i = 0; i < count; ++i) {
    const WeatherPoint& p = points[i];
    const int index = localDayNumber(p.timestamp, offset) - today - 1;
    if (index < 0 || index > 3) continue;
    DayForecast& day = days[index];
    // 대표 날씨(아이콘·설명·바람)는 그날 정오에 가장 가까운 3시간 예보를 씁니다.
    if (!day.valid || secondsFromLocalNoon(p.timestamp, offset) < secondsFromLocalNoon(day.representative.timestamp, offset))
      day.representative = p;
    if (!day.valid || p.low < day.low) day.low = p.low;
    if (!day.valid || p.high > day.high) day.high = p.high;
    day.rain += p.rain + p.snow;
    if (p.precipitationProbability > day.probability) day.probability = p.precipitationProbability;
    ++day.samples;
    day.valid = true;
  }
}
Outlook buildOutlook(const WeatherPoint* points, size_t count, time_t nowUtc) {
  Outlook o;
  for (size_t i = 0; i < count; ++i) {
    const WeatherPoint& p = points[i];
    // 3시간 구간이 지금과 겹치면 포함 (구간 시작 시각 기준)
    if (p.timestamp + 3 * 3600 <= nowUtc) continue;
    const time_t ahead = p.timestamp - nowUtc;
    if (ahead < 12 * 3600) {
      if (p.precipitationProbability > o.pop12h) o.pop12h = p.precipitationProbability;
      o.rain12h += p.rain + p.snow;
    }
    if (ahead < 24 * 3600) {
      if (!o.valid || p.high > o.high24h) o.high24h = p.high;
      if (!o.valid || p.low < o.low24h) o.low24h = p.low;
      o.valid = true;
    }
  }
  return o;
}

// ─────────────────────────── 아이콘 / 방위 ───────────────────────────
const char* weatherIconName(uint16_t id, const char* icon) {
  const bool night = icon && strlen(icon) >= 3 && icon[2] == 'n';
  if (id >= 200 && id < 300) return "thunderstorm";
  if (id >= 300 && id < 400) return "drizzle";
  if (id == 500 || id == 520) return "lightRain";
  if (id == 511 || (id >= 611 && id <= 616)) return "sleet";
  if (id >= 500 && id < 600) return "rain";
  if (id >= 600 && id < 700) return "snow";
  if (id == 771 || id == 781) return "wind";
  if (id >= 700 && id < 800) return "fog";
  if (id == 800) return night ? "clear-night" : "clear-day";
  if (id == 801 || id == 802) return night ? "partly-cloudy-night" : "partly-cloudy-day";
  if (id == 803 || id == 804) return "cloudy";
  return "unknown";
}
static int octant(int degrees) {
  const int normalized = ((degrees % 360) + 360) % 360;
  return ((normalized + 22) / 45) % 8;
}
const char* windDirection(int degrees) {
  static const char* directions[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return directions[octant(degrees)];
}

// ─────────────────────────── 한국어 표기 ───────────────────────────
const char* koreanWeather(uint16_t id) {
  switch (id) {
    case 200: return "약한 뇌우";      case 201: return "뇌우";         case 202: return "강한 뇌우";
    case 210: return "약한 천둥";      case 211: return "천둥번개";     case 212: return "강한 천둥번개";
    case 221: return "천둥번개";       case 230: case 231: case 232: return "뇌우·이슬비";
    case 300: return "약한 이슬비";    case 301: return "이슬비";       case 302: return "강한 이슬비";
    case 310: case 311: case 312: return "이슬비·비";
    case 313: case 314: case 321: return "소나기·이슬비";
    case 500: return "약한 비";        case 501: return "비";           case 502: return "강한 비";
    case 503: return "매우 강한 비";   case 504: return "폭우";         case 511: return "어는 비";
    case 520: return "약한 소나기";    case 521: return "소나기";       case 522: return "강한 소나기";
    case 531: return "소나기";
    case 600: return "약한 눈";        case 601: return "눈";           case 602: return "폭설";
    case 611: return "진눈깨비";       case 612: return "약한 진눈깨비"; case 613: return "진눈깨비";
    case 615: return "약한 비와 눈";   case 616: return "비와 눈";
    case 620: return "약한 눈 소나기"; case 621: return "눈 소나기";    case 622: return "강한 눈 소나기";
    case 701: return "박무";           case 711: return "연기";         case 721: return "연무";
    case 731: return "모래먼지";       case 741: return "안개";         case 751: return "모래바람";
    case 761: return "황사";           case 762: return "화산재";       case 771: return "돌풍";
    case 781: return "토네이도";
    case 800: return "맑음";           case 801: return "대체로 맑음";  case 802: return "구름 조금";
    case 803: return "구름 많음";      case 804: return "흐림";
  }
  if (id >= 200 && id < 300) return "뇌우";
  if (id >= 300 && id < 400) return "이슬비";
  if (id >= 500 && id < 600) return "비";
  if (id >= 600 && id < 700) return "눈";
  if (id >= 700 && id < 800) return "흐린 대기";
  return "알 수 없음";
}
const char* koreanWindDirection(int degrees) {
  static const char* directions[] = {"북", "북동", "동", "남동", "남", "남서", "서", "북서"};
  return directions[octant(degrees)];
}
const char* koreanWeekday(int wday) {
  static const char* names[] = {"일", "월", "화", "수", "목", "금", "토"};
  return names[((wday % 7) + 7) % 7];
}
const char* windStrengthText(float speed) {
  // 기상청 예보 용어: 약한 바람(4m/s 미만) / 약간 강한(4~9) / 강한(9~14) / 매우 강한(14 이상)
  if (speed < 4) return "약함";
  if (speed < 9) return "약간 강함";
  if (speed < 14) return "강함";
  return "매우 강함";
}
int pm10Grade(float v) {
  const long n = lroundf(v);
  return n <= 30 ? 0 : n <= 80 ? 1 : n <= 150 ? 2 : 3;
}
int pm25Grade(float v) {
  const long n = lroundf(v);
  return n <= 15 ? 0 : n <= 35 ? 1 : n <= 75 ? 2 : 3;
}
const char* dustGradeText(int grade) {
  static const char* names[] = {"좋음", "보통", "나쁨", "매우나쁨"};
  return grade >= 0 && grade < 4 ? names[grade] : "-";
}
int uvGrade(float index) {
  const long n = lroundf(index);
  return n <= 2 ? 0 : n <= 5 ? 1 : n <= 7 ? 2 : n <= 10 ? 3 : 4;
}
const char* uvGradeText(int grade) {
  static const char* names[] = {"낮음", "보통", "높음", "매우높음", "위험"};
  return grade >= 0 && grade < 5 ? names[grade] : "-";
}
const char* signalQualityText(int rssi) {
  if (rssi >= -55) return "아주 좋음";
  if (rssi >= -67) return "좋음";
  if (rssi >= -75) return "보통";
  return "약함";
}
const char* moonPhaseName(int phase) {
  static const char* names[] = {"삭", "초승달", "상현달", "차가는 달", "보름달", "기우는 달", "하현달", "그믐달"};
  return names[((phase % 8) + 8) % 8];
}
SunEvent nextSunEvent(time_t sunrise, time_t sunset, time_t now) {
  SunEvent e;
  if (now < sunrise) { e.sunrise = true; e.at = sunrise; }
  else if (now < sunset) { e.sunrise = false; e.at = sunset; }
  else { e.sunrise = true; e.at = sunrise + 86400; }
  return e;
}
const char* cloudSentence(int clouds) {
  if (clouds <= 10) return "구름 없이 맑은 하늘";
  if (clouds <= 50) return "구름이 조금 있는 하늘";
  if (clouds <= 84) return "구름이 많은 하늘";
  return "구름으로 덮인 하늘";
}
bool isPrecipitation(uint16_t id) { return id >= 200 && id < 700; }
bool airIsFresh(const WeatherState& s, time_t nowUtc) {
  return s.airValid && nowUtc - s.airTimestamp < 3 * 3600;
}

Advice buildAdvice(const WeatherState& s, time_t nowUtc, const AdviceThresholds& t) {
  Advice a;
  auto set = [&a](AdviceLevel level, const char* text) {
    a.level = level; snprintf(a.text, sizeof(a.text), "%s", text);
  };
  if (!s.current.valid) return a;
  const WeatherPoint& p = s.current.point;
  const bool air = airIsFresh(s, nowUtc);
  const int dust = air ? (pm25Grade(s.pm25) >= pm10Grade(s.pm10) ? pm25Grade(s.pm25) : pm10Grade(s.pm10)) : 0;
  const char* dustName = air && pm25Grade(s.pm25) >= pm10Grade(s.pm10) ? "초미세먼지" : "미세먼지";
  char text[72];
  if (p.feelsLike >= t.heatFeelsLike) {
    snprintf(text, sizeof(text), "폭염 주의 · 체감 %.0f°", p.feelsLike); set(ADVICE_WARNING, text);
  } else if (p.temperature <= t.coldTemperature) {
    snprintf(text, sizeof(text), "한파 주의 · 체감 %.0f°", p.feelsLike); set(ADVICE_WARNING, text);
  } else if (isPrecipitation(p.conditionId)) {
    const bool snow = p.conditionId >= 600;
    set(ADVICE_CAUTION, snow ? "눈 오는 중 · 미끄럼 주의" : "비 오는 중 · 우산 챙기세요");
  } else if (p.wind >= t.strongWind) {
    snprintf(text, sizeof(text), "강풍 주의 · %.0f m/s", p.wind); set(ADVICE_WARNING, text);
  } else if (dust == 3) {
    snprintf(text, sizeof(text), "%s 매우나쁨 · 외출 자제", dustName); set(ADVICE_WARNING, text);
  } else if (s.outlook.valid && s.outlook.pop12h >= t.umbrellaPop) {
    snprintf(text, sizeof(text), "우산 챙기세요 · 강수 %.0f%%", s.outlook.pop12h * 100); set(ADVICE_CAUTION, text);
  } else if (dust == 2) {
    snprintf(text, sizeof(text), "%s 나쁨 · 마스크 권장", dustName); set(ADVICE_CAUTION, text);
  } else if (air && uvGrade(s.uv) >= 2) {
    snprintf(text, sizeof(text), "자외선 %s · 선크림 필수", uvGradeText(uvGrade(s.uv))); set(ADVICE_CAUTION, text);
  } else if (s.outlook.valid && s.outlook.high24h - s.outlook.low24h >= t.bigDailyRange) {
    snprintf(text, sizeof(text), "일교차 %.0f° · 겉옷 챙기세요", s.outlook.high24h - s.outlook.low24h); set(ADVICE_INFO, text);
  } else if (air && dust == 0 && p.conditionId == 800 && p.temperature >= 15 && p.temperature <= 26) {
    set(ADVICE_INFO, "맑고 공기 좋음 · 산책하기 좋아요");
  }
  return a;
}

// ─────────────────────────── 화면 방향 ───────────────────────────
static bool g_landscape = false;
void setScreenLandscape(bool landscape) { g_landscape = landscape; }
bool screenLandscape() { return g_landscape; }
int screenWidth() { return g_landscape ? 320 : 240; }
int screenHeight() { return g_landscape ? 240 : 320; }

// ─────────────────────────── 페이지 / 터치 ───────────────────────────
int wrapPage(int page) { return (page % PAGE_COUNT + PAGE_COUNT) % PAGE_COUNT; }
uint32_t secondsUntil(uint32_t nowMs, uint32_t deadlineMs) {
  const int32_t remaining = static_cast<int32_t>(deadlineMs - nowMs);
  return remaining > 0 ? (static_cast<uint32_t>(remaining) + 999) / 1000 : 0;
}
// 좌우로 밀기: 다음/이전 화면. 짧게 탭: 왼쪽 1/4 은 이전, 나머지는 다음 화면.
int pageForGesture(int current, int x0, int y0, int x1, int y1) {
  const int dx = x1 - x0, dy = y1 - y0;
  if (abs(dx) >= 40 && abs(dx) > abs(dy)) return wrapPage(current + (dx < 0 ? 1 : -1));
  if (abs(dx) > 20 || abs(dy) > 20) return current;
  return wrapPage(current + (x1 < screenWidth() / 4 ? -1 : 1));
}
Rect systemButtonRect(int index) {
  Rect r;
  if (g_landscape) {  // 3열 x 2줄, 98x26
    r.w = 98; r.h = 26; r.x = 9 + (index % 3) * 103; r.y = 176 + (index / 3) * 30;
  } else {            // 2열 x 3줄, 108x30
    r.w = 108; r.h = 30; r.x = 8 + (index % 2) * 116; r.y = 206 + (index / 2) * 34;
  }
  return r;
}
int systemButtonsTop() { return systemButtonRect(0).y; }
int systemButtonAt(int x, int y) {
  for (int i = 0; i < SYSTEM_BUTTON_COUNT; ++i) {
    const Rect r = systemButtonRect(i);
    if (x >= r.x - 2 && x <= r.x + r.w + 2 && y >= r.y - 2 && y <= r.y + r.h + 2) return i;
  }
  return -1;
}
void calibrationTarget(int step, int& x, int& y) {
  x = step == 1 || step == 2 ? screenWidth() - CALIBRATION_MARGIN : CALIBRATION_MARGIN;
  y = step >= 2 ? screenHeight() - CALIBRATION_MARGIN : CALIBRATION_MARGIN;
}
TouchCalibration defaultTouchCalibration() {
  TouchCalibration c; const float w = screenWidth() - 1, h = screenHeight() - 1;
  c.xx = w / 3600; c.xc = -200 * w / 3600; c.yy = h / 3600; c.yc = -200 * h / 3600;
  return c;
}
bool isNightHour(int hour, int start, int end) {
  if (start == end) return false;
  return start < end ? hour >= start && hour < end : hour >= start || hour < end;
}
// 세 점(0,1,3)으로 아핀 변환을 구하고 네 번째 점(2)으로 검증합니다. 축이 뒤바뀐 패널도 처리됩니다.
bool fitTouchCalibration(const int raw[4][2], TouchCalibration& r) {
  const float spanX = screenWidth() - 2 * CALIBRATION_MARGIN, spanY = screenHeight() - 2 * CALIBRATION_MARGIN;
  const float ax = raw[1][0] - raw[0][0], ay = raw[1][1] - raw[0][1];
  const float bx = raw[3][0] - raw[0][0], by = raw[3][1] - raw[0][1];
  const float determinant = ax * by - ay * bx;
  if (fabsf(determinant) < 100000) return false;
  r.xx = spanX * by / determinant; r.xy = -spanX * bx / determinant;
  r.xc = CALIBRATION_MARGIN - r.xx * raw[0][0] - r.xy * raw[0][1];
  r.yx = -spanY * ay / determinant; r.yy = spanY * ax / determinant;
  r.yc = CALIBRATION_MARGIN - r.yx * raw[0][0] - r.yy * raw[0][1];
  const float x = r.xx * raw[2][0] + r.xy * raw[2][1] + r.xc;
  const float y = r.yx * raw[2][0] + r.yy * raw[2][1] + r.yc;
  return fabsf(x - (screenWidth() - CALIBRATION_MARGIN)) < 20 && fabsf(y - (screenHeight() - CALIBRATION_MARGIN)) < 20;
}
void mapTouch(const TouchCalibration& c, int rx, int ry, int& x, int& y) {
  x = lroundf(c.xx * rx + c.xy * ry + c.xc); y = lroundf(c.yx * rx + c.yy * ry + c.yc);
  if (x < 0) x = 0;
  if (x >= screenWidth()) x = screenWidth() - 1;
  if (y < 0) y = 0;
  if (y >= screenHeight()) y = screenHeight() - 1;
}
