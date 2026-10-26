#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <T1Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/LocationProvider.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/MomentaryButton.h>
#include <helpers/ui/ST7735Display.h>
#else
#include "helpers/ui/NullDisplayDriver.h"
#endif

extern T1Board board;
#define RADIOLIB_TARGET_GLOBALS   // radio, radio_driver and rtc_clock come from RadioLibTarget.cpp
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#define DISPLAY_TARGET_GLOBALS   // display and user_btn come from VariantTarget.cpp
#define DISPLAY_PERIPH_POWER &board.periph_power
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
