#pragma once

// Stand-in for the target.h of an ESP32 variant of cut D2 of note 0011, for the
// suite that pins its radio, wrapper and RTC globals and its radio_init(). The
// RTC clock is the real AutoDiscoverRTCClock (its out-of-line members are stood
// in by the test); helpers/ESP32Board.h, which the variants include through
// their board header, is a stand-in next to this file. The environment sets the pins, P_LORA_SCLK and ESP32,
// as the variants' build flags do. Listed ahead of test/mocks/radiolib_variant.

#include <helpers/ESP32Board.h>
#include "../radiolib_variant/variant_stubs.h"
#include <helpers/AutoDiscoverRTCClock.h>

extern TestBoard board;
#define RADIOLIB_TARGET_GLOBALS   // radio, radio_driver and rtc_clock come from RadioLibTarget.cpp
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
