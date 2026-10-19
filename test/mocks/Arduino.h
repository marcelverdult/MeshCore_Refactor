#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
