#pragma once

#include <cstdint>

#include <Mesh.h>

#include "EventLog.h"

// A variant's target.h brings in the platform file system; the native env gets the
// in-memory stand-in, so FILESYSTEM and File are defined for the server helpers.
#include "FS.h"

// Stand-in for a variant's target.h, so code that reaches the global
// radio_driver (e.g. CommonRadioPrefs.cpp) builds and runs in the native env.
// On a board, radio_driver is a RadioLibWrapper, which is a mesh::Radio, and the
// role is constructed with it. Deriving here keeps that true in the native env, so
// a test can build a role exactly the way the role's main.cpp does.
struct MockRadioDriver : public mesh::Radio {
  int8_t tx_power = 0;
  int tx_power_calls = 0;
  bool rx_boosted = false;
  int rx_boost_calls = 0;
  bool rx_boost_supported = true;

  float freq = 0, bw = 0;
  uint8_t sf = 0, cr = 0;
  int set_params_calls = 0;
  float last_rssi = -90.0f, last_snr = 5.0f;
  uint32_t packets_recv = 0, packets_sent = 0, packets_recv_errors = 0;

  int begin_calls = 0;
  void begin() override { begin_calls++; logEvent("radio.begin"); }

  void setTxPower(int8_t dbm) { tx_power = dbm; tx_power_calls++; logEvent("radio.setTxPower"); }
  bool setRxBoostedGainMode(bool en) {
    rx_boosted = en; rx_boost_calls++;
    logEvent("radio.setRxBoostedGainMode");
    return rx_boost_supported;
  }
  bool getRxBoostedGainMode() const { return rx_boosted; }

  void setParams(float f, float b, uint8_t s, uint8_t c) {
    freq = f; bw = b; sf = s; cr = c; set_params_calls++;
    logEvent("radio.setParams");
  }

  float getLastRSSI() const { return last_rssi; }
  float getLastSNR() const { return last_snr; }
  uint32_t getPacketsRecv() const { return packets_recv; }
  uint32_t getPacketsSent() const { return packets_sent; }
  uint32_t getPacketsRecvErrors() const { return packets_recv_errors; }
  void resetStats() { packets_recv = packets_sent = packets_recv_errors = 0; }
  bool configSideDetectors(const uint8_t[], uint8_t, float) { return false; }

  // mesh::Radio, as in test_core_tuning: airtime grows with the length, so a wrong
  // length in a formula changes the result.
  int recvRaw(uint8_t* bytes, int sz) override { return 0; }
  uint32_t getEstAirtimeFor(int len_bytes) override { return 11 + 37 * (uint32_t)len_bytes; }
  float packetScore(float snr, int packet_len) override { return 0; }
  // A radio that accepts the frame, so a test can see that something was actually
  // transmitted and when. Set send_succeeds to false to model a refusing radio.
  bool send_succeeds = true;
  int send_calls = 0;
  int last_send_len = 0;
  uint8_t last_send[256] = {0};

  bool startSendRaw(const uint8_t* bytes, int len) override {
    if (!send_succeeds) return false;
    send_calls++;
    last_send_len = len;
    if (len > 0 && len <= (int)sizeof(last_send)) memcpy(last_send, bytes, len);
    return true;
  }
  bool isSendComplete() override { return true; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }

  void reset() {
    tx_power = 0; tx_power_calls = 0;
    rx_boosted = false; rx_boost_calls = 0; rx_boost_supported = true;
    freq = bw = 0; sf = cr = 0; set_params_calls = 0;
    begin_calls = 0;
    send_succeeds = true; send_calls = 0; last_send_len = 0;
    last_rssi = -90.0f; last_snr = 5.0f;
    packets_recv = packets_sent = packets_recv_errors = 0;
  }
};

inline MockRadioDriver radio_driver;

#include <MeshCore.h>
#include <helpers/KeyValueStore.h>
#include <helpers/SensorManager.h>

// A variant's target.h declares the board and the sensor manager as globals the
// role code reaches directly. The native env provides the same two names, so the
// real role implementations compile and run unchanged.

class MockBoard : public mesh::MainBoard {
public:
  uint16_t batt_milli_volts = 4100;
  float mcu_temperature = 25.0f;
  float adc_multiplier = 1.0f;
  uint32_t gpio = 0;
  int reboot_calls = 0;

  uint16_t getBattMilliVolts() override { return batt_milli_volts; }
  float getMCUTemperature() override { return mcu_temperature; }
  bool setAdcMultiplier(float m) override {
    adc_multiplier = m;
    logEvent("board.setAdcMultiplier");
    return true;
  }
  float getAdcMultiplier() const override { return adc_multiplier; }
  const char* getManufacturerName() const override { return "Mock"; }
  void reboot() override { reboot_calls++; }
  uint8_t getStartupReason() const override { return 0; }
  uint32_t getGpio() override { return gpio; }
  void setGpio(uint32_t values) override { gpio = values; }

  // Not on MainBoard: the concrete variant boards declare it, and the role code
  // calls it on the concrete type (see helpers/NRF52Board.h and its siblings).
  void attachDynamicPrefs(KeyValueStore* prefs) { logEvent("board.attachDynamicPrefs"); }

  void reset() {
    batt_milli_volts = 4100;
    mcu_temperature = 25.0f;
    adc_multiplier = 1.0f;
    gpio = 0;
    reboot_calls = 0;
  }
};

class MockRTCClock : public mesh::RTCClock {
public:
  uint32_t now = 1760000000;   // a fixed point in time, so replies stay repeatable
  uint32_t getCurrentTime() override { return now; }
  void setCurrentTime(uint32_t time) override { now = time; }
};

inline MockBoard board;
inline MockRTCClock rtc_clock;

// SensorManager is concrete, so the real class serves as the stand-in; this one only
// adds a record of the settings the role pushes to it (the GPS toggle at boot).
class MockSensorManager : public SensorManager {
public:
  bool setSettingValue(const char* name, const char* value) override {
    logEvent(std::string("sensors.setSettingValue ") + name + "=" + value);
    return true;
  }
};

inline MockSensorManager sensors;
