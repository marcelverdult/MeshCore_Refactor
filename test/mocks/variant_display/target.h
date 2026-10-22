#pragma once

// Stand-in for the target.h of a variant with a display and a user button, for
// the suite that pins its display and user_btn globals (cut E of note 0011).
// The environment picks which variant's description it stands in for
// (TEST_DISPLAY_VARIANT, see platformio.ini); the display class records which
// constructor built it, the button is the real MomentaryButton.

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ui/MomentaryButton.h>

#define PIN_USER_BTN 7

class TestBoard {
public:
  RefCountedDigitalPin periph_power{9};
};

class TestDisplay {
public:
  bool default_constructed;
  RefCountedDigitalPin* periph_power;

  TestDisplay() : default_constructed(true), periph_power(nullptr) { }
  explicit TestDisplay(RefCountedDigitalPin* power) : default_constructed(false), periph_power(power) { }
};

#define DISPLAY_CLASS TestDisplay

extern TestBoard board;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif
