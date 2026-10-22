#pragma once

#include "CustomChipWrapper.h"
#include "LR11x0Reset.h"

// The wrapper of the LR11x0 family: LR1110 and LR1121.
template <class Chip>
class CustomLR11x0Wrapper : public CustomChipWrapper<Chip> {
public:
  CustomLR11x0Wrapper(Chip& radio, mesh::MainBoard& board) : CustomChipWrapper<Chip>(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    this->setModemParams(freq, bw, sf, cr);
    this->setRxDeadlines(bw, sf, cr);
  }

  float getCurrentRSSI() override {
    float rssi = -110;
    this->chip()->getRssiInst(&rssi);
    return rssi;
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    auto airtime = RadioLibWrapper::getEstAirtimeFor(len_bytes);
    return airtime < 200 ? 200 : airtime;   // at least 200 millis
  }

  void onSendFinished() override {
    RadioLibWrapper::onSendFinished();
    this->_radio->setPreambleLength(this->preambleLengthForSF(getSpreadingFactor())); // overcomes weird issues with small and big pkts
  }

  uint8_t getSpreadingFactor() const override { return this->chip()->getSpreadingFactor(); }

  bool setRxBoostedGainMode(bool en) override {
    return this->chip()->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return this->chip()->getRxBoostedGainMode();
  }

  void doResetAGC() override { lr11x0ResetAGC(this->chip(), this->chip()->getFreqMHz(), getRxBoostedGainMode()); }
};
