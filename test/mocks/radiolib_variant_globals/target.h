#pragma once

// Stand-in for the target.h of an nRF52 variant of cut D of note 0011, for the
// suite that pins its radio, wrapper and RTC globals. As in the variants, the
// RTC clock is the real AutoDiscoverRTCClock (its out-of-line members are stood
// in by the test), and the environment sets the pins and the platform, as the
// variants' build flags and variant.h do. Listed ahead of
// test/mocks/radiolib_variant, whose target.h stands in for a variant whose
// RTC clock is a recording stand-in.

#include "../radiolib_variant/variant_stubs.h"
#include <helpers/AutoDiscoverRTCClock.h>

extern TestBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
