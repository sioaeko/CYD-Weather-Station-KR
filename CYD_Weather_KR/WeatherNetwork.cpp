#include "WeatherNetwork.h"
#include "WeatherJson.h"
#include "All_Settings.h"
#include "NTP_Time.h"
#include "ApiCertificates.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <atomic>
#include <memory>
namespace {
WeatherState published;
SemaphoreHandle_t stateMutex = nullptr;
std::atomic<bool> requested{true}, refreshing{false};
std::atomic<uint32_t> nextFetch{0};
std::atomic<uint32_t> placeId{1};       // 지역이 바뀔 때마다 1 씩 증가
std::atomic<bool> wifiConfigured{true};
constexpr int HTTP_TIMEOUT_MS = 15000;  // 신호가 약하면 응답이 느림 (8초에서 늘림)
// 지역이 그대로일 때만 게시합니다. (받는 도중 지역을 바꾸면 이전 지역 결과가 화면에 섞이지 않게)
bool publish(WeatherState& s) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  const bool current = placeId.load() == s.placeId;
  if (current) { s.generation = published.generation + 1; published = s; }
  xSemaphoreGive(stateMutex);
  return current;
}
int fetchJson(const String& url, JsonDocument& result, JsonDocument& filter) {
  if (WiFi.status() != WL_CONNECTED) return NET_WIFI_OFFLINE;
  if (!clockValid()) return NET_CLOCK_NOT_SET;
  WiFiClientSecure tls; tls.setCACert(API_ROOT_CERTIFICATES); tls.setHandshakeTimeout(20);
  HTTPClient http; http.setConnectTimeout(HTTP_TIMEOUT_MS); http.setTimeout(HTTP_TIMEOUT_MS);
  // 청크 없는 응답을 스트림으로 바로 파싱해 RAM 에 응답 전체를 두 번 올리지 않습니다.
  http.useHTTP10(true); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(tls, url)) return NET_BEGIN_FAILED;
  const int code = http.GET();
  if (code == HTTP_CODE_OK) {
    const DeserializationError error = deserializeJson(result, http.getStream(), DeserializationOption::Filter(filter));
    http.end(); return error ? NET_JSON_ERROR : code;
  }
  if (code < 0) {
    // 서버 이름과 TLS 오류만 남깁니다 (주소 뒷부분에는 API 키가 있으므로 찍지 않음)
    const int hostStart = url.indexOf("://") + 3, hostEnd = url.indexOf('/', hostStart);
    char err[100] = ""; const int tlsError = tls.lastError(err, sizeof(err));
    Serial.printf("HTTPS %s failed: http=%d mbedtls=%d %s (min heap %u)\n", url.substring(hostStart, hostEnd).c_str(), code,
                  tlsError, err, ESP.getMinFreeHeap());
  }
  http.end(); return code;
}
int fetchOpenWeather(const char* endpoint, const Place& where, JsonDocument& doc, JsonDocument& filter, uint8_t& activeKey) {
  for (int key = 0; key < 2; ++key) {
    doc.clear();
    const String url = String("https://api.openweathermap.org/data/2.5/") + endpoint +
      "?lat=" + where.latitude + "&lon=" + where.longitude + "&units=metric&appid=" + OPENWEATHER_KEYS[key];
    const int status = fetchJson(url, doc, filter);
    if (status == 200) { activeKey = key + 1; return status; }
    if (key == 1 || OPENWEATHER_KEYS[1][0] == '\0' || (status != 401 && status != 403 && status != 429)) return status;
  }
  return NET_BAD_DATA;
}
void fetchCurrent(WeatherState& next, const Place& where) {
  JsonDocument doc, filter; currentWeatherFilter(filter);
  next.currentHttp = fetchOpenWeather("weather", where, doc, filter, next.activeKey);
  if (next.currentHttp != 200) return;
  CurrentWeather result; result.valid = parsePoint(doc.as<JsonObjectConst>(), result.point, true);
  result.sunrise = doc["sys"]["sunrise"] | static_cast<time_t>(0);
  result.sunset = doc["sys"]["sunset"] | static_cast<time_t>(0);
  if (!result.valid || result.sunrise <= 0 || result.sunset <= result.sunrise) { next.currentHttp = NET_BAD_DATA; return; }
  result.fetched = time(nullptr); next.current = result;
}
void fetchForecast(WeatherState& next, const Place& where) {
  JsonDocument doc, filter; forecastWeatherFilter(filter);
  next.forecastHttp = fetchOpenWeather("forecast", where, doc, filter, next.activeKey);
  if (next.forecastHttp != 200) return;
  JsonArrayConst list = doc["list"];
  if (list.size() == 0 || list.size() > 40) { next.forecastHttp = NET_BAD_DATA; return; }
  std::unique_ptr<WeatherPoint[]> points(new (std::nothrow) WeatherPoint[list.size()]);
  if (!points) { next.forecastHttp = NET_NO_MEMORY; return; }
  size_t count = 0;
  for (JsonObjectConst entry : list) {
    if (!parsePoint(entry, points[count])) { next.forecastHttp = NET_BAD_DATA; return; }
    ++count;
  }
  const time_t now = time(nullptr);
  DayForecast days[4]; buildDailyForecast(points.get(), count, now, LOCAL_UTC_OFFSET_SECONDS, days);
  for (int i = 0; i < 4; ++i) if (!days[i].valid) { next.forecastHttp = NET_BAD_DATA; return; }
  for (int i = 0; i < 4; ++i) next.days[i] = days[i];
  next.outlook = buildOutlook(points.get(), count, now);
  next.forecastFetched = now;
}
// 자외선 지수 + 미세먼지(PM10) + 초미세먼지(PM2.5): Open-Meteo 대기질 API (키 필요 없음, CAMS 모델값)
void fetchAir(WeatherState& next, const Place& where) {
  JsonDocument doc, filter;
  for (const char* field : {"time", "uv_index", "pm10", "pm2_5"}) filter["hourly"][field] = true;
  const String url = String("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=") +
    where.latitude + "&longitude=" + where.longitude +
    "&hourly=uv_index,pm10,pm2_5&timeformat=unixtime&forecast_days=2";
  next.airHttp = fetchJson(url, doc, filter);
  if (next.airHttp != 200) return;
  JsonArrayConst times = doc["hourly"]["time"], uv = doc["hourly"]["uv_index"];
  JsonArrayConst pm10 = doc["hourly"]["pm10"], pm25 = doc["hourly"]["pm2_5"];
  const time_t nowUtc = time(nullptr); int selected = -1;
  for (size_t i = 0; i < times.size() && i < uv.size() && i < pm10.size() && i < pm25.size(); ++i) {
    const time_t stamp = times[i].as<time_t>();
    if (stamp <= nowUtc && nowUtc < stamp + 3600) selected = i;
  }
  if (selected < 0 || !uv[selected].is<float>() || !pm10[selected].is<float>() || !pm25[selected].is<float>()) {
    next.airHttp = NET_BAD_DATA; return;
  }
  const float uvValue = uv[selected], pm10Value = pm10[selected], pm25Value = pm25[selected];
  if (!isfinite(uvValue) || uvValue < 0 || uvValue > 30 || !isfinite(pm10Value) || pm10Value < 0 || pm10Value > 2000 ||
      !isfinite(pm25Value) || pm25Value < 0 || pm25Value > 1500) { next.airHttp = NET_BAD_DATA; return; }
  next.uv = uvValue; next.pm10 = pm10Value; next.pm25 = pm25Value;
  next.airTimestamp = times[selected].as<time_t>(); next.airFetched = nowUtc; next.airValid = true;
}
void describeResult(WeatherState& s) {
  if (s.currentHttp == 200 && s.forecastHttp == 200 && s.airHttp == 200) {
    strlcpy(s.status, "모든 정보가 최신입니다", sizeof(s.status)); return;
  }
  const char* source = s.currentHttp != 200 ? "현재 날씨" : s.forecastHttp != 200 ? "예보" : "대기질";
  const int code = s.currentHttp != 200 ? s.currentHttp : s.forecastHttp != 200 ? s.forecastHttp : s.airHttp;
  snprintf(s.status, sizeof(s.status), "%s 실패: %s", source, httpStatusText(code));
}
void networkTask(void*) {
  for (;;) {
    const bool manual = requested.exchange(false);
    if (!manual && static_cast<int32_t>(millis() - nextFetch.load()) < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    refreshing.store(true); WeatherState next; copyWeatherState(next);
    const Place where = next.place;
    if (!wifiConfigured.load()) {
      // Wi-Fi 정보가 없으면 설정 웹(Wi-Fi 설정 모드)에서 입력할 때까지 기다립니다.
      strlcpy(next.status, "Wi-Fi 설정이 필요합니다", sizeof(next.status));
      next.currentHttp = next.forecastHttp = next.airHttp = NET_WIFI_OFFLINE;
      nextFetch.store(millis() + RETRY_INTERVAL_MS);
      publish(next); refreshing.store(false); continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      strlcpy(next.status, "Wi-Fi 다시 연결하는 중", sizeof(next.status)); publish(next);
      // 처음 WiFi.begin() 이 끝날 시간을 준 뒤에 재연결을 시도합니다.
      if (millis() >= 15000) WiFi.reconnect();
      const uint32_t started = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (WiFi.status() != WL_CONNECTED) {
      strlcpy(next.status, "Wi-Fi 연결 안 됨 · 1분 후 재시도", sizeof(next.status));
      next.currentHttp = next.forecastHttp = next.airHttp = NET_WIFI_OFFLINE;
    } else {
      const uint32_t started = millis();
      if (!clockValid()) {
        strlcpy(next.status, "인터넷 시간 맞추는 중 (NTP)", sizeof(next.status)); publish(next);
      }
      while (!clockValid() && millis() - started < 15000) vTaskDelay(pdMS_TO_TICKS(100));
      snprintf(next.status, sizeof(next.status), "%s 날씨 받는 중", where.name); publish(next);
      fetchCurrent(next, where); fetchForecast(next, where); fetchAir(next, where);
      describeResult(next);
    }
    const bool success = next.currentHttp == 200 && next.forecastHttp == 200 && next.airHttp == 200;
    nextFetch.store(millis() + (success ? UPDATE_INTERVAL_MS : RETRY_INTERVAL_MS));
    // 받는 도중 지역이 바뀌었으면 결과를 버리고 곧바로 새 지역을 받습니다.
    if (!publish(next)) requested.store(true);
    refreshing.store(false);
  }
}
}
const char* httpStatusText(int code) {
  switch (code) {
    case 200: return "정상";
    case 0: return "대기 중";
    case 401: return "API 키 거부(401) · 새 키는 최대 2시간 후 활성";
    case 403: return "접근 거부(403)";
    case 404: return "주소 없음(404)";
    case 429: return "호출 한도 초과(429)";
    case NET_WIFI_OFFLINE: return "Wi-Fi 끊김";
    case NET_CLOCK_NOT_SET: return "시간 동기화 전";
    case NET_BEGIN_FAILED: return "요청 생성 실패";
    case NET_JSON_ERROR: return "응답 해석 실패";
    case NET_BAD_DATA: return "데이터 형식 오류";
    case NET_NO_MEMORY: return "메모리 부족";
    case -1: return "서버 연결 실패";
    case -11: return "응답 시간 초과";
  }
  if (code >= 500) return "서버 오류(5xx)";
  return code < 0 ? "네트워크 오류" : "HTTP 오류";
}
void copyWeatherState(WeatherState& destination) {
  if (!stateMutex) { destination = WeatherState{}; return; }
  xSemaphoreTake(stateMutex, portMAX_DELAY); destination = published; xSemaphoreGive(stateMutex);
}
bool requestWeatherRefresh() {
  if (refreshing.load() || requested.exchange(true)) return false;
  return true;
}
bool weatherIsRefreshing() { return refreshing.load(); }
uint32_t weatherNextRefreshSeconds() {
  return refreshing.load() || requested.load() ? 0 : secondsUntil(millis(), nextFetch.load());
}
void setWeatherPlace(const Place& place) {
  if (!stateMutex) { published.place = place; requested.store(true); return; }
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  // 이전 지역 데이터는 지우고 새 지역으로 바로 받습니다.
  WeatherState fresh; fresh.place = place; fresh.placeId = placeId.fetch_add(1) + 1; fresh.activeKey = published.activeKey;
  snprintf(fresh.status, sizeof(fresh.status), "%s 날씨 받는 중", place.name);
  fresh.generation = published.generation + 1; published = fresh;
  xSemaphoreGive(stateMutex);
  requested.store(true);
}
void setWeatherWifiConfigured(bool configured) {
  const bool was = wifiConfigured.exchange(configured);
  if (configured && !was) requested.store(true);
}
void startWeatherNetwork(const Place& place, bool configured) {
  wifiConfigured.store(configured);
  stateMutex = xSemaphoreCreateMutex();
  published.place = place; published.placeId = placeId.load();
  if (!configured) strlcpy(published.status, "Wi-Fi 설정이 필요합니다", sizeof(published.status));
  if (!stateMutex || xTaskCreatePinnedToCore(networkTask, "weather", 12288, nullptr, 1, nullptr, 0) != pdPASS)
    Serial.println("ERROR: could not start weather worker");
}
