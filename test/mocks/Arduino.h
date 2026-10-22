#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <string>
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

// Arduino's random API, used by helpers/ArduinoHelpers.h. The sequence is
// deterministic, so a test that exercises code drawing from it stays repeatable.
inline uint32_t g_mock_random_state = 1;

inline void randomSeed(long seed) {
  g_mock_random_state = (uint32_t)(seed ? seed : 1);
}

inline long random(long howsmall, long howbig) {
  if (howbig <= howsmall) return howsmall;
  g_mock_random_state = g_mock_random_state * 1103515245u + 12345u;
  uint32_t span = (uint32_t)(howbig - howsmall);
  return howsmall + (long)((g_mock_random_state >> 16) % span);
}

inline long random(long howbig) { return random(0, howbig); }

// Arduino's min() and max(), as functions rather than macros. They must not be
// macros: production code uses those words as ordinary identifiers, and
// helpers/CommonRadioPrefs.cpp has both a struct field 'float max' and a parameter
// named 'max', which a function-like macro turns into a syntax error. Two type
// parameters, because the call sites mix types, for example
// min(sp - hex, PUB_KEY_SIZE*2) with a pointer difference and an int.
template <typename T, typename U>
inline auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }

template <typename T, typename U>
inline auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }

// Arduino's Serial. It keeps what the firmware printed, so a test can assert on
// output the roles write there (the packet log dump, for example).
class MockSerial : public Stream {
public:
  std::string out;

  size_t write(uint8_t b) override { out.push_back((char)b); return 1; }

  size_t print(const char* s) { out += s; return strlen(s); }
  size_t print(char c) { out.push_back(c); return 1; }
  size_t print(int v, int r = DEC) { char b[24]; snprintf(b, sizeof(b), "%d", v); out += b; return strlen(b); }
  size_t print(unsigned int v, int r = DEC) { char b[24]; snprintf(b, sizeof(b), "%u", v); out += b; return strlen(b); }
  size_t print(long v, int r = DEC) { char b[32]; snprintf(b, sizeof(b), "%ld", v); out += b; return strlen(b); }
  size_t print(unsigned long v, int r = DEC) { char b[32]; snprintf(b, sizeof(b), "%lu", v); out += b; return strlen(b); }
  size_t print(double v, int p = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", p, v); out += b; return strlen(b); }

  size_t println() { out.push_back('\n'); return 1; }
  template <typename T> size_t println(T v) { size_t n = print(v); return n + println(); }

  size_t printf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    out += buf;
    return (size_t)n;
  }

  void reset() { out.clear(); }
};

inline MockSerial Serial;
