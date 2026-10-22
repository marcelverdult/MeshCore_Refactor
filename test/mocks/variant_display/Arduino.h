#pragma once

// The common Arduino stand-in plus the pin API MomentaryButton and
// RefCountedDigitalPin use, for the suite that pins a variant's display and
// user button (cut E of note 0011). pinMode() is recorded; digitalRead()
// returns what the test set for the pin.

#include_next <Arduino.h>

#include <map>
#include <utility>
#include <vector>

#define LOW 0
#define HIGH 1
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09

inline std::vector<std::pair<int, int>> g_mock_pin_modes;
inline std::map<int, int> g_mock_pin_levels;

inline void pinMode(int pin, int mode) { g_mock_pin_modes.push_back({pin, mode}); }
inline int digitalRead(int pin) { return g_mock_pin_levels.count(pin) ? g_mock_pin_levels[pin] : HIGH; }
inline void digitalWrite(int pin, int level) { g_mock_pin_levels[pin] = level; }
inline int analogRead(int pin) { return 0; }
