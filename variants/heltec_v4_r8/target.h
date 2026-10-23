#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <HeltecV4R8Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#ifdef DISPLAY_CLASS
  #ifdef HELTEC_V4_R8_OLED
    #include <helpers/ui/SSD1306Display.h>
  #elif defined(HELTEC_V4_R8_TFT)
    #include <helpers/ui/ST7789LCDDisplay.h>
  #endif
  #include <helpers/ui/MomentaryButton.h>
#endif

extern HeltecV4R8Board board;
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
