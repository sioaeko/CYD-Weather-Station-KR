#pragma once
// 내부망 설정 웹 (http://cyd-weather-kr.local 또는 기기 IP).
// Wi-Fi 정보가 없거나 연결에 실패하면 기기가 직접 "CYD-Weather-XXXX" Wi-Fi 를 띄우는
// 설정 모드(캡티브 포털)로 들어가고, 휴대폰으로 접속해 공유기 정보를 입력할 수 있습니다.
#include <Arduino.h>

constexpr char WEB_HOSTNAME[] = "cyd-weather-kr";
void startWebPortal();
void handleWebPortal();          // loop() 에서 자주 호출
bool webPortalApMode();          // 설정 모드(기기 자체 Wi-Fi)가 켜져 있으면 true
const char* webPortalApName();   // 설정 모드 Wi-Fi 이름
