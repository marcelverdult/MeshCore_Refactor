#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

IkokaNanoNRFBoard board;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  // MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

EnvironmentSensorManager sensors;
