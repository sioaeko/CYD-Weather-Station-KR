#pragma once
// PC 미리보기용 최소 Arduino 대체 헤더 (실제 펌웨어에는 쓰이지 않음)
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string>

class String {
 public:
  std::string s;
  String() {}
  String(const char* v) : s(v ? v : "") {}
  String(const std::string& v) : s(v) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(long long v) : s(std::to_string(v)) {}
  String(uint8_t v) : s(std::to_string(v)) {}
  String(int8_t v) : s(std::to_string(v)) {}
  String(double v, int precision = 2) { char b[48]; snprintf(b, sizeof(b), "%.*f", precision, v); s = b; }
  String(float v, int precision = 2) : String(static_cast<double>(v), precision) {}
  unsigned length() const { return static_cast<unsigned>(s.size()); }
  const char* c_str() const { return s.c_str(); }
  char operator[](unsigned i) const { return i < s.size() ? s[i] : 0; }
  void remove(unsigned index) { if (index < s.size()) s.erase(index); }
  bool endsWith(const char* suffix) const { const size_t n = strlen(suffix); return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0; }
  String substring(unsigned a) const { return a < s.size() ? String(s.substr(a)) : String(); }
  String substring(unsigned a, unsigned b) const { return a < s.size() ? String(s.substr(a, b > a ? b - a : 0)) : String(); }
  int indexOf(const String& needle) const { const size_t p = s.find(needle.s); return p == std::string::npos ? -1 : static_cast<int>(p); }
  int lastIndexOf(const char* needle) const { const size_t p = s.rfind(needle); return p == std::string::npos ? -1 : static_cast<int>(p); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
  friend String operator+(const String& a, const char* b) { return String(a.s + b); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
  friend String operator+(const String& a, int b) { return a + String(b); }
  friend String operator+(const String& a, long b) { return a + String(b); }
  friend String operator+(const String& a, uint8_t b) { return a + String(b); }
  bool operator==(const String& o) const { return s == o.s; }
};

#define pgm_read_byte(p) (*(const uint8_t*)(p))
uint32_t millis();
inline void delay(uint32_t) {}
struct FakeSerial {
  void printf(const char* f, ...) { va_list a; va_start(a, f); vfprintf(stderr, f, a); va_end(a); }
  void println(const char* v) { fprintf(stderr, "%s\n", v); }
};
extern FakeSerial Serial;
struct FakeEsp { uint32_t getFreeHeap() { return 104 * 1024; } };
extern FakeEsp ESP;

// 미리보기용 가짜 시계 (KST 고정)
time_t fake_time(time_t* out);
#define time(x) fake_time(x)
inline struct tm* localtime_r(const time_t* t, struct tm* out) { time_t k = *t + 9 * 3600; gmtime_s(out, &k); return out; }
inline struct tm* gmtime_r(const time_t* t, struct tm* out) { gmtime_s(out, t); return out; }
inline void configTzTime(const char*, const char*, const char*, const char*) {}
inline size_t strlcpy(char* d, const char* s, size_t n) { const size_t l = strlen(s); if (n) { const size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
