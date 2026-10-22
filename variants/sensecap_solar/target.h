#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <SenseCapSolarBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

extern SenseCapSolarBoard board;
#define RADIOLIB_TARGET_GLOBALS   // radio, radio_driver and rtc_clock come from RadioLibTarget.cpp
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();

