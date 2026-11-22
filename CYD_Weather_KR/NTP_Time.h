#pragma once
#include <Arduino.h>
#include <time.h>
#include "All_Settings.h"
inline bool clockValid() { return time(nullptr) > 1700000000; }
inline void initClock() {
  // POSIX 표기는 부호가 반대: "KST-9" 는 UTC+9, 서머타임 없음.
  configTzTime(TIMEZONE_POSIX, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}
inline String localTimeText(time_t utc, const char* format = "%H:%M") {
  if (utc <= 0) return "--:--";
  struct tm local; localtime_r(&utc, &local);
  char output[40]; strftime(output, sizeof(output), format, &local);
  return String(output);
}
inline int localHour(time_t utc) {
  struct tm local; localtime_r(&utc, &local); return local.tm_hour;
}
