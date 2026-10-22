#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#endif

XiaoNrf52Board board;

EnvironmentSensorManager sensors;
