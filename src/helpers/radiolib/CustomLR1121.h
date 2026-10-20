#pragma once

#include <RadioLib.h>
#include "MeshCore.h"
#include "RxActivityTracker.h"

class CustomLR1121 : public LR1121 {
  RxActivityTracker _rxActivity;
  bool _rx_boosted = false;
  int8_t _txPower = 0;
  bool _txPowerSet = false;

  public:
    CustomLR1121(Module *mod) : LR1121(mod) { }

    int16_t begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7,
                  uint8_t syncWord = RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, int8_t power = 10,
                  uint16_t preambleLength = 8, float tcxoVoltage = 1.6) {
      int16_t state = LR1121::begin(freq, bw, sf, cr, syncWord, power, preambleLength,
                                    tcxoVoltage);
      // RadioLib begin() defaults to LDO; use the LR1121 DC/DC regulator.
      if (state == RADIOLIB_ERR_NONE) state = setRegulatorDCDC();
      return state;
    }

    size_t getPacketLength(bool update) override {
      size_t len = LR1121::getPacketLength(update);
      if (len == 0 && getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
        // we've just received a corrupted packet
        // this may have triggered a bug causing subsequent packets to be shifted
        // call standby() to return radio to known-good state
        // recvRaw will call startReceive() to restart rx
        MESH_DEBUG_PRINTLN("LR1121: got header err, calling standby()");
        standby();
      }
      return len;
    }
    
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

    float getFreqMHz() const { return freqMHz; }

    int16_t setRxBoostedGainMode(bool en) {
      _rx_boosted = en;
      return LR1121::setRxBoostedGainMode(en);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    int16_t startReceive() override {
      // include the PREAMBLE_DETECTED irq bit in reported flags.
      return LR1121::startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED), RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    bool isReceiving() {
      uint32_t irq = getIrqStatus();
      RxActivityTracker::Result r = _rxActivity.update(
          irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED,
          irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID,
          irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR,
          millis());
      if (r == RxActivityTracker::CLEAR_PREAMBLE) {
        clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
      } else if (r == RxActivityTracker::CLEAR_ALL) {
        clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_HEADER_ERR);
      }
      return r == RxActivityTracker::RECEIVING;
    }
    
    void setPreambleMillis(uint32_t preambleMillis) { _rxActivity.setPreambleMillis(preambleMillis); }
    void setMaxPayloadMillis(uint32_t payloadMillis) { _rxActivity.setMaxPayloadMillis(payloadMillis); }

    uint8_t getSpreadingFactor() const { return spreadingFactor; }
};