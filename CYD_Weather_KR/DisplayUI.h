#pragma once
#include <TFT_eSPI.h>
#if !defined(USER_SETUP_ID) || (USER_SETUP_ID != 801 && USER_SETUP_ID != 805)
#error "TFT_eSPI 가 CYD 용으로 설정되지 않았습니다. libraries/ 의 TFT_eSPI-2.5.44.zip 을 설치하세요 (README 2단계)."
#endif
#ifndef SMOOTH_FONT
#error "TFT_eSPI 의 User_Setup.h 에서 #define SMOOTH_FONT 가 필요합니다 (한글 글꼴)."
#endif
#include "WeatherModel.h"
extern TFT_eSPI tft;
// 화면에 같이 그리는 기기 상태
struct UiState {
  bool autoCycle = false, refreshing = false, scheduleOn = true;
  uint8_t onHour = 8, offHour = 23, nightBrightness = 0;
  bool hour12 = false;          // 큰 시계 12시간제
  bool apMode = false;          // Wi-Fi 설정 모드(기기 자체 Wi-Fi) 켜짐
  char apName[24] = "";
};
bool beginDisplay(bool landscape, bool flipped);
// 화면 방향 바꾸기 (가로/세로가 바뀌면 화면 버퍼를 다시 만듦). flipped = 180° 뒤집기
bool setDisplayOrientation(bool landscape, bool flipped);
void setDisplayInvert(bool invert);        // 패널 색 반전 (INVON/INVOFF)
// 반 화면 버퍼로 위·아래를 나눠 그립니다. false = 버퍼를 못 만듦 (잠시 뒤 다시 호출)
bool renderPage(const WeatherState& weather, int page, const UiState& ui);
bool renderCalibration(int step, bool failed = false);
void showBootSplash(const char* placeName);
bool renderSetupScreen(const UiState& ui);   // Wi-Fi 설정 모드 안내 화면
// 기기 화면 글꼴(16px)에 없는 글자만 모아 돌려줍니다. 설정 웹에서 지역 이름을 확인할 때 씁니다.
String missingGlyphs(const char* utf8);
void showMessage(const char* line1, const char* line2, uint16_t color);
void auditAssets();
