#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

WioTrackerL1Board board;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, false, false);
  MomentaryButton joystick_left(JOYSTICK_LEFT, 1000, true, false, false);
  MomentaryButton joystick_right(JOYSTICK_RIGHT, 1000, true, false, false);
  MomentaryButton back_btn(PIN_BACK_BTN, 1000, true, false, true);
#endif
