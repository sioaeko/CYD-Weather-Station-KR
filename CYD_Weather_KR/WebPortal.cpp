// WebServer.h 는 전역 FS 이름이 필요한데 TFT_eSPI 가 FS_NO_GLOBALS 를 정의하므로 웹 헤더를 먼저 포함합니다.
#include <FS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif_net_stack.h>
#include <lwip/netif.h>
#include <ArduinoJson.h>
#include <math.h>
#include "WebPortal.h"
#include "All_Settings.h"
#include "DeviceSettings.h"
#include "DisplayUI.h"
#include "NTP_Time.h"
#include "WeatherNetwork.h"
#include "WebPage.h"

namespace {
WebServer server(80);
DNSServer dns;
bool apOn = false, mdnsStarted = false, restartPending = false;
char apName[24] = "";
uint32_t lastConnectedMs = 0, connectedSinceMs = 0, restartAt = 0;
constexpr uint32_t AP_AFTER_FAIL_MS = 30000;      // 이 시간 동안 연결 못 하면 설정 모드
constexpr uint32_t AP_KEEP_AFTER_JOIN_MS = 180000; // 연결 성공 후 설정 모드를 유지할 시간 (새 IP 안내용)
const char* FIRMWARE = "KR 1.2";
// 신호가 약하면(-80dBm 안팎) 1400바이트 프레임은 절반쯤 깨지지만 1000바이트 이하는 거의 다 통과합니다.
// MTU 를 576 으로 낮춰 TCP 세그먼트를 536바이트로 만들면 설정 웹 응답이 끊기지 않습니다.
constexpr uint16_t STA_MTU = 576;
bool linkLogged = false;  // 연결될 때마다 신호 세기를 한 번 기록하고 MTU 를 맞춤
void onLinkUp() {
  linkLogged = true;
  esp_netif_t* nif = WiFi.STA.netif();
  struct netif* n = nif ? static_cast<struct netif*>(esp_netif_get_netif_impl(nif)) : nullptr;
  if (n && n->mtu > STA_MTU) n->mtu = STA_MTU;
  Serial.printf("WIFI connected rssi=%d dBm ch=%d ip=%s mtu=%u\n", WiFi.RSSI(), WiFi.channel(),
                WiFi.localIP().toString().c_str(), n ? n->mtu : 0);
}

// ── 응답 도우미 ──
void sendJson(int code, JsonDocument& doc) {
  String body; serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}
void sendError(int code, const char* error, const char* message, const char* field = nullptr) {
  JsonDocument doc; doc["error"] = error; doc["message"] = message;
  if (field) doc["field"] = field;
  sendJson(code, doc);
}
// 관리 비밀번호: 설정했다면 모든 /api 요청에 X-Admin-Password 헤더가 맞아야 합니다.
bool constantTimeEqual(const char* a, const char* b) {
  const size_t la = strlen(a), lb = strlen(b); uint8_t diff = la != lb;
  for (size_t i = 0; i < la && i < lb; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}
bool authorized() {
  const char* admin = currentSettings().adminPassword;
  if (!admin[0]) return true;
  if (constantTimeEqual(server.header("X-Admin-Password").c_str(), admin)) return true;
  sendError(401, "auth", "관리 비밀번호가 필요합니다");
  return false;
}
bool readBody(JsonDocument& doc) {
  if (!server.hasArg("plain") || deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "json", "요청 형식이 올바르지 않습니다"); return false;
  }
  return true;
}

// ── 상태 ──
void stateJson(JsonDocument& doc) {
  const DeviceSettings& s = currentSettings();
  WeatherState w; copyWeatherState(w);
  const time_t now = time(nullptr);
  JsonObject d = doc["device"].to<JsonObject>();
  d["firmware"] = FIRMWARE; d["uptime"] = millis() / 1000; d["heapKb"] = ESP.getFreeHeap() / 1024;
  d["host"] = String(WEB_HOSTNAME) + ".local";
  const bool online = WiFi.status() == WL_CONNECTED;
  d["online"] = online;
  d["ip"] = online ? WiFi.localIP().toString() : String("");
  d["ssid"] = online ? WiFi.SSID() : String("");
  d["rssi"] = online ? WiFi.RSSI() : 0;
  d["channel"] = online ? WiFi.channel() : 0;
  d["ap"] = apOn; d["apName"] = apName; d["apIp"] = apOn ? WiFi.softAPIP().toString() : String("");
  d["time"] = clockValid() ? static_cast<long long>(now) : 0;
  d["utcOffset"] = LOCAL_UTC_OFFSET_SECONDS;

  JsonObject x = doc["weather"].to<JsonObject>();
  x["place"] = w.place.name; x["lat"] = w.place.latitude; x["lon"] = w.place.longitude;
  x["valid"] = w.current.valid;
  if (w.current.valid) {
    const WeatherPoint& p = w.current.point;
    x["temp"] = roundf(p.temperature * 10) / 10; x["feels"] = roundf(p.feelsLike * 10) / 10;
    x["condition"] = koreanWeather(p.conditionId); x["humidity"] = p.humidity;
    x["wind"] = roundf(p.wind * 10) / 10; x["windDir"] = koreanWindDirection(p.windDegrees);
    x["sunrise"] = static_cast<long long>(w.current.sunrise); x["sunset"] = static_cast<long long>(w.current.sunset);
    x["updated"] = static_cast<long long>(w.current.fetched);
  }
  if (airIsFresh(w, now)) {
    x["pm10"] = lroundf(w.pm10); x["pm25"] = lroundf(w.pm25); x["uv"] = roundf(w.uv * 10) / 10;
    const int g10 = pm10Grade(w.pm10), g25 = pm25Grade(w.pm25);
    x["dustGrade"] = g25 > g10 ? g25 : g10;
  }
  if (w.outlook.valid) x["pop"] = lroundf(w.outlook.pop12h * 100);
  x["status"] = w.status; x["refreshing"] = weatherIsRefreshing(); x["next"] = weatherNextRefreshSeconds();
  JsonObject http = x["http"].to<JsonObject>();
  http["current"] = w.currentHttp; http["forecast"] = w.forecastHttp; http["air"] = w.airHttp;

  JsonObject set = doc["settings"].to<JsonObject>();
  set["preset"] = s.presetIndex;
  set["customName"] = s.custom.name; set["customLat"] = s.custom.latitude; set["customLon"] = s.custom.longitude;
  set["onHour"] = s.onHour; set["offHour"] = s.offHour;
  set["dayBrightness"] = s.dayBrightness; set["nightBrightness"] = s.nightBrightness;
  set["schedule"] = s.scheduleOn; set["landscape"] = s.landscape; set["autoCycle"] = s.autoCycle;
  set["invertColors"] = s.invertColors;
  set["wifiSsid"] = wifiConfigured(s) ? s.wifiSsid : "";
  set["locked"] = s.adminPassword[0] != 0;

  JsonArray presets = doc["presets"].to<JsonArray>();
  for (int i = 0; i < LOCATION_COUNT; ++i) {
    JsonArray item = presets.add<JsonArray>();
    item.add(LOCATIONS[i].name); item.add(LOCATIONS[i].latitude); item.add(LOCATIONS[i].longitude);
  }
}
void handleState() {
  if (!authorized()) return;
  JsonDocument doc; stateJson(doc); sendJson(200, doc);
}

// ── 설정 저장 ──
bool parseCoordinate(JsonVariantConst v, float lo, float hi, char* out, size_t size) {
  float value;
  if (v.is<float>()) value = v.as<float>();
  else if (v.is<const char*>()) { char* end; value = strtof(v.as<const char*>(), &end); if (end == v.as<const char*>()) return false; }
  else return false;
  if (!isfinite(value) || value < lo || value > hi) return false;
  snprintf(out, size, "%.4f", value);
  return true;
}
size_t utf8Length(const char* s) { size_t n = 0; for (; *s; ++s) n += (static_cast<uint8_t>(*s) & 0xC0) != 0x80; return n; }
void handleSettings() {
  if (!authorized()) return;
  JsonDocument body; if (!readBody(body)) return;
  DeviceSettings next = currentSettings();
  if (body["preset"].is<int>()) {
    const int preset = body["preset"];
    if (preset < -1 || preset >= LOCATION_COUNT) return sendError(422, "invalid", "지역 번호가 올바르지 않습니다", "preset");
    next.presetIndex = preset;
  }
  if (!body["customName"].isNull() || !body["customLat"].isNull() || !body["customLon"].isNull()) {
    const char* name = body["customName"] | "";
    String trimmed = name; trimmed.trim();
    if (trimmed.isEmpty() || utf8Length(trimmed.c_str()) > 10 || trimmed.length() >= sizeof(next.custom.name))
      return sendError(422, "invalid", "이름은 1~10자로 적어 주세요", "customName");
    Place place; strlcpy(place.name, trimmed.c_str(), sizeof(place.name));
    if (!parseCoordinate(body["customLat"], -90, 90, place.latitude, sizeof(place.latitude)))
      return sendError(422, "invalid", "위도는 -90~90 사이 숫자입니다 (예: 37.5665)", "customLat");
    if (!parseCoordinate(body["customLon"], -180, 180, place.longitude, sizeof(place.longitude)))
      return sendError(422, "invalid", "경도는 -180~180 사이 숫자입니다 (예: 126.9780)", "customLon");
    next.custom = place; next.presetIndex = -1;
  }
  auto hour = [&](const char* key, uint8_t& out) {
    if (body[key].isNull()) return true;
    const int h = body[key] | -1; if (h < 0 || h > 23) return false; out = h; return true;
  };
  if (!hour("onHour", next.onHour)) return sendError(422, "invalid", "켜지는 시각은 0~23시입니다", "onHour");
  if (!hour("offHour", next.offHour)) return sendError(422, "invalid", "꺼지는 시각은 0~23시입니다", "offHour");
  auto level = [&](const char* key, uint8_t& out, int lo) {
    if (body[key].isNull()) return true;
    const int v = body[key] | -1; if (v < lo || v > 255) return false; out = v; return true;
  };
  if (!level("dayBrightness", next.dayBrightness, 10)) return sendError(422, "invalid", "낮 밝기 값이 올바르지 않습니다", "dayBrightness");
  if (!level("nightBrightness", next.nightBrightness, 0)) return sendError(422, "invalid", "밤 밝기 값이 올바르지 않습니다", "nightBrightness");
  if (body["schedule"].is<bool>()) next.scheduleOn = body["schedule"];
  if (body["landscape"].is<bool>()) next.landscape = body["landscape"];
  if (body["autoCycle"].is<bool>()) next.autoCycle = body["autoCycle"];
  if (body["invertColors"].is<bool>()) next.invertColors = body["invertColors"];
  if (body["adminPassword"].is<const char*>()) {
    const char* pass = body["adminPassword"];
    if (strlen(pass) > 32) return sendError(422, "invalid", "비밀번호는 32자까지입니다", "adminPassword");
    if (pass[0] && strlen(pass) < 4) return sendError(422, "invalid", "비밀번호는 4자 이상으로 정해 주세요", "adminPassword");
    strlcpy(next.adminPassword, pass, sizeof(next.adminPassword));
  }
  applySettings(next);
  JsonDocument doc; stateJson(doc); sendJson(200, doc);
}

// ── 글꼴 확인: 기기 화면에 표시할 수 없는 글자 ──
void handleGlyphs() {
  if (!authorized()) return;
  JsonDocument doc; doc["missing"] = missingGlyphs(server.arg("text").c_str()); sendJson(200, doc);
}

// ── Wi-Fi ──
void handleScan() {
  if (!authorized()) return;
  JsonDocument doc;
  const int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true); doc["scanning"] = true; return sendJson(200, doc); }
  if (found == WIFI_SCAN_RUNNING) { doc["scanning"] = true; return sendJson(200, doc); }
  doc["scanning"] = false;
  JsonArray list = doc["networks"].to<JsonArray>();
  for (int i = 0; i < found && list.size() < 20; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool duplicate = false;  // 같은 이름(메시 공유기)은 가장 센 것 하나만
    for (JsonObject n : list) if (ssid == n["ssid"].as<const char*>()) { duplicate = true; break; }
    if (duplicate) continue;
    JsonObject n = list.add<JsonObject>();
    n["ssid"] = ssid; n["rssi"] = WiFi.RSSI(i); n["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    n["channel"] = WiFi.channel(i);  // 14 이하 = 2.4GHz (ESP32 는 5GHz 를 찾지 못함)
  }
  WiFi.scanDelete();
  sendJson(200, doc);
}
void handleWifi() {
  if (!authorized()) return;
  JsonDocument body; if (!readBody(body)) return;
  const char* ssid = body["ssid"] | "";
  const char* pass = body["password"] | "";
  if (!ssid[0] || strlen(ssid) > 32) return sendError(422, "invalid", "Wi-Fi 이름을 입력해 주세요 (32자 이하)", "ssid");
  if (strlen(pass) > 63 || (pass[0] && strlen(pass) < 8)) return sendError(422, "invalid", "Wi-Fi 비밀번호는 8~63자입니다", "password");
  DeviceSettings next = currentSettings();
  strlcpy(next.wifiSsid, ssid, sizeof(next.wifiSsid)); strlcpy(next.wifiPassword, pass, sizeof(next.wifiPassword));
  applySettings(next);
  JsonDocument doc; doc["restarting"] = true; doc["ssid"] = ssid; sendJson(200, doc);
  restartPending = true; restartAt = millis() + 1500;  // 응답이 전달된 뒤 다시 시작
}

