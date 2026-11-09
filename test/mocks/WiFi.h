#pragma once

// Stand-in for the Arduino WiFi library, for the companion's text CLI (step 13d-a of
// docs/refactoring/0013-cli-command-table.md). MyMesh.cpp includes <WiFi.h> when
// ENABLE_WIFI_INTERFACE is set and reads two things from it for `get wifi.status` and
// `get wifi.ip`: WiFi.status() compared with WL_CONNECTED, and WiFi.localIP().toString().
// A test sets `status_value` and `address` and reads them back through the CLI.

#include <string>

enum wl_status_t {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6
};

// The Arduino IPAddress::toString() returns an Arduino String; the caller only takes
// c_str(), so std::string serves.
class IPAddress {
public:
  std::string text;
  IPAddress() { }
  explicit IPAddress(const char* t) : text(t) { }
  std::string toString() const { return text; }
};

class WiFiClass {
public:
  wl_status_t status_value = WL_DISCONNECTED;
  IPAddress address{"0.0.0.0"};

  wl_status_t status() const { return status_value; }
  IPAddress localIP() const { return address; }

  void reset() {
    status_value = WL_DISCONNECTED;
    address = IPAddress("0.0.0.0");
  }
};

inline WiFiClass WiFi;
