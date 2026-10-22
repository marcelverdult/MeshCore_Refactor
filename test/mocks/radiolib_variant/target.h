#pragma once

// Stand-in for the target.h of a variant with a RadioLib radio, for the suites
// that build the shared variant code (src/helpers/radiolib/RadioLibTarget.cpp).
// Listed ahead of test/mocks, whose target.h stands in for a variant without one.
// As on a board, the environment sets WRAPPER_CLASS and the variant's target.cpp
// (here: the test) defines the globals declared below.

#include "variant_stubs.h"

extern WRAPPER_CLASS radio_driver;
extern TestRTCClock rtc_clock;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
