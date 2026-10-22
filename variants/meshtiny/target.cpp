#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

MeshtinyBoard board;

EnvironmentSensorManager sensors = EnvironmentSensorManager();

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
  MomentaryButton user_btn(ENCODER_PRESS, 1000, true, true);
  MomentaryButton joystick_left(ENCODER_LEFT, 1000, true, true);
  MomentaryButton joystick_right(ENCODER_RIGHT, 1000, true, true);
  MomentaryButton back_btn(PIN_SIDE_BUTTON, 1000, true, true);
#endif
