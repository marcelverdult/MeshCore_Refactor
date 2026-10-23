#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <HeltecT190Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#ifdef DISPLAY_CLASS
#include <helpers/ui/ST7789Display.h>
#include <helpers/ui/MomentaryButton.h>
#endif

extern HeltecT190Board board;
#define RADIOLIB_TARGET_GLOBALS   // radio, radio_driver and rtc_clock come from RadioLibTarget.cpp
#define P_LORA_SPI_HOST FSPI   // the radio's own SPI bus
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
