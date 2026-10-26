#pragma once

// Stand-in for a variant's target.h, as far as src/helpers/ESP32Board.cpp uses
// it: ESP32Board::enterDeepSleep() powers off the radio and stops the GPS.

#include <helpers/ESP32Board.h>

struct TestRadioDriver {
  void powerOff() { }
};
inline TestRadioDriver radio_driver;

struct TestLocationProvider {
  void stop() { }
};
struct TestSensors {
  TestLocationProvider* getLocationProvider() { return nullptr; }
};
inline TestSensors sensors;
