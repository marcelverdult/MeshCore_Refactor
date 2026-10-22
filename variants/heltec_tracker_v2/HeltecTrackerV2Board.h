#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <helpers/FemRxGain.h>
#include "LoRaFEMControl.h"

class HeltecTrackerV2Board : public ESP32Board {
  friend class FemRxGain<HeltecTrackerV2Board>;
  FemRxGain<HeltecTrackerV2Board> fem_rxgain;

  bool setLoRaFemLnaEnabled(bool enable);
  bool isLoRaFemLnaEnabled() const;

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;

  HeltecTrackerV2Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override;

  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override ;

};
