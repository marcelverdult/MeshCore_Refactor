// The parts of a variant's target.cpp that are not about the radio, defined
// once here from the board's description, for the variants whose target.h asks
// for them. Like the radio globals in radiolib/RadioLibTarget.cpp, and for the
// same reason, the objects are not weak: a weak object is still constructed
// here, into the variant's object where the variant defines its own, so a
// variant that keeps its own simply does not define the macro. See
// docs/refactoring/0011-variant-target.md, "Cut E".
#include "target.h"

// The display and the user button, for the variants whose target.h defines
// DISPLAY_TARGET_GLOBALS. Built from the board's description: DISPLAY_CLASS,
// DISPLAY_PERIPH_POWER (the argument of the display's constructor where the
// variant passes one: the board's peripheral power pin, or NULL), and the
// button on PIN_USER_BTN, active low, with the pin's pull resistor where
// USER_BTN_PULL is defined.
#if defined(DISPLAY_TARGET_GLOBALS) && defined(DISPLAY_CLASS)

#ifdef DISPLAY_PERIPH_POWER
  DISPLAY_CLASS display(DISPLAY_PERIPH_POWER);
#else
  DISPLAY_CLASS display;
#endif

#ifdef USER_BTN_PULL
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);
#else
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

#endif
