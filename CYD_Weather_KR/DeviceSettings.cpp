#include "DeviceSettings.h"
#include "All_Settings.h"
#include <Preferences.h>

namespace {
constexpr char NAMESPACE[] = "cyd-kr-set";
void copyText(char* out, size_t size, const String& value) { strlcpy(out, value.c_str(), size); }
}

void loadSettings(DeviceSettings& s) {
  s = DeviceSettings{};
  // 기본값: All_Settings.h · Secrets.h
  s.presetIndex = DEFAULT_LOCATION_INDEX;
  s.onHour = SCREEN_ON_HOUR; s.offHour = SCREEN_OFF_HOUR;
  s.dayBrightness = DAY_BRIGHTNESS; s.nightBrightness = NIGHT_BRIGHTNESS;
  s.landscape = DEFAULT_LANDSCAPE; s.invertColors = DEFAULT_INVERT_COLORS;
  strlcpy(s.wifiSsid, WIFI_SSID, sizeof(s.wifiSsid));
  strlcpy(s.wifiPassword, WIFI_PASSWORD, sizeof(s.wifiPassword));

  Preferences p;
  if (!p.begin(NAMESPACE, true)) return;  // 아직 저장된 값이 없음
  s.presetIndex = p.getChar("preset", s.presetIndex);
  if (s.presetIndex < -1 || s.presetIndex >= LOCATION_COUNT) s.presetIndex = DEFAULT_LOCATION_INDEX;
  copyText(s.custom.name, sizeof(s.custom.name), p.getString("cName", s.custom.name));
  copyText(s.custom.latitude, sizeof(s.custom.latitude), p.getString("cLat", s.custom.latitude));
  copyText(s.custom.longitude, sizeof(s.custom.longitude), p.getString("cLon", s.custom.longitude));
  s.onHour = p.getUChar("onH", s.onHour) % 24;
  s.offHour = p.getUChar("offH", s.offHour) % 24;
  s.dayBrightness = p.getUChar("dayB", s.dayBrightness);
  s.nightBrightness = p.getUChar("nightB", s.nightBrightness);
  s.scheduleOn = p.getBool("sched", s.scheduleOn);
  s.landscape = p.getBool("land", s.landscape);
  s.autoCycle = p.getBool("auto", s.autoCycle);
  s.invertColors = p.getBool("inv", s.invertColors);
  if (p.isKey("ssid")) {
    copyText(s.wifiSsid, sizeof(s.wifiSsid), p.getString("ssid", ""));
    copyText(s.wifiPassword, sizeof(s.wifiPassword), p.getString("pass", ""));
  }
  copyText(s.adminPassword, sizeof(s.adminPassword), p.getString("admin", ""));
  p.end();
}

void saveSettings(const DeviceSettings& s) {
  Preferences p;
  if (!p.begin(NAMESPACE, false)) { Serial.println("SETTINGS save failed"); return; }
  // put* 는 값이 같으면 플래시에 다시 쓰지 않습니다.
  p.putChar("preset", s.presetIndex);
  p.putString("cName", s.custom.name); p.putString("cLat", s.custom.latitude); p.putString("cLon", s.custom.longitude);
  p.putUChar("onH", s.onHour); p.putUChar("offH", s.offHour);
  p.putUChar("dayB", s.dayBrightness); p.putUChar("nightB", s.nightBrightness);
  p.putBool("sched", s.scheduleOn); p.putBool("land", s.landscape); p.putBool("auto", s.autoCycle); p.putBool("inv", s.invertColors);
  p.putString("ssid", s.wifiSsid); p.putString("pass", s.wifiPassword);
  p.putString("admin", s.adminPassword);
  p.end();
}

void resetSettings() {
  Preferences p;
  if (p.begin(NAMESPACE, false)) { p.clear(); p.end(); }
}

Place resolvePlace(const DeviceSettings& s) {
  if (s.presetIndex < 0 && s.custom.latitude[0] && s.custom.longitude[0]) return s.custom;
  const LocationPreset& preset = LOCATIONS[s.presetIndex < 0 ? 0 : s.presetIndex];
  Place place;
  strlcpy(place.name, preset.name, sizeof(place.name));
  strlcpy(place.latitude, preset.latitude, sizeof(place.latitude));
  strlcpy(place.longitude, preset.longitude, sizeof(place.longitude));
  return place;
}

bool wifiConfigured(const DeviceSettings& s) {
  return s.wifiSsid[0] && strcmp(s.wifiSsid, "YOUR_WIFI_SSID") != 0;
}

bool samePlace(const Place& a, const Place& b) {
  return !strcmp(a.name, b.name) && !strcmp(a.latitude, b.latitude) && !strcmp(a.longitude, b.longitude);
}
