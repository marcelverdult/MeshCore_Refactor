#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

TechoCardBoard board;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  MomentaryButton back_btn(PIN_BUTTON2, 1000, true);
#endif
