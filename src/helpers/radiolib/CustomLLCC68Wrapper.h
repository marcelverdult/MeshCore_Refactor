#pragma once

#include "CustomLLCC68.h"
#include "CustomSX126xWrapper.h"

class CustomLLCC68Wrapper : public CustomSX126xWrapper<CustomLLCC68> {
public:
  CustomLLCC68Wrapper(CustomLLCC68& radio, mesh::MainBoard& board) : CustomSX126xWrapper(radio, board) { }

  bool setRxBoostedGainMode(bool en) override {
    return chip()->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return chip()->getRxBoostedGainMode();
  }
};
