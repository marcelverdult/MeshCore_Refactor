#include <Arduino.h>
#include "target.h"

HeltecV4Board board;

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  NmeaGpsDriver gps_driver(Serial1);
  MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif
