#include <Arduino.h>
#include "target.h"

RAK3112Board board;

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  NmeaGpsDriver gps_driver(Serial1);
  MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
