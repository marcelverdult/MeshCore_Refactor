#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

GAT56MeshWatch13Board board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif


#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, false, true);
#endif


EnvironmentSensorManager sensors;

