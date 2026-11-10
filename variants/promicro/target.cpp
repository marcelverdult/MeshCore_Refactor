#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

PromicroBoard board;

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  #ifdef GPS_BAUD_PROBE_LIST  // e.g. -D GPS_BAUD_PROBE_LIST=9600,38400,115200
    static const uint32_t gps_probe_bauds[] = { GPS_BAUD_PROBE_LIST };
    NmeaGpsDriver gps_driver(Serial1, gps_probe_bauds, sizeof(gps_probe_bauds) / sizeof(gps_probe_bauds[0]));
  #else
    NmeaGpsDriver gps_driver(Serial1);
  #endif
  MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif
