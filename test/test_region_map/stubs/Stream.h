#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Just enough of Arduino's Print and Stream for RegionMap's exporters to compile.
// The test never exports, so nothing here keeps what is written.
class Print {
public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) { return 1; }
  virtual size_t write(const uint8_t* buffer, size_t size) {
    size_t n = 0;
    for (size_t i = 0; i < size; i++) n += write(buffer[i]);
    return n;
  }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t println(const char* s) { return print(s) + print('\n'); }
  size_t printf(const char* fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return n > 0 ? write((const uint8_t*)buf, (size_t)n) : 0;
  }
  virtual void flush() {}
};

class Stream : public Print {
public:
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual int peek() { return -1; }
};
