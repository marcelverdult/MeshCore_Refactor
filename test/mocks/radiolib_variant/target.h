#pragma once

// Stand-in for the target.h of a variant with a RadioLib radio, for the suites
// that build the shared variant code (src/helpers/radiolib/RadioLibTarget.cpp).
// Listed ahead of test/mocks, whose target.h stands in for a variant without one.
// As on a board, the environment sets WRAPPER_CLASS and the variant's target.cpp
// (here: the test) defines the globals declared below.

#include <helpers/radiolib/RadioLibWrappers.h>

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

bool radio_init();
mesh::LocalIdentity radio_new_identity();
