#pragma once

#include <cstdint>

// Stand-in for a variant's target.h, so code that reaches the global
// radio_driver (e.g. CommonRadioPrefs.cpp) builds and runs in the native env.
struct MockRadioDriver {
  int8_t tx_power = 0;
  int tx_power_calls = 0;
  bool rx_boosted = false;
  int rx_boost_calls = 0;
  bool rx_boost_supported = true;

  void setTxPower(int8_t dbm) { tx_power = dbm; tx_power_calls++; }
  bool setRxBoostedGainMode(bool en) { rx_boosted = en; rx_boost_calls++; return rx_boost_supported; }

  void reset() { *this = MockRadioDriver(); }
};

inline MockRadioDriver radio_driver;
