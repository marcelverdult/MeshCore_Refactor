#include "target.h"

#include <Arduino.h>
#include <helpers/ArduinoHelpers.h>

#ifdef ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
#endif

T096Board board;

#if ENV_INCLUDE_GPS
#include <helpers/sensors/MicroNMEALocationProvider.h>
NmeaGpsDriver gps_driver(Serial1, PIN_GPS_TX, PIN_GPS_RX, GPS_RESET, GPS_EN,
                          &board.periph_power);
MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
EnvironmentSensorManager sensors;
#endif
