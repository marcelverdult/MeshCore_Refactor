#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

GAT562MeshTrackerProBoard board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif


#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, false, false);
  MomentaryButton joystick_left(JOYSTICK_LEFT, 1000, true, false, false);
  MomentaryButton joystick_right(JOYSTICK_RIGHT, 1000, true, false, false);
  MomentaryButton back_btn(PIN_BACK_BTN, 1000, true, false, true);
#endif


#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif
