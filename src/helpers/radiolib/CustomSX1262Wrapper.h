#pragma once

#include "CustomSX1262.h"
#include "CustomSX126xWrapper.h"

#ifndef USE_SX1262
#define USE_SX1262
#endif

class CustomSX1262Wrapper : public CustomSX126xWrapper<CustomSX1262> {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : CustomSX126xWrapper(radio, board) { }

  virtual void powerOff() override {
    chip()->sleep(false);
  }

  bool setRxBoostedGainMode(bool en) override {
    return chip()->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return chip()->getRxBoostedGainMode();
  }
};
