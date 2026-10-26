#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

TechoBoard board;

#ifdef ENV_INCLUDE_GPS
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors = EnvironmentSensorManager();
#endif

#ifdef DISPLAY_CLASS
  MomentaryButton backlight_btn(BACKLIGHT_BTN, 1000, true);   // own global, not part of DISPLAY_TARGET_GLOBALS
#endif
