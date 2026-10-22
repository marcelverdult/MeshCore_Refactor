#include "target.h"
#include <Arduino.h>

HeltecWirelessPaperBoard board;

SensorManager sensors;

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif
