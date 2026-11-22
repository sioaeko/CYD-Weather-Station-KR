#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  CYD 날씨 스테이션 (한국판) 사용자 설정
//  Wi-Fi 비밀번호와 API 키는 Secrets.h 에 넣습니다 (Git 에 올라가지 않음).
// ─────────────────────────────────────────────────────────────────────────────
#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif

// ── 지역 ────────────────────────────────────────────────────────────────────
// 시스템 화면의 [지역 변경] 버튼으로 아래 목록을 순서대로 바꿀 수 있고,
// 선택한 지역은 전원을 꺼도 기억됩니다. 목록을 자유롭게 고치거나 추가해도 됩니다.
// 새 한글 지명을 넣었다면 `python tools/make_korean_fonts.py` 를 다시 실행하세요.
// (화면에 표시할 글자만 골라 폰트를 만들기 때문입니다. 없는 글자는 □ 로 보입니다.)
struct LocationPreset {
  const char* name;       // 화면 표시 이름 (짧을수록 좋음, 한글 최대 5자 권장)
  const char* latitude;   // 위도  (북위, 소수점 4자리면 충분)
  const char* longitude;  // 경도  (동경)
};

inline constexpr LocationPreset LOCATIONS[] = {
  {"서울",   "37.5665", "126.9780"},
  {"인천",   "37.4563", "126.7052"},
  {"수원",   "37.2636", "127.0286"},
  {"안양",   "37.3943", "126.9568"},
  {"의왕",   "37.3449", "126.9683"},
  {"성남",   "37.4200", "127.1267"},
  {"고양",   "37.6584", "126.8320"},
  {"용인",   "37.2411", "127.1776"},
  {"춘천",   "37.8813", "127.7298"},
  {"원주",   "37.3422", "127.9202"},
  {"강릉",   "37.7519", "128.8761"},
  {"청주",   "36.6424", "127.4890"},
  {"천안",   "36.8151", "127.1139"},
  {"세종",   "36.4800", "127.2890"},
  {"대전",   "36.3504", "127.3845"},
  {"전주",   "35.8242", "127.1480"},
  {"광주",   "35.1595", "126.8526"},
  {"목포",   "34.8118", "126.3922"},
  {"여수",   "34.7604", "127.6622"},
  {"대구",   "35.8714", "128.6014"},
  {"안동",   "36.5684", "128.7294"},
  {"포항",   "36.0190", "129.3435"},
  {"울산",   "35.5384", "129.3114"},
  {"창원",   "35.2280", "128.6811"},
  {"부산",   "35.1796", "129.0756"},
  {"제주",   "33.4996", "126.5312"},
  {"서귀포", "33.2541", "126.5600"},
  {"울릉도", "37.4845", "130.9057"},
};
inline constexpr int LOCATION_COUNT = sizeof(LOCATIONS) / sizeof(LOCATIONS[0]);
// 처음 켤 때의 지역 (LOCATIONS 배열 번호, 0 = 서울). 터치로 바꾼 값이 있으면 그 값이 우선입니다.
inline constexpr int DEFAULT_LOCATION_INDEX = 0;

// ── 시간대 ──────────────────────────────────────────────────────────────────
// 한국 표준시 KST = UTC+9, 서머타임 없음. (POSIX 표기는 부호가 반대라서 "KST-9")
inline constexpr char TIMEZONE_POSIX[] = "KST-9";
inline constexpr int32_t LOCAL_UTC_OFFSET_SECONDS = 9 * 3600;
inline constexpr char TIMEZONE_LABEL[] = "KST";
// NTP 서버: 한국표준과학연구원(KRISS) → 국내 풀 → 구글 순서로 시도
inline constexpr char NTP_SERVER_1[] = "time.kriss.re.kr";
inline constexpr char NTP_SERVER_2[] = "kr.pool.ntp.org";
inline constexpr char NTP_SERVER_3[] = "time.google.com";

