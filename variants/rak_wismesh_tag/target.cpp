#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

RAKWismeshTagBoard board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  NmeaGpsDriver gps_driver(Serial1);
  MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif
