#pragma once

#include <RadioLib.h>
#include "MeshCore.h"
#include "RxActivityTracker.h"

// The chip class shared by CustomLR1110 and CustomLR1121: Base is the RadioLib
// chip class, Traits names the chip in the debug output. CustomLR1121 adds its
// band dependent power handling on top (see docs/refactoring/0009-radio-chip-adapter.md,
// step 9d).
template <class Base, class Traits>
class CustomLR11x0 : public Base {
  RxActivityTracker _rxActivity;
  bool _rx_boosted = false;

  public:
    CustomLR11x0(Module *mod) : Base(mod) { }

    int16_t begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7,
                  uint8_t syncWord = RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE, int8_t power = 10,
                  uint16_t preambleLength = 8, float tcxoVoltage = 1.6) {
      int16_t state = Base::begin(freq, bw, sf, cr, syncWord, power, preambleLength,
                                  tcxoVoltage);
      // RadioLib begin() defaults to LDO; use the chip's DC/DC regulator.
      if (state == RADIOLIB_ERR_NONE) state = this->setRegulatorDCDC();
      return state;
    }

    size_t getPacketLength(bool update) override {
      size_t len = Base::getPacketLength(update);
      if (len == 0 && this->getIrqStatus() & RADIOLIB_LR11X0_IRQ_HEADER_ERR) {
        // we've just received a corrupted packet
        // this may have triggered a bug causing subsequent packets to be shifted
        // call standby() to return radio to known-good state
        // recvRaw will call startReceive() to restart rx
        MESH_DEBUG_PRINTLN("%s: got header err, calling standby()", Traits::name());
        this->standby();
      }
      return len;
    }

    float getFreqMHz() const { return this->freqMHz; }

    int16_t setRxBoostedGainMode(bool en) {
      _rx_boosted = en;
      return Base::setRxBoostedGainMode(en);
    }

    bool getRxBoostedGainMode() const { return _rx_boosted; }

    int16_t startReceive() override {
      // include the PREAMBLE_DETECTED irq bit in reported flags.
      return Base::startReceive(RADIOLIB_LR11X0_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED), RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    bool isReceiving() {
      uint32_t irq = this->getIrqStatus();
      RxActivityTracker::Result r = _rxActivity.update(
          irq & RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED,
          irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID,
          irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR,
          millis());
      if (r == RxActivityTracker::CLEAR_PREAMBLE) {
        this->clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED);
      } else if (r == RxActivityTracker::CLEAR_ALL) {
        this->clearIrqState(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID | RADIOLIB_LR11X0_IRQ_HEADER_ERR);
      }
      return r == RxActivityTracker::RECEIVING;
    }

    void setPreambleMillis(uint32_t preambleMillis) { _rxActivity.setPreambleMillis(preambleMillis); }
    void setMaxPayloadMillis(uint32_t payloadMillis) { _rxActivity.setMaxPayloadMillis(payloadMillis); }

    uint8_t getSpreadingFactor() const { return this->spreadingFactor; }
};
