#include <Arduino.h>
#include "target.h"

ESP32Board board;

SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

#ifndef LORA_CR
  #define LORA_CR      5
#endif
