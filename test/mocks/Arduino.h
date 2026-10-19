#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>
#include "Stream.h"

inline uint32_t g_mock_millis = 0;

using std::isnan;
// Arduino offers these as macros; the host gets the std versions, which
// pick the right overload for the double arithmetic in the core.
using std::max;
using std::min;

// Arduino's two-argument random(), lower bound inclusive. Deterministic
// here, so a test that mixes it in stays reproducible.
inline long random(long lower, long upper) {
  return upper > lower ? lower + (upper - lower) / 2 : lower;
}

inline uint32_t millis() {
  return g_mock_millis;
}

inline void delay(uint32_t ms) {
  g_mock_millis += ms;
}