// ── 기기 동작 ──
void handleRefresh() {
  if (!authorized()) return;
  JsonDocument doc; doc["started"] = requestWeatherRefresh(); sendJson(200, doc);
}
void handleCalibrate() {
  if (!authorized()) return;
  requestTouchCalibration();
  JsonDocument doc; doc["started"] = true; sendJson(200, doc);
}
void handleReboot() {
  if (!authorized()) return;
  JsonDocument doc; doc["restarting"] = true; sendJson(200, doc);
  restartPending = true; restartAt = millis() + 1000;
}
void handlePage() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(WEB_PAGE_GZ), WEB_PAGE_GZ_LEN);
}
void handleNotFound() {
  // 설정 모드에서는 휴대폰의 인터넷 확인 요청을 모두 설정 페이지로 돌려 자동으로 열리게 합니다.
  if (apOn && server.hostHeader() != WiFi.softAPIP().toString()) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  sendError(404, "not_found", "없는 주소입니다");
}

void startAccessPoint() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName);  // 설정용이라 비밀번호 없음. 연결에 성공하면 몇 분 뒤 꺼집니다.
  // 모드를 바꾸면 설정이 초기화될 수 있어서 20MHz 폭을 다시 고정합니다 (약한 신호에서 큰 패킷 손실 방지)
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20); esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  dns.start(53, "*", WiFi.softAPIP());
  apOn = true;
  Serial.printf("SETUP AP %s at %s\n", apName, WiFi.softAPIP().toString().c_str());
}
void stopAccessPoint() {
  if (!apOn) return;
  dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  apOn = false; Serial.println("SETUP AP off");
}
}

