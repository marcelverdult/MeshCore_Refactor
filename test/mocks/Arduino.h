#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
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

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

// Arduino core extension (not in the C standard library).
inline char* ltoa(long value, char* buf, int base) {
  char tmp[sizeof(long) * 8 + 1];
  char* t = tmp;
  unsigned long v = (value < 0 && base == 10) ? -(unsigned long)value : (unsigned long)value;
  do {
    int digit = v % base;
    *t++ = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
    v /= base;
  } while (v);
  char* p = buf;
  if (value < 0 && base == 10) *p++ = '-';
  while (t > tmp) *p++ = *--t;
  *p = 0;
  return buf;
}
