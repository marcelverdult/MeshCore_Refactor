#pragma once

#include "RadioLibWrappers.h"

// What every chip wrapper shares: access to its chip class and the calls that
// are the same for all of them. Chip is the Custom<chip> class the wrapper
// drives; it reaches PhysicalLayer by single inheritance, so chip() is the
// radio without any pointer adjustment.
template <class Chip>
class CustomChipWrapper : public RadioLibWrapper {
protected:
  Chip* chip() const { return static_cast<Chip*>(_radio); }

  // The first half of every setParams(): the four modem settings, then the
  // preamble length for the new spreading factor.
  void setModemParams(float freq, float bw, uint8_t sf, uint8_t cr) {
    chip()->setFrequency(freq);
    chip()->setSpreadingFactor(sf);
    chip()->setBandwidth(bw);
    chip()->setCodingRate(cr);
    updatePreamble(sf);
  }

  // The second half, for the chips with a receive tracker (all but SX1276):
  // the deadlines of the receive detection for the new parameters.
  void setRxDeadlines(float bw, uint8_t sf, uint8_t cr) {
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
    chip()->setPreambleMillis(pm.preambleMillis);
    chip()->setMaxPayloadMillis(pm.payloadMillis);
  }

public:
  CustomChipWrapper(Chip& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  bool isReceivingPacket() override {
    return chip()->isReceiving();
  }
  float getLastRSSI() const override { return chip()->getRSSI(); }
  float getLastSNR() const override { return chip()->getSNR(); }
};
