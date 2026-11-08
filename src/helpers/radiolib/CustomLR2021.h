#pragma once

#include <RadioLib.h>
#include "MeshCore.h"
#include "RxActivityTracker.h"

class CustomLR2021 : public LR2021 {
  RxActivityTracker _rxActivity;
  // last applied gain level per RX path, mirroring RadioLib's gainModeLf/gainModeHf defaults
  uint8_t _rx_gain_lf = RADIOLIB_LR2021_RX_BOOST_LF;
  uint8_t _rx_gain_hf = RADIOLIB_LR2021_RX_BOOST_HF;

  public:
    CustomLR2021(Module *mod) : LR2021(mod) { irqDioNum = LR2021_IRQ_DIO; }

    bool std_init(SPIClass* spi = NULL)
    {
      
  #ifdef LR2021_TCXO_VOLTAGE
      float tcxo = LR2021_TCXO_VOLTAGE;
  #else
      float tcxo = 1.6f;
  #endif

  #ifdef LORA_CR
      uint8_t cr = LORA_CR;
  #else
      uint8_t cr = 5;
  #endif

  #if defined(P_LORA_SCLK)
    #ifdef NRF52_PLATFORM
      if (spi) { spi->setPins(P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI); spi->begin(); }
    #elif defined(RP2040_PLATFORM)
      if (spi) {
        spi->setMISO(P_LORA_MISO);
        //spi->setCS(P_LORA_NSS); // Setting CS results in freeze
        spi->setSCK(P_LORA_SCLK);
        spi->setMOSI(P_LORA_MOSI);
        spi->begin();
      }
    #else
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
    #endif
  #endif
      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
      // if radio init fails with -707/-706, try again with tcxo voltage set to 0.0f
      if (status == RADIOLIB_ERR_SPI_CMD_FAILED || status == RADIOLIB_ERR_SPI_CMD_INVALID) {
        tcxo = 0.0f;
        status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE, LORA_TX_POWER, 16, tcxo);
      }
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }
    
      setCRC(2);
      explicitHeader();

      
    #ifdef LR2021_RX_BOOSTED_GAIN
      setRxBoostedGainMode(LR2021_RX_BOOSTED_GAIN);
    #endif

      return true;  // success
    }
    
    float getFreqMHz() const { return freqMHz; }

    bool isHighFreqPath() const { return freqMHz > RADIOLIB_LR2021_LF_CUTOFF_FREQ; }

    int16_t setRxBoostedGainMode(uint8_t level) {
      uint8_t& applied = isHighFreqPath() ? _rx_gain_hf : _rx_gain_lf;
      int16_t status = LR2021::setRxBoostedGainMode(level);
      if (status == RADIOLIB_ERR_NONE) {
        applied = level;
      } else {
        // RadioLib caches the level before sending it; restore it so the next startReceive keeps the old gain
        LR2021::setRxBoostedGainMode(applied);
      }
      return status;
    }

    bool getRxBoostedGainMode() const { return (isHighFreqPath() ? _rx_gain_hf : _rx_gain_lf) != 0; }

    int16_t startReceive() override {
      // include the PREAMBLE_DETECTED irq bit in reported flags
      return LR2021::startReceive(RADIOLIB_LR2021_RX_TIMEOUT_INF, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED), RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
    }

    bool isReceiving() {
      uint32_t irq = getIrqStatus();
      RxActivityTracker::Result r = _rxActivity.update(
          irq & RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED,
          irq & RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID,
          irq & RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR,
          millis());
      if (r == RxActivityTracker::CLEAR_PREAMBLE) {
        clearIrqFlags(RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED);
      } else if (r == RxActivityTracker::CLEAR_ALL) {
        clearIrqFlags(RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID | RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR);
      }
      return r == RxActivityTracker::RECEIVING;
    }
    
    void setPreambleMillis(uint32_t preambleMillis) { _rxActivity.setPreambleMillis(preambleMillis); }
    void setMaxPayloadMillis(uint32_t payloadMillis) { _rxActivity.setMaxPayloadMillis(payloadMillis); }


    uint8_t getSpreadingFactor() const { return spreadingFactor; }
};