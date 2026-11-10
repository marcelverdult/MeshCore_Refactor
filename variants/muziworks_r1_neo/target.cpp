#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

R1NeoBoard board;

#ifdef DISPLAY_CLASS
  NullDisplayDriver display;
#endif

#ifdef PIN_USER_BTN 
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

