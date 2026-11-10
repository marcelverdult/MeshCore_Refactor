#include <Arduino.h>
#include "target.h"

#include <helpers/MillisDeadline.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

MeshadventurerBoard board;

NmeaGpsDriver gps_driver(Serial1);
MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
MASensorManager sensors = MASensorManager(nmea);


void MASensorManager::start_gps() {
  MESH_DEBUG_PRINTLN("starting GPS");
  _location->setGpsOn(true);
}

void MASensorManager::stop_gps() {
  MESH_DEBUG_PRINTLN("stopping GPS");
  _location->setGpsOn(false);
}

bool MASensorManager::begin() {
  // the GPS UART (pins, baud rate) is now configured by gps_driver when the GPS is started
  return true;
}

bool MASensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if(requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void MASensorManager::loop() {
  static uint32_t next_gps_update = 0;
  _location->loop();
  if(mesh::millisHasPassed(millis(), next_gps_update) && _location->isGpsOn()) {
    if(_location->hasValidFix()) {
      node_lat = ((double)_location->getLatitude()) / 1000000.;
      node_lon = ((double)_location->getLongitude()) / 1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int MASensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* MASensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* MASensorManager::getSettingValue(int i) const {
  if(i == 0) {
    return _location->isGpsOn() ? "1" : "0";
  }
  return NULL;
}
bool MASensorManager::setSettingValue(const char* name, const char* value) {
  if(strcmp(name, "gps") == 0) {
    if(strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
