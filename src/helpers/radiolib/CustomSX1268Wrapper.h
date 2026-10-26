#pragma once

#include "CustomSX1268.h"
#include "CustomSX126xWrapper.h"

#ifndef USE_SX1268
#define USE_SX1268
#endif

class CustomSX1268Wrapper : public CustomSX126xWrapper<CustomSX1268> {
public:
  CustomSX1268Wrapper(CustomSX1268& radio, mesh::MainBoard& board) : CustomSX126xWrapper(radio, board) { }

  bool setRxBoostedGainMode(bool en) override {
    return chip()->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return chip()->getRxBoostedGainMode();
  }
};
