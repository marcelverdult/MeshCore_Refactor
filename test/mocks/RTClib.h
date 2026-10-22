#pragma once

// Stand-in for RTClib's DateTime, so the roles and CommonCLI build in the native
// env. It follows RTClib: the uint32_t constructor takes seconds since
// 2000-01-01T00:00:00. Only the accessors the firmware uses are implemented, and
// no test characterizes date formatting through this mock.

#include <cstdint>

class DateTime {
  uint16_t _year = 2000;
  uint8_t _month = 1, _day = 1, _hour = 0, _minute = 0, _second = 0;

public:
  DateTime() { }

  explicit DateTime(uint32_t secs_since_2000) {
    uint32_t days = secs_since_2000 / 86400UL;
    uint32_t rem = secs_since_2000 % 86400UL;
    _hour = (uint8_t)(rem / 3600);
    _minute = (uint8_t)((rem % 3600) / 60);
    _second = (uint8_t)(rem % 60);

    uint16_t y = 2000;
    for (;;) {
      bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
      uint32_t len = leap ? 366 : 365;
      if (days < len) break;
      days -= len;
      y++;
    }
    _year = y;
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    static const uint8_t lengths[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint8_t m = 0;
    for (; m < 12; m++) {
      uint32_t len = lengths[m] + ((m == 1 && leap) ? 1 : 0);
      if (days < len) break;
      days -= len;
    }
    _month = (uint8_t)(m + 1);
    _day = (uint8_t)(days + 1);
  }

  uint16_t year() const { return _year; }
  uint8_t month() const { return _month; }
  uint8_t day() const { return _day; }
  uint8_t hour() const { return _hour; }
  uint8_t minute() const { return _minute; }
  uint8_t second() const { return _second; }
};
