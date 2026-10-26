#include <Arduino.h>
#include "target.h"
#if defined(UI_HAS_ROTARY_INPUT)
#include "HeltecRC32RotaryInput.h"
#endif

HeltecRC32Board board;

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock, PIN_GPS_RESET, PIN_GPS_EN);
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