void startWebPortal() {
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(apName, sizeof(apName), "CYD-Weather-%04X", static_cast<unsigned>((mac >> 32) & 0xFFFF));
  const char* headers[] = {"X-Admin-Password"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, handlePage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/glyphs", HTTP_GET, handleGlyphs);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.onNotFound(handleNotFound);
  server.begin();
  if (!wifiConfigured(currentSettings())) startAccessPoint();
  Serial.printf("WEB http://%s.local (port 80)\n", WEB_HOSTNAME);
}

void handleWebPortal() {
  const uint32_t now = millis();
  const bool online = WiFi.status() == WL_CONNECTED;
  if (online) {
    if (!linkLogged) onLinkUp();
    if (!connectedSinceMs) connectedSinceMs = now;
    lastConnectedMs = now;
    if (!mdnsStarted && MDNS.begin(WEB_HOSTNAME)) { MDNS.addService("http", "tcp", 80); mdnsStarted = true; }
    if (apOn && wifiConfigured(currentSettings()) && now - connectedSinceMs > AP_KEEP_AFTER_JOIN_MS) stopAccessPoint();
  } else {
    connectedSinceMs = 0; linkLogged = false;
    const uint32_t since = lastConnectedMs ? now - lastConnectedMs : now;
    if (!apOn && since > AP_AFTER_FAIL_MS) startAccessPoint();
  }
  if (apOn) dns.processNextRequest();
  server.handleClient();
  if (restartPending && static_cast<int32_t>(now - restartAt) >= 0) { Serial.println("RESTART (web)"); delay(100); ESP.restart(); }
}

bool webPortalApMode() { return apOn; }
const char* webPortalApName() { return apName; }
