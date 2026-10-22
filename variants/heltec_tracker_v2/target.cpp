#include <Arduino.h>
#include "target.h"

HeltecTrackerV2Board board;

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock, GPS_RESET, GPS_EN, &board.periph_power);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display(&board.periph_power);   // peripheral power pin is shared
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