// ── 갱신 주기 ───────────────────────────────────────────────────────────────
// OpenWeather 무료 요금제: 분당 60회 / 월 100만 회. 15분 주기면 하루 약 200회입니다.
inline constexpr uint32_t UPDATE_INTERVAL_MS = 15UL * 60UL * 1000UL;  // 정상 갱신
inline constexpr uint32_t RETRY_INTERVAL_MS = 60UL * 1000UL;          // 실패 시 재시도
inline constexpr uint32_t AUTO_PAGE_INTERVAL_MS = 8000;               // 자동 넘김 간격 (시스템 화면에서 켬/끔)

// ── 화면 켜짐 / 꺼짐 일정 ───────────────────────────────────────────────────
// 기본값: 아침 8시에 화면이 켜지고, 밤 11시에 꺼집니다 (KST, 인터넷 시간 기준).
// 꺼져 있는 동안에도 날씨는 계속 갱신되며, 화면을 한 번 터치하면 30초 동안 켜집니다.
// 끄지 않고 어둡게만 하려면 NIGHT_BRIGHTNESS 를 20~40 정도로 바꾸세요.
// 일정을 아예 쓰지 않으려면 SCREEN_ON_HOUR 와 SCREEN_OFF_HOUR 를 같은 값으로 두세요.
inline constexpr uint8_t SCREEN_ON_HOUR = 8;      // 이 시각에 켜짐 (기상 시간)
inline constexpr uint8_t SCREEN_OFF_HOUR = 23;    // 이 시각에 꺼짐 (취침 시간)
inline constexpr uint8_t DAY_BRIGHTNESS = 255;    // 켜져 있을 때 밝기 0~255
inline constexpr uint8_t NIGHT_BRIGHTNESS = 0;    // 밤 밝기 0~255 (0 = 완전히 꺼짐)
inline constexpr uint32_t WAKE_BOOST_MS = 30000;  // 밤에 터치하면 켜 둘 시간 (밀리초)

// ── 생활 안내 기준 (기상청·환경부 기준을 참고한 값) ─────────────────────────
inline constexpr float BIG_DAILY_RANGE_C = 10.0f;     // 일교차 10℃ 이상이면 안내
inline constexpr float HEAT_FEELS_LIKE_C = 33.0f;     // 체감 33℃ 이상: 폭염 주의
inline constexpr float COLD_TEMPERATURE_C = -12.0f;   // 기온 -12℃ 이하: 한파 주의
inline constexpr float UMBRELLA_POP = 0.6f;           // 강수확률 60% 이상: 우산 안내

// ── 하드웨어 (ESP32-2432S028R / CYD 기본값, 보통 고칠 필요 없음) ─────────────
// ── 화면 방향 ── 시스템 화면의 [가로로 회전]/[세로로 회전] 버튼으로 바꿀 수 있고, 선택은 기억됩니다.
// 화면 색 반전: 바탕이 하얗고 색이 필름처럼 반대로 보이면 반대 값으로 바꾸세요. (설정 웹에서도 바꿀 수 있음)
//   USB 단자가 2개(USB-C + Micro-USB)인 후기형 CYD → true,  Micro-USB 하나뿐인 초기형 CYD → false
inline constexpr bool DEFAULT_INVERT_COLORS = true;
inline constexpr bool DEFAULT_LANDSCAPE = true;    // 처음 켤 때: true = 가로(머리맡 추천, 큰 시계), false = 세로
// TFT_eSPI 회전값. 기기를 거꾸로 세웠다면 설정 웹의 "화면 180° 뒤집기" 를 켜면 됩니다 (터치도 같이 뒤집힘).
inline constexpr uint8_t PORTRAIT_ROTATION = 0;
inline constexpr uint8_t LANDSCAPE_ROTATION = 1;
// 터치 패널 회전값은 고정해 두고, 방향마다 터치 보정값을 따로 저장해서 맞춥니다.
inline constexpr uint8_t TOUCH_ROTATION = 1;
inline constexpr int TOUCH_CS_PIN = 33;
inline constexpr int TOUCH_IRQ_PIN = 36;
inline constexpr int TOUCH_CLK_PIN = 25;
inline constexpr int TOUCH_MISO_PIN = 39;
inline constexpr int TOUCH_MOSI_PIN = 32;
inline constexpr int BACKLIGHT_PIN = 21;
