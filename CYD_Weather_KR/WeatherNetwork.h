#pragma once
#include "WeatherModel.h"
// 내부 오류 코드 (HTTPClient 의 음수 코드와 겹치지 않게 -100 부터 사용)
constexpr int NET_WIFI_OFFLINE = -100, NET_CLOCK_NOT_SET = -101, NET_BEGIN_FAILED = -102;
constexpr int NET_JSON_ERROR = -103, NET_BAD_DATA = -104, NET_NO_MEMORY = -105;
// configured = false 면 Wi-Fi 정보가 없어서 설정 웹에서 입력할 때까지 날씨를 받지 않습니다.
void startWeatherNetwork(const Place& place, bool configured);
void setWeatherWifiConfigured(bool configured);
bool requestWeatherRefresh();
bool weatherIsRefreshing();
uint32_t weatherNextRefreshSeconds();
void copyWeatherState(WeatherState& destination);
// 지역을 바꾸면 이전 지역 데이터는 지우고 즉시 새로 받습니다.
void setWeatherPlace(const Place& place);
const char* httpStatusText(int code);

