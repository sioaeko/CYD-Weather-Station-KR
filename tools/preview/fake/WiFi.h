#pragma once
#include "Arduino.h"
enum { WL_CONNECTED = 3, WL_DISCONNECTED = 6 };
struct FakeIp { String toString() const { return "192.168.0.23"; } };
struct FakeWiFi {
  int connected = WL_CONNECTED;
  int status() { return connected; }
  int RSSI() { return -58; }
  FakeIp localIP() { return {}; }
};
extern FakeWiFi WiFi;
