#pragma once

// Stand-in for src/helpers/ESP32Board.h, which the ESP32 variants include
// through their board header: only ESP32RTCClock, recording begin(), since the
// real one sets the system time from the ESP32's RTC memory.

#include <Mesh.h>
#include "../../radiolib_variant/target_calls.h"

class ESP32RTCClock : public mesh::RTCClock {
  uint32_t _time = 0;
public:
  void begin() { target_calls.push_back("fallback_clock.begin()"); }
  uint32_t getCurrentTime() override { return _time; }
  void setCurrentTime(uint32_t time) override { _time = time; }
};
