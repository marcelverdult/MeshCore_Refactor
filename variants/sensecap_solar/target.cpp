#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>
#ifdef ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif
SenseCapSolarBoard board;

#ifdef ENV_INCLUDE_GPS
MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors = EnvironmentSensorManager();
#endif
