#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#include <Wire.h>

ThinkNodeM8Board board;

#ifdef DISPLAY_CLASS
  RotaryInputGPIO rotary_input(PIN_BUTTON1_A, PIN_BUTTON1_B);
  MomentaryButton backlight_btn(BACKLIGHT_BTN, 1000, true);
#endif
