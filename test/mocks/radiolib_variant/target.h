#pragma once

// Stand-in for the target.h of a variant with a RadioLib radio, for the suites
// that build the shared variant code (src/helpers/radiolib/RadioLibTarget.cpp).
// Listed ahead of test/mocks, whose target.h stands in for a variant without one.
// As on a board, the environment sets WRAPPER_CLASS and the variant's target.cpp
// (here: the test) defines the globals declared below.

#include <helpers/radiolib/RadioLibWrappers.h>
#include <SPI.h>
#include <Wire.h>

#include <string>
#include <vector>

// What the stand-ins below were asked to do, in order, for the tests to compare.
inline std::vector<std::string> target_calls;

// Stand-in for a chip class with std_init() (CustomSX1262 and its siblings):
// records the call and the SPI bus it was given, and counts the calls per radio.
class TestRadio : public PhysicalLayer {
public:
  bool std_init_result = true;
  int std_init_calls = 0;

  bool std_init(SPIClass* spi = nullptr) {
    target_calls.push_back(spi == &SPI ? "std_init(&SPI)" : spi ? "std_init(other SPI)" : "std_init()");
    std_init_calls++;
    return std_init_result;
  }
};

// Stand-in for AutoDiscoverRTCClock: records which bus it was started on.
class TestRTCClock {
public:
  void begin(TwoWire& wire) { target_calls.push_back(&wire == &Wire ? "rtc_clock.begin(Wire)" : "rtc_clock.begin(other)"); }
};

// What CustomSX1262Wrapper.h defines, which every target.h of an SX1262 variant
// includes.
#ifndef USE_SX1262
#define USE_SX1262
#endif

class TestRadioWrapper : public RadioLibWrapper {
public:
  TestRadioWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  void setParams(float, float, uint8_t, uint8_t) override { }
  bool isReceivingPacket() override { return false; }
  float getCurrentRSSI() override { return -100.0f; }
};

class TestBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 4200; }
  const char* getManufacturerName() const override { return "test"; }
  void reboot() override { }
  uint8_t getStartupReason() const override { return 0; }
};

extern WRAPPER_CLASS radio_driver;
extern TestRTCClock rtc_clock;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
