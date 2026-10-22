#include "target.h"
#include <Arduino.h>

HeltecE290Board board;

SensorManager sensors;

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display(&board.periph_power);
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
