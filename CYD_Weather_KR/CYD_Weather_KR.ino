/*
 * CYD 날씨 스테이션 – 한국판 (ESP32-2432S028R, 가로 320x240 / 세로 240x320, 터치)
 *
 * 원작: Rau7han/ESP32-CYD-Weather-Station v3.1
 *       (Daniel Eichhorn / ThingPulse 날씨 스테이션, Bodmer 의 TFT_eSPI·LittleFS 이식 기반)
 * 한국판 변경: 한국어 UI·한글 글꼴, KST·국내 NTP, 전국 지역 선택, 미세먼지/초미세먼지,
 *             기상청·환경부 등급 표기, 생활 안내, 강수확률, 체감온도, 화면 켜짐/꺼짐 일정,
 *             가로·세로 화면, 내부망 설정 웹 + Wi-Fi 설정 모드.
 * MIT 고지: THIRD_PARTY_NOTICES.txt
 */
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <XPT2046_Touchscreen.h>
#include "All_Settings.h"
#include "DeviceSettings.h"
#include "WeatherModel.h"
#include "WeatherNetwork.h"
#include "NTP_Time.h"
#include "DisplayUI.h"
#include "WebPortal.h"
#include <esp_wifi.h>

#ifndef ARDUINO_PARTITION_no_ota
#error "In Arduino IDE Tools > Partition Scheme select No OTA (2MB APP/2MB SPIFFS), then upload both the sketch and LittleFS data."
#endif

SPIClass touchSPI(VSPI);
// Poll pressure: IRQ gating is unreliable on some CYD boards.
XPT2046_Touchscreen touchPanel(TOUCH_CS_PIN, 255);
Preferences touchPreferences;   // 터치 보정값 (방향별)
TouchCalibration touchCalibration;
DeviceSettings settings;        // 지역 · 화면 일정 · 밝기 · 방향 · 자동 넘김 · Wi-Fi · 관리 비밀번호
WeatherState weather;
int page = 0;
bool dirty = true, wasRefreshing = false, wasApMode = false, touchReady = false;
uint32_t lastPageChange = 0, lastClockTick = 0;
int lastMinute = -1;
// 화면 일정: 밤(꺼짐 시각 ~ 켜짐 시각)에는 밤 밝기로, 터치하면 WAKE_BOOST_MS 동안 낮 밝기
uint32_t lastTouchMs = 0; bool touchedOnce = false; int appliedBrightness = -1;

bool calibrating = false, calibrationWaitRelease = false, calibrationFailed = false, calibrationDrawPending = false;
int calibrationStep = 0, calibrationRaw[4][2] = {};
uint32_t calibrationStarted = 0, calibrationPressStart = 0;
long calibrationSumX = 0, calibrationSumY = 0;
int calibrationSamples = 0;
bool fingerDown = false;
int startX = 0, startY = 0, endX = 0, endY = 0;
uint32_t fingerStart = 0, lastFingerSeen = 0;

void changePage(int next) {
  page = wrapPage(next); dirty = true; lastPageChange = millis();
  Serial.printf("PAGE %d %s\n", page, page == 0 ? "Home" : page == 1 ? "Forecast" : page == 2 ? "Conditions" : "System");
}
bool nightNow() {
  return settings.scheduleOn && clockValid() && isNightHour(localHour(time(nullptr)), settings.offHour, settings.onHour);
}
bool boosted() { return touchedOnce && millis() - lastTouchMs < WAKE_BOOST_MS; }
// 백라이트: 최대 밝기와 꺼짐은 핀을 직접 켜고 끄고, 중간 밝기일 때만 PWM 을 붙입니다.
// (USB 단자가 2개인 CYD 에서는 부팅 초반에 붙인 PWM 이 백라이트를 켜지 못해 화면이 검게 남는 문제가 있었음)
bool backlightPwm = false;
void setBacklight(int level) {
  if (level <= 0 || level >= 255) {
    if (backlightPwm) { ledcDetach(BACKLIGHT_PIN); backlightPwm = false; }
    pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, level > 0 ? HIGH : LOW);
    return;
  }
  if (!backlightPwm) backlightPwm = ledcAttach(BACKLIGHT_PIN, 5000, 8);
  if (backlightPwm) ledcWrite(BACKLIGHT_PIN, level);
  else { pinMode(BACKLIGHT_PIN, OUTPUT); digitalWrite(BACKLIGHT_PIN, HIGH); }  // PWM 을 못 쓰면 밝게라도
}
void updateBacklight() {
  const int target = nightNow() && !boosted() ? settings.nightBrightness : settings.dayBrightness;
  if (target == appliedBrightness) return;
  appliedBrightness = target; setBacklight(target);
  Serial.printf("BACKLIGHT %d%s\n", target, backlightPwm ? " (pwm)" : "");
}

// ── 터치 보정 (방향마다 따로 저장) ──
const char* calibrationKey() { return settings.landscape ? "touch_l" : "touch_p"; }
bool loadCalibration() {
  if (touchPreferences.getBytesLength(calibrationKey()) == sizeof(TouchCalibration)) {
    touchPreferences.getBytes(calibrationKey(), &touchCalibration, sizeof(touchCalibration));
    if (settings.flipped) touchCalibration = flipCalibration(touchCalibration);
    return true;
  }
  touchCalibration = defaultTouchCalibration();
  if (settings.flipped) touchCalibration = flipCalibration(touchCalibration);
  return false;
}
void startCalibration() {
  calibrating = true; calibrationStep = 0; calibrationStarted = millis();
  calibrationWaitRelease = true; calibrationFailed = false;
  calibrationSamples = 0; calibrationPressStart = 0; fingerDown = false;
  lastTouchMs = millis(); touchedOnce = true; updateBacklight();  // 밤이어도 보정하는 동안은 밝게
  calibrationDrawPending = !renderCalibration(0); Serial.println("CALIBRATION started");
}

// ── 설정 적용: 터치 버튼과 설정 웹이 함께 씁니다 (DeviceSettings.h) ──
const DeviceSettings& currentSettings() { return settings; }
void applySettings(const DeviceSettings& next) {
  const DeviceSettings previous = settings;
  settings = next; saveSettings(settings);
  const Place before = resolvePlace(previous), after = resolvePlace(settings);
  if (!samePlace(before, after)) {
    setWeatherPlace(after);
    Serial.printf("LOCATION %s (%s, %s)\n", after.name, after.latitude, after.longitude);
  }
  if (previous.landscape != settings.landscape || previous.flipped != settings.flipped) {
    if (!setDisplayOrientation(settings.landscape, settings.flipped)) { Serial.println("FATAL: screen buffer reallocation failed"); ESP.restart(); }
    Serial.printf("ORIENTATION %s%s\n", settings.landscape ? "landscape 320x240" : "portrait 240x320", settings.flipped ? " flipped" : "");
    if (previous.landscape != settings.landscape) { if (!loadCalibration()) startCalibration(); }  // 이 방향으로 처음 돌렸다면 보정부터
    else touchCalibration = flipCalibration(touchCalibration);  // 뒤집기만 바뀌면 보정값도 같이 뒤집음
    if (calibrating) calibrationDrawPending = true;
  }
  if (previous.autoCycle != settings.autoCycle) lastPageChange = millis();
  if (previous.invertColors != settings.invertColors) setDisplayInvert(settings.invertColors);
  if (strcmp(previous.apiKey, settings.apiKey)) setWeatherApiKey(settings.apiKey);
  appliedBrightness = -1; updateBacklight();   // 밝기 · 일정 변경을 바로 반영
  dirty = true;
}
void requestTouchCalibration() { startCalibration(); }

void cycleLocation() {
  DeviceSettings next = settings;
  next.presetIndex = settings.presetIndex < 0 ? 0 : (settings.presetIndex + 1) % LOCATION_COUNT;
  applySettings(next);
}
void handleGesture(int x0, int y0, int x1, int y1) {
  lastPageChange = millis();
  if (page == 3 && abs(x1 - x0) < 20 && abs(y1 - y0) < 20) {
    DeviceSettings next = settings;
    switch (systemButtonAt(x1, y1)) {
      case BUTTON_REFRESH: requestWeatherRefresh(); dirty = true; return;
      case BUTTON_AUTO: next.autoCycle = !next.autoCycle; applySettings(next); return;
      case BUTTON_LOCATION: cycleLocation(); return;
      case BUTTON_ROTATE: next.landscape = !next.landscape; applySettings(next); return;
      case BUTTON_CALIBRATE: startCalibration(); return;
      case BUTTON_SCHEDULE: next.scheduleOn = !next.scheduleOn; applySettings(next); return;
      default: if (y1 >= systemButtonsTop() - 4 && y1 < screenHeight() - 10) return;  // 버튼 사이 빈 곳은 무시
    }
  }
  changePage(pageForGesture(page, x0, y0, x1, y1));
}
void calibrateSample(bool pressed, const TS_Point& raw) {
  if (millis() - calibrationStarted > 90000) {
    calibrating = false; dirty = true; lastPageChange = millis(); Serial.println("CALIBRATION timed out"); return;
  }
  if (calibrationWaitRelease) {
    if (!pressed) {
      calibrationWaitRelease = false; calibrationSamples = 0; calibrationPressStart = 0;
      calibrationSumX = calibrationSumY = 0;
      if (calibrationStep == 4) {
        TouchCalibration result;
        if (fitTouchCalibration(calibrationRaw, result)) {
          touchCalibration = result;
          const TouchCalibration stored = settings.flipped ? flipCalibration(result) : result;
          touchPreferences.putBytes(calibrationKey(), &stored, sizeof(stored));
          calibrating = false; dirty = true; lastPageChange = millis(); Serial.println("CALIBRATION saved"); return;
        }
        calibrationStep = 0; calibrationFailed = true; Serial.println("CALIBRATION rejected; retry");
      }
      calibrationDrawPending = !renderCalibration(calibrationStep, calibrationFailed);
    }
    return;
  }
  if (!pressed) {
    calibrationSamples = 0; calibrationPressStart = 0; calibrationSumX = calibrationSumY = 0; return;
  }
  if (!calibrationSamples) calibrationPressStart = millis();
  calibrationSumX += raw.x; calibrationSumY += raw.y; ++calibrationSamples;
  if (millis() - calibrationPressStart >= 350 && calibrationSamples >= 8) {
    calibrationRaw[calibrationStep][0] = calibrationSumX / calibrationSamples;
    calibrationRaw[calibrationStep][1] = calibrationSumY / calibrationSamples;
    Serial.printf("CALIBRATION point=%d raw=%d,%d\n", calibrationStep,
                  calibrationRaw[calibrationStep][0], calibrationRaw[calibrationStep][1]);
    ++calibrationStep; calibrationWaitRelease = true;
  }
}
void pollTouch() {
  if (!touchReady) return;
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 12) return;
  lastPoll = millis();
  TS_Point raw; bool pressed = touchPanel.touched();
  if (pressed) { raw = touchPanel.getPoint(); pressed = raw.z >= 200; }
  if (calibrating) { calibrateSample(pressed, raw); return; }
  if (pressed) {
    int x, y; mapTouch(touchCalibration, raw.x, raw.y, x, y);
    lastFingerSeen = millis(); endX = x; endY = y;
    if (!fingerDown) { fingerDown = true; fingerStart = millis(); startX = x; startY = y; }
    if (millis() - fingerStart > 2000 && abs(endX - startX) < 20 && abs(endY - startY) < 20) startCalibration();
  } else if (fingerDown && millis() - lastFingerSeen > 70) {
    fingerDown = false;
    // 밤에 꺼진(어두운) 화면을 터치하면 먼저 켜기만 하고, 페이지는 넘기지 않습니다.
    const bool wakeOnly = nightNow() && !boosted() && settings.nightBrightness < settings.dayBrightness;
    lastTouchMs = millis(); touchedOnce = true;
    if (wakeOnly) { lastPageChange = millis(); updateBacklight(); Serial.println("TOUCH wake"); return; }
    if (millis() - fingerStart >= 35) {
      Serial.printf("TOUCH %d,%d -> %d,%d\n", startX, startY, endX, endY);
      handleGesture(startX, startY, endX, endY);
    }
  }
}
UiState uiState(bool refreshing) {
  UiState ui;
  ui.autoCycle = settings.autoCycle; ui.refreshing = refreshing; ui.scheduleOn = settings.scheduleOn;
  ui.onHour = settings.onHour; ui.offHour = settings.offHour; ui.nightBrightness = settings.nightBrightness;
  ui.hour12 = settings.hour12;
  ui.apMode = webPortalApMode(); strlcpy(ui.apName, webPortalApName(), sizeof(ui.apName));
  return ui;
}
void setup() {
  Serial.begin(115200); delay(200);
  Serial.println("\nCYD Weather KR v1.2 (based on v3.1) / landscape+portrait / KST / touch / web settings");
  loadSettings(settings);
  touchPreferences.begin("cyd-kr-v1", false);
  if (!beginDisplay(settings.landscape, settings.flipped)) { Serial.println("FATAL: screen buffer allocation failed"); while (true) delay(1000); }
  setDisplayInvert(settings.invertColors);
  appliedBrightness = settings.dayBrightness; setBacklight(appliedBrightness);
  if (!LittleFS.begin(false)) {
    showMessage("LittleFS 데이터가 없습니다", "data 폴더를 먼저 업로드하세요", TFT_RED);
    Serial.println("FATAL: LittleFS mount failed"); while (true) delay(1000);
  }
  Serial.printf("Display setup=%d backlight=%d size=%dx%d\n", USER_SETUP_ID, TFT_BL, tft.width(), tft.height());
  const Place place = resolvePlace(settings);
  auditAssets(); showBootSplash(place.name);
  pinMode(TOUCH_CS_PIN, OUTPUT); digitalWrite(TOUCH_CS_PIN, HIGH); pinMode(TOUCH_IRQ_PIN, INPUT);
  touchSPI.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touchReady = touchPanel.begin(touchSPI); touchPanel.setRotation(TOUCH_ROTATION);
  const bool calibrated = loadCalibration();
  Serial.printf("TOUCH ready=%d / own SPI bus / long press to calibrate\n", touchReady);
  const bool configured = wifiConfigured(settings);
  WiFi.setHostname(WEB_HOSTNAME);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
  // 채널 폭을 20MHz 로 고정: 신호가 약할 때(-75dBm 안팎) 40MHz 폭에서는 큰 패킷이 깨져서
  // 설정 웹 페이지가 끝까지 내려가지 않았음 (2026-11 실측: HT20 + 최대 출력에서 1400바이트 손실 0%).
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  if (configured) WiFi.begin(settings.wifiSsid, settings.wifiPassword);
  setWeatherApiKey(settings.apiKey);
  initClock(); startWeatherNetwork(place, configured); startWebPortal();
  Serial.printf("LOCATION %s (%s, %s) / Wi-Fi %s\n", place.name, place.latitude, place.longitude,
                configured ? settings.wifiSsid : "(설정 필요)");
  delay(600); lastPageChange = millis(); dirty = true;
  // 이 방향의 보정값이 없으면(처음 켰을 때) 터치 보정부터 진행합니다.
  if (!calibrated && touchReady) startCalibration();
}
void loop() {
  pollTouch();
  handleWebPortal();
  WeatherState latest; copyWeatherState(latest);
  if (latest.generation != weather.generation) {
    weather = latest; dirty = true;
    Serial.printf("WEATHER current=%d forecast=%d air=%d / pm10=%.0f pm2.5=%.0f uv=%.1f / %s / heap=%u\n",
                  weather.currentHttp, weather.forecastHttp, weather.airHttp, weather.pm10, weather.pm25, weather.uv,
                  weather.status, ESP.getFreeHeap());
  }
  const bool refreshing = weatherIsRefreshing();
  if (refreshing != wasRefreshing) { wasRefreshing = refreshing; dirty = true; }
  if (webPortalApMode() != wasApMode) { wasApMode = webPortalApMode(); dirty = true; }
  if (millis() - lastClockTick >= 1000) {
    lastClockTick = millis();
    const time_t stamp = time(nullptr); const int minute = (stamp / 60) % 60;
    if (page == 3 || (page == 0 && minute != lastMinute)) dirty = true;
    lastMinute = minute;
    updateBacklight();
  }
  if (!calibrating && !fingerDown && settings.autoCycle && millis() - lastPageChange >= AUTO_PAGE_INTERVAL_MS) changePage(page + 1);
  if (dirty && !calibrating) {
    const UiState ui = uiState(refreshing);
    // 설정 모드이고 아직 날씨가 없으면 휴대폰 연결 안내 화면을 보여줍니다.
    // 날씨를 받는 중이라 화면 버퍼를 못 잡으면 dirty 를 남겨 두고 다음 loop 에서 다시 그립니다.
    const bool drawn = ui.apMode && !weather.current.valid ? renderSetupScreen(ui) : renderPage(weather, page, ui);
    dirty = !drawn;
  }
  if (calibrating && calibrationDrawPending) calibrationDrawPending = !renderCalibration(calibrationStep, calibrationFailed);
  delay(3);
}
