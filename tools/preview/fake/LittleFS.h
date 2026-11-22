#pragma once
#include "Arduino.h"
#include <vector>
#include <string>
// PC 미리보기용: LittleFS 경로를 스케치의 data/ 폴더로 연결합니다.
extern std::string g_dataRoot;
namespace fs {
class File {
 public:
  FILE* f = nullptr; std::string path, base; bool dir = false;
  std::vector<std::string> entries; size_t next = 0;
  File() {}
  explicit operator bool() const { return f || dir; }
  int read() { if (!f) return -1; const int c = fgetc(f); return c == EOF ? -1 : c; }
  int read(uint8_t* b, size_t n) { return f ? static_cast<int>(fread(b, 1, n, f)) : 0; }
  bool seek(uint32_t pos) { return f && fseek(f, pos, SEEK_SET) == 0; }
  size_t size() { if (!f) return 0; const long p = ftell(f); fseek(f, 0, SEEK_END); const long n = ftell(f); fseek(f, p, SEEK_SET); return n; }
  String name() const { return String(base); }
  bool isDirectory() const { return dir; }
  File openNextFile();
  // 간단히 하려고 파일을 닫지 않고 소멸 시 닫습니다.
  File(const File&) = delete; File& operator=(const File&) = delete;
  File(File&& o) noexcept { *this = std::move(o); }
  File& operator=(File&& o) noexcept { if (f && f != o.f) fclose(f); f = o.f; o.f = nullptr; path = o.path; base = o.base; dir = o.dir; entries = o.entries; next = o.next; return *this; }
  ~File() { if (f) fclose(f); }
};
class FS {
 public:
  File open(const String& path, const char* mode = "r");
  bool exists(const String& path);
  bool begin(bool) { return true; }
};
}
using fs::File;
extern fs::FS LittleFS;
