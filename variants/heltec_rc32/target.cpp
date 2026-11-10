#include <Arduino.h>
#include "target.h"
#if defined(UI_HAS_ROTARY_INPUT)
#include "HeltecRC32RotaryInput.h"
#endif

HeltecRC32Board board;

#ifdef DISPLAY_CLASS
#if defined(UI_HAS_ROTARY_INPUT)
  static HeltecRC32RotaryInput rotaryInputImpl(&board.periph_power);
  RotaryInput& rotary_input = rotaryInputImpl;
#endif
#endif
