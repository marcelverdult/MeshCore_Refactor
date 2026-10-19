#pragma once

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Stream.h"

inline uint32_t g_mock_millis = 0;

using std::isnan;

inline uint32_t millis() {
  return g_mock_millis;
}

inline void delay(uint32_t ms) {
  g_mock_millis += ms;
}

// ltoa() is part of the Arduino API and has no equivalent in the host C library,
// so code that formats numbers (TxtDataHelpers.cpp) needs it spelled out here.
inline char* ltoa(long value, char* dest, int base) {
  snprintf(dest, 24, base == 16 ? "%lx" : "%ld", value);
  return dest;
}
