#pragma once
// 이 파일을 같은 폴더에 Secrets.h 라는 이름으로 복사한 뒤 값을 채우세요.
// Secrets.h 는 .gitignore 에 들어 있어 GitHub 에 올라가지 않습니다.

// ESP32 는 2.4GHz Wi-Fi 만 지원합니다. 공유기 이름이 "_5G" 로 끝나는 망은 안 됩니다.
// (예: "iptime" 은 되고 "iptime_5G" 는 안 됨)
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// https://home.openweathermap.org/api_keys 에서 발급한 32자리 키.
// 새로 만든 키는 활성화까지 최대 2시간쯤 걸립니다 (그동안 HTTP 401).
inline constexpr char OPENWEATHER_KEYS[][33] = {
  "YOUR_OPENWEATHER_API_KEY",
  "" // 예비 키 (선택). 첫 번째 키가 401/403/429 로 거절될 때만 사용합니다.
};
