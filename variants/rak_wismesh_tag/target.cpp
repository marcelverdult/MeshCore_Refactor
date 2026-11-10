#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

RAKWismeshTagBoard board;

#ifndef PIN_USER_BTN
  #define PIN_USER_BTN (-1)
#endif

