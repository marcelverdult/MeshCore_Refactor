#pragma once

// Minimal stand-in for RadioLib's PhysicalLayer, so the real RadioLibWrapper
// can be built and exercised on the host. Only the members the wrapper calls
// are present; each one records or returns test-visible state instead of
// touching hardware. Same idea as the Arduino and Mesh mocks next to it.

// The real RadioLib.h pulls in the Arduino core, and the wrapper relies on
// that for millis(), min(), max() and random(). Mirror it here.
#include "Arduino.h"

#include <cstddef>
#include <cstdint>

#define RADIOLIB_ERR_NONE      0
#define RADIOLIB_CHANNEL_FREE  (-2)

class PhysicalLayer {
public:
  virtual ~PhysicalLayer() = default;

  // State a test can set up or inspect.
  float rssi = -100.0f;
  float snr = 0.0f;
  uint32_t time_on_air = 1000;
  size_t packet_length = 0;
  int16_t scan_result = RADIOLIB_CHANNEL_FREE;
  size_t preamble_length = 0;
  int8_t output_power = 0;

  int16_t sleep() { return RADIOLIB_ERR_NONE; }
  int16_t standby() { return RADIOLIB_ERR_NONE; }
  int16_t startReceive() { return RADIOLIB_ERR_NONE; }
  int16_t startTransmit(uint8_t*, size_t) { return RADIOLIB_ERR_NONE; }
  int16_t finishTransmit() { return RADIOLIB_ERR_NONE; }
  size_t getPacketLength() { return packet_length; }
  int16_t readData(uint8_t*, size_t) { return RADIOLIB_ERR_NONE; }
  float getRSSI() { return rssi; }
  float getSNR() { return snr; }
  uint32_t getTimeOnAir(size_t) { return time_on_air; }
  int16_t scanChannel() { return scan_result; }
  int16_t setOutputPower(int8_t dbm) { output_power = dbm; return RADIOLIB_ERR_NONE; }
  void setPacketReceivedAction(void (*)(void)) { }
  int16_t setPreambleLength(size_t len) { preamble_length = len; return RADIOLIB_ERR_NONE; }
  uint8_t randomByte() { return 0x5A; }
  int32_t random(int32_t upper) { return upper / 2; }
};
