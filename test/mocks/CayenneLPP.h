#pragma once

#include <cstddef>
#include <cstdint>

#define LPP_DIGITAL_INPUT        0
#define LPP_DIGITAL_OUTPUT       1
#define LPP_ANALOG_INPUT          2
#define LPP_ANALOG_OUTPUT         3
#define LPP_GENERIC_SENSOR      100
#define LPP_LUMINOSITY          101
#define LPP_PRESENCE            102
#define LPP_TEMPERATURE         103
#define LPP_RELATIVE_HUMIDITY   104
#define LPP_ACCELEROMETER       113
#define LPP_BAROMETRIC_PRESSURE 115
#define LPP_VOLTAGE             116
#define LPP_CURRENT             117
#define LPP_FREQUENCY           118
#define LPP_PERCENTAGE          120
#define LPP_ALTITUDE            121
#define LPP_POWER               128
#define LPP_DISTANCE            130
#define LPP_ENERGY              131
#define LPP_DIRECTION           132
#define LPP_GYROMETER           134
#define LPP_GPS                 136

class CayenneLPP {
public:
  explicit CayenneLPP(size_t) {}
  const uint8_t* getBuffer() const { return nullptr; }
  uint16_t getSize() const { return 0; }

  // The roles build a telemetry frame from these; the tests here do not inspect
  // the encoding, so recording the call count is enough.
  void reset() { adds = 0; }
  uint8_t addVoltage(uint8_t channel, float value) { adds++; return 0; }
  uint8_t addTemperature(uint8_t channel, float value) { adds++; return 0; }

  int adds = 0;
};
