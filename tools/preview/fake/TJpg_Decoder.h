#pragma once
#include "Arduino.h"
#include "LittleFS.h"
enum JRESULT { JDR_OK = 0, JDR_INP = 2 };
using TJpgCallback = bool (*)(int16_t, int16_t, uint16_t, uint16_t, uint16_t*);
struct FakeJpg {
  void setJpgScale(int) {}
  void setSwapBytes(bool) {}
  void setCallback(TJpgCallback) {}
  JRESULT getFsJpgSize(uint16_t* w, uint16_t* h, const char*, fs::FS&) { *w = 240; *h = 124; return JDR_OK; }
  JRESULT drawFsJpg(int, int, const char*, fs::FS&) { return JDR_OK; }
};
extern FakeJpg TJpgDec;
