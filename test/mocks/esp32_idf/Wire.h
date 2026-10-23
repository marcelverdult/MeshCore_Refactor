#pragma once

// Stand-in for the Arduino Wire library, as far as ESP32Board::begin() uses it.
class TwoWire {
public:
  bool begin() { return true; }
  bool begin(int sda, int scl) { return true; }
};

inline TwoWire Wire;
