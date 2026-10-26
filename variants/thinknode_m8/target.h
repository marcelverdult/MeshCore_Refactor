#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <ThinkNodeM8Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/GxEPDDisplay.h>
  #include <helpers/ui/RotaryInputGPIO.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern ThinkNodeM8Board board;
#define RADIOLIB_TARGET_GLOBALS   // radio, radio_driver and rtc_clock come from RadioLibTarget.cpp
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  #define DISPLAY_TARGET_GLOBALS   // display and user_btn come from VariantTarget.cpp
  extern MomentaryButton backlight_btn;
  extern RotaryInputGPIO rotary_input;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
