#include <Arduino.h>
#include "target.h"
#if defined(UI_HAS_ROTARY_INPUT)
#include "HeltecRC32RotaryInput.h"
#endif

HeltecRC32Board board;

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  NmeaGpsDriver gps_driver(Serial1, PIN_GPS_TX, PIN_GPS_RX, PIN_GPS_RESET,
                            PIN_GPS_EN);
  MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
#if defined(UI_HAS_ROTARY_INPUT)
  static HeltecRC32RotaryInput rotaryInputImpl(&board.periph_power);
  RotaryInput& rotary_input = rotaryInputImpl;
#endif
#endif
