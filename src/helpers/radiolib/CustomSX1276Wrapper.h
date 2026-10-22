#pragma once

#include "CustomSX1276.h"
#include "CustomChipWrapper.h"

#ifndef USE_SX1276
#define USE_SX1276
#endif

class CustomSX1276Wrapper : public CustomChipWrapper<CustomSX1276> {
public:
  CustomSX1276Wrapper(CustomSX1276& radio, mesh::MainBoard& board) : CustomChipWrapper(radio, board) { }

  // no receive deadlines: the chip class reads its modem status instead
  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    setModemParams(freq, bw, sf, cr);
  }

  float getCurrentRSSI() override {
    return chip()->getRSSI(false);
  }

  uint8_t getSpreadingFactor() const override { return chip()->spreadingFactor; }
};
