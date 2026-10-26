#pragma once

#include "CustomLR11x0.h"

struct CustomLR1121Traits {
  static const char* name() { return "LR1121"; }
};

// LR1121 picks its power amplifier by band, so it clips the power to the active
// PA's range and applies it again after a frequency change. LR1110 does neither;
// giving it this handling is a behaviour change of its own.
class CustomLR1121 : public CustomLR11x0<LR1121, CustomLR1121Traits> {
  int8_t _txPower = 0;
  bool _txPowerSet = false;

  public:
    CustomLR1121(Module *mod) : CustomLR11x0<LR1121, CustomLR1121Traits>(mod) { }

    using LR1121::setOutputPower;
    using LR1121::setFrequency;

    int16_t setOutputPower(int8_t power) override {
#ifdef LR1121_FORCE_HP_PA
      const bool force_hp = true;   // boards with only RFO_HP_LF wired: never select the LP PA
#else
      const bool force_hp = false;
#endif
      // clip to the active PA's range (HF PA tops out at 13 dBm) rather than fail and keep the other band's PA
      int8_t clipped = power;
      checkOutputPower(power, &clipped, force_hp);
      int16_t state = LR1121::setOutputPower(clipped, force_hp);
      if (state == RADIOLIB_ERR_NONE) {
        _txPower = power;
        _txPowerSet = true;
      }
      return state;
    }

    int16_t setFrequency(float freq) override {
      int16_t state = LR1121::setFrequency(freq);
      // PA selection depends on the band, and set radio / tempradio don't call setTxPower
      if (state == RADIOLIB_ERR_NONE && _txPowerSet) state = setOutputPower(_txPower);
      return state;
    }
};
