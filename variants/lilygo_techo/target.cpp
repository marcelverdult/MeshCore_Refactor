#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

TechoBoard board;

#ifdef DISPLAY_CLASS
  MomentaryButton backlight_btn(BACKLIGHT_BTN, 1000, true);   // own global, not part of DISPLAY_TARGET_GLOBALS
#endif
