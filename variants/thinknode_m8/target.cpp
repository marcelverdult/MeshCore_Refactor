#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <Wire.h>

ThinkNodeM8Board board;

#ifdef ENV_INCLUDE_GPS
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors = EnvironmentSensorManager();
#endif

#ifdef DISPLAY_CLASS
  RotaryInputGPIO rotary_input(PIN_BUTTON1_A, PIN_BUTTON1_B);
  MomentaryButton backlight_btn(BACKLIGHT_BTN, 1000, true);
#endif
