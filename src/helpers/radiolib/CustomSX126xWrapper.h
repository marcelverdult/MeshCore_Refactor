#pragma once

#include "CustomChipWrapper.h"
#include "SX126xReset.h"

// The wrapper of the SX126x family: SX1262, SX1268, LLCC68 and STM32WLx.
// Boosted gain is not here, because STM32WLx has no override for it.
template <class Chip>
class CustomSX126xWrapper : public CustomChipWrapper<Chip> {
public:
  CustomSX126xWrapper(Chip& radio, mesh::MainBoard& board) : CustomChipWrapper<Chip>(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    this->setModemParams(freq, bw, sf, cr);
    this->setRxDeadlines(bw, sf, cr);
  }

  float getCurrentRSSI() override {
    return this->chip()->getRSSI(false);
  }

  uint8_t getSpreadingFactor() const override { return this->chip()->spreadingFactor; }

  void doResetAGC() override { sx126xResetAGC(this->chip(), this->getRxBoostedGainMode()); }
};
