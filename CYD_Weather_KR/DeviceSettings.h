#pragma once
// 실행 중에 바꿀 수 있는 기기 설정 (터치 버튼 · 설정 웹). ESP32 NVS 에 저장되어 전원을 꺼도 유지됩니다.
// 처음 켤 때의 기본값은 All_Settings.h / Secrets.h 에서 가져옵니다.
#include <Arduino.h>
#include "WeatherModel.h"

struct DeviceSettings {
  int8_t presetIndex = 0;          // LOCATIONS 번호, -1 = 직접 입력(custom)
  Place custom;                    // 직접 입력한 지역
  uint8_t onHour = 8, offHour = 23;
  uint8_t dayBrightness = 255, nightBrightness = 0;
  bool scheduleOn = true, landscape = true, autoCycle = false;
  bool invertColors = true;        // 패널 색 반전 (후기형 CYD 는 켜야 정상 색)
  bool flipped = false;            // 화면 180° 뒤집기 (기기를 거꾸로 세웠을 때)
  bool hour12 = false;             // 큰 시계를 12시간제(오전/오후)로
  char wifiSsid[33] = "", wifiPassword[65] = "";
  char adminPassword[33] = "";     // 비어 있으면 설정 웹에 비밀번호 없음
  char apiKey[33] = "";            // OpenWeather API 키 (설정 웹에서 입력, 없으면 Secrets.h 의 첫 번째 키)
};

void loadSettings(DeviceSettings& settings);
void saveSettings(const DeviceSettings& settings);
void resetSettings();                                 // 저장된 설정 모두 지우기 (기본값으로)
Place resolvePlace(const DeviceSettings& settings);   // 프리셋 또는 직접 입력한 지역
bool wifiConfigured(const DeviceSettings& settings);  // SSID 가 있고 예시 값이 아니면 true
bool validApiKey(const char* key);                    // 32자리 16진수면 true
bool samePlace(const Place& a, const Place& b);

// ── CYD_Weather_KR.ino 에 구현: 설정 웹과 터치 버튼이 공통으로 씁니다 ──
const DeviceSettings& currentSettings();
void applySettings(const DeviceSettings& next);       // 바뀐 부분만 기기에 적용하고 저장
void requestTouchCalibration();
