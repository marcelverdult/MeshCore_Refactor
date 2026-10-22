#pragma once

// The parts of the RadioLib variant stand-ins that do not depend on how the
// variant defines its globals: a recording radio, wrapper, board and RTC clock,
// and RadioLib's Module. Included by test/mocks/radiolib_variant/target.h (a
// variant that defines its globals in its target.cpp) and by
// test/mocks/radiolib_variant_globals/target.h (one that takes them from
// RadioLibTarget.cpp).

#include <helpers/radiolib/RadioLibWrappers.h>
#include <SPI.h>
#include <Wire.h>

#include <string>
#include <vector>

// What the stand-ins below were asked to do, in order, for the tests to compare.
inline std::vector<std::string> target_calls;

// Stand-in for RadioLib's Module, with its two Arduino constructors, which do
// different things: without an SPI bus RadioLib starts the default bus itself,
// with one it leaves that to the caller. `spi` tells them apart (null for the
// first), the pins are kept as given.
class Module {
public:
  uint32_t cs, irq, rst, gpio;
  SPIClass* spi;

  Module(uint32_t cs, uint32_t irq, uint32_t rst, uint32_t gpio = (uint32_t)-1)
    : cs(cs), irq(irq), rst(rst), gpio(gpio), spi(nullptr) { }
  Module(uint32_t cs, uint32_t irq, uint32_t rst, uint32_t gpio, SPIClass& spi)
    : cs(cs), irq(irq), rst(rst), gpio(gpio), spi(&spi) { }
};

// Stand-in for a chip class with std_init() (CustomSX1262 and its siblings):
// records the call and the SPI bus it was given, and counts the calls per radio.
class TestRadio : public PhysicalLayer {
public:
  Module* module = nullptr;  // what the variant constructed it from, as `new Module(...)`
  bool std_init_result = true;
  int std_init_calls = 0;

  bool std_init(SPIClass* spi = nullptr) {
    target_calls.push_back(spi == &SPI ? "std_init(&SPI)" : spi ? "std_init(other SPI)" : "std_init()");
    std_init_calls++;
    return std_init_result;
  }

  TestRadio() = default;
  TestRadio(Module* mod) : module(mod) { }
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
  // What it was constructed with, for the tests (the real wrappers keep them
  // protected as well).
  PhysicalLayer* wrapped_radio() const { return _radio; }
  mesh::MainBoard* wrapped_board() const { return _board; }
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
