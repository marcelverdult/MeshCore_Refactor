#pragma once

#include <RadioLib.h>
#include "RxActivityTracker.h"

class CustomSTM32WLx : public STM32WLx {
  RxActivityTracker _rxActivity;

  public:
    CustomSTM32WLx(STM32WLx_Module *mod) : STM32WLx(mod) { }

    int16_t startReceive() override {
      // include the PREAMBLE_DETECTED irq bit in reported flags
      return STM32WLx::startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED), RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    bool isReceiving() {
      uint32_t irq = getIrqFlags();
      RxActivityTracker::Result r = _rxActivity.update(
          irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED,
          irq & RADIOLIB_SX126X_IRQ_HEADER_VALID,
          irq & RADIOLIB_SX126X_IRQ_HEADER_ERR,
          millis());
      if (r == RxActivityTracker::CLEAR_PREAMBLE) {
        clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
      } else if (r == RxActivityTracker::CLEAR_ALL) {
        clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID | RADIOLIB_SX126X_IRQ_HEADER_ERR | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID);
      }
      return r == RxActivityTracker::RECEIVING;
    }

    void setPreambleMillis(uint32_t preambleMillis) { _rxActivity.setPreambleMillis(preambleMillis); }
    void setMaxPayloadMillis(uint32_t payloadMillis) { _rxActivity.setMaxPayloadMillis(payloadMillis); }

};