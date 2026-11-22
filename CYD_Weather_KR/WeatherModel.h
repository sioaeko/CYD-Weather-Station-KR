#pragma once
// 하드웨어와 무관한 날씨 모델 / 한국어 표기 / 터치 계산.
// Arduino 헤더를 쓰지 않으므로 PC 에서 단위 테스트와 화면 미리보기에 그대로 쓸 수 있습니다.
#include <stdint.h>
#include <stddef.h>
#include <time.h>
constexpr int PAGE_COUNT = 4;
// 화면 방향: 세로 240x320 / 가로 320x240. 실행 중에 바꿀 수 있습니다 (시스템 → 화면 회전).
void setScreenLandscape(bool landscape);
bool screenLandscape();
int screenWidth();
int screenHeight();
// 날씨를 받을 장소 (프리셋 또는 설정 웹에서 직접 입력). 이름은 UTF-8, 한글 10자 안팎.
struct Place {
  char name[32] = "서울";
  char latitude[12] = "37.5665";
  char longitude[12] = "126.9780";
};
struct WeatherPoint {
  time_t timestamp = 0;
  float temperature = 0, feelsLike = 0, low = 0, high = 0;
  float wind = 0, rain = 0, snow = 0, precipitationProbability = 0;
  int humidity = 0, pressure = 0, clouds = 0, windDegrees = 0, visibility = 0;
  uint16_t conditionId = 0;
  char icon[4] = "";
};
struct CurrentWeather {
  bool valid = false;
  WeatherPoint point;
  time_t sunrise = 0, sunset = 0, fetched = 0;
};
struct DayForecast {
  bool valid = false;
  int32_t localDay = 0;
  int samples = 0;
  float low = 0, high = 0, rain = 0, probability = 0;
  WeatherPoint representative;
};
// 앞으로 12/24시간 요약 (오늘 남은 시간의 우산·일교차 안내용)
struct Outlook {
  bool valid = false;
  float pop12h = 0, rain12h = 0;   // 12시간 최대 강수확률(0~1), 누적 강수량(mm)
  float high24h = 0, low24h = 0;   // 24시간 최고/최저 기온
};
enum AdviceLevel : uint8_t { ADVICE_NONE = 0, ADVICE_INFO, ADVICE_CAUTION, ADVICE_WARNING };
struct Advice {
  AdviceLevel level = ADVICE_NONE;
  char text[72] = "";
};
struct WeatherState {
  CurrentWeather current;
  DayForecast days[4];
  Outlook outlook;
  time_t forecastFetched = 0, airFetched = 0, airTimestamp = 0;
  float uv = 0, pm10 = 0, pm25 = 0;
  bool airValid = false;
  int currentHttp = 0, forecastHttp = 0, airHttp = 0;
  uint8_t activeKey = 1;
  Place place;
  uint32_t placeId = 0;
  char status[96] = "Wi-Fi 연결 중";
  uint32_t generation = 0;
};

// ── 예보 가공 ──
int32_t localDayNumber(time_t utc, int32_t utcOffsetSeconds);
void buildDailyForecast(const WeatherPoint* points, size_t count, time_t nowUtc, int32_t utcOffsetSeconds,
                        DayForecast (&days)[4]);
Outlook buildOutlook(const WeatherPoint* points, size_t count, time_t nowUtc);

// ── 아이콘 / 방위 ──
const char* weatherIconName(uint16_t conditionId, const char* apiIcon);
const char* windDirection(int degrees);          // 영문 8방위 (바람 화살표 파일 이름)

// ── 한국어 표기 ──
const char* koreanWeather(uint16_t conditionId);  // OpenWeather 코드 → "구름 많음" 등
const char* koreanWindDirection(int degrees);     // "북서" (바람이 불어오는 방향)
const char* koreanWeekday(int wday);              // 0=일 … 6=토
const char* windStrengthText(float metersPerSecond);  // 기상청 바람 세기 표현
// 등급: 0 좋음 / 1 보통 / 2 나쁨 / 3 매우나쁨 (환경부 2018 개정 기준)
int pm10Grade(float microgramsPerCubicMeter);
int pm25Grade(float microgramsPerCubicMeter);
const char* dustGradeText(int grade);
// 자외선: 0 낮음 / 1 보통 / 2 높음 / 3 매우높음 / 4 위험 (기상청 기준)
int uvGrade(float index);
const char* uvGradeText(int grade);
const char* signalQualityText(int rssi);
const char* moonPhaseName(int phase);             // moon_phase() 의 0~7 단계 → "보름달" 등
// 다음 해 뜨는/지는 시각: 해 뜨기 전이면 일출, 해 진 뒤면 내일 일출(약 24시간 뒤), 낮이면 일몰
struct SunEvent { bool sunrise = true; time_t at = 0; };
SunEvent nextSunEvent(time_t sunrise, time_t sunset, time_t nowUtc);
const char* cloudSentence(int clouds);           // 하단 문구: "하늘의 87%가 구름으로 덮였어요" 의 앞부분

struct AdviceThresholds {
  float bigDailyRange = 10, heatFeelsLike = 33, coldTemperature = -12, umbrellaPop = 0.6f;
  float strongWind = 14;
};
// 현재 상태에서 가장 중요한 생활 안내 한 줄을 고릅니다.
Advice buildAdvice(const WeatherState& state, time_t nowUtc, const AdviceThresholds& thresholds);
bool airIsFresh(const WeatherState& state, time_t nowUtc);
bool isPrecipitation(uint16_t conditionId);

// ── 페이지 / 터치 ──
int wrapPage(int page);
uint32_t secondsUntil(uint32_t nowMs, uint32_t deadlineMs);
int pageForGesture(int current, int startX, int startY, int endX, int endY);
// 터치 원시값(약 200~3800) → 화면 좌표 아핀 변환. 방향마다 따로 보정해 저장합니다.
struct TouchCalibration {
  float xx = 239 / 3600.0f, xy = 0, xc = -200 * 239 / 3600.0f;
  float yx = 0, yy = 319 / 3600.0f, yc = -200 * 319 / 3600.0f;
};
TouchCalibration defaultTouchCalibration();   // 현재 방향의 보정 전 기본값
// 보정 표적 4곳: 왼쪽 위 → 오른쪽 위 → 오른쪽 아래 → 왼쪽 아래 (가장자리에서 20px)
constexpr int CALIBRATION_MARGIN = 20;
void calibrationTarget(int step, int& x, int& y);
bool fitTouchCalibration(const int raw[4][2], TouchCalibration& result);
void mapTouch(const TouchCalibration& calibration, int rawX, int rawY, int& x, int& y);
// 시스템 화면 버튼 6개 (세로 2열x3줄 / 가로 3열x2줄)
enum SystemButton { BUTTON_REFRESH = 0, BUTTON_AUTO, BUTTON_LOCATION, BUTTON_ROTATE, BUTTON_CALIBRATE, BUTTON_SCHEDULE,
                    SYSTEM_BUTTON_COUNT };
struct Rect { int x = 0, y = 0, w = 0, h = 0; };
Rect systemButtonRect(int index);
int systemButtonAt(int x, int y);      // -1 = 버튼 아님
int systemButtonsTop();                // 버튼 영역 맨 위 y
// 야간 감광 여부 (시작 > 끝 이면 자정을 넘는 구간)
bool isNightHour(int hour, int startHour, int endHour);
