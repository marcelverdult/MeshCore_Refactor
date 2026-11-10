#include <Arduino.h>
#include "target.h"

#include <helpers/MillisDeadline.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

HeltecV3Board board;

// GPS_EN (GPIO35) drives N-ch MOSFET → P-ch high-side switch; GPS_RESET (GPIO36) active LOW
class HeltecTrackerGpsDriver : public NmeaGpsDriver {
public:
  HeltecTrackerGpsDriver()
    : NmeaGpsDriver(Serial1, PIN_GPS_TX, PIN_GPS_RX, GPS_RESET, GPS_EN,
                    &board.periph_power) {}

  void reset() override {
    NmeaGpsDriver::reset();
    // Send configuration after reset
    if (isActive()) {
      delay(250);
      sendNmeaSentence("$CFGSYS,h35155*68");
      // TODO: remove active wait (250ms is a lot) 
    }
  }
};

HeltecTrackerGpsDriver gps_driver;
MicroNMEALocationProvider nmea(gps_driver, &rtc_clock);
HWTSensorManager sensors = HWTSensorManager(nmea);

void HWTSensorManager::start_gps() {
  _location->setGpsOn(true);
}

void HWTSensorManager::stop_gps() {
  _location->setGpsOn(false);
}

bool HWTSensorManager::begin() {
  // the GPS UART (pins, baud rate) is now configured by gps_driver when the GPS is started
  return true;
}

bool HWTSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if (requester_permissions & TELEM_PERM_LOCATION) {   // does requester have permission?
    telemetry.addGPS(TELEM_CHANNEL_SELF, node_lat, node_lon, node_altitude);
  }
  return true;
}

void HWTSensorManager::loop() {
  static uint32_t next_gps_update = 0;

  _location->loop();

  if (mesh::millisHasPassed(millis(), next_gps_update)) {
    if (_location->isGpsOn() && _location->hasValidFix()) {
      node_lat = ((double)_location->getLatitude())/1000000.;
      node_lon = ((double)_location->getLongitude())/1000000.;
      node_altitude = ((double)_location->getAltitude()) / 1000.0;
      MESH_DEBUG_PRINTLN("lat %f lon %f", node_lat, node_lon);
    }
    next_gps_update = millis() + 1000;
  }
}

int HWTSensorManager::getNumSettings() const { return 1; }  // just one supported: "gps" (power switch)

const char* HWTSensorManager::getSettingName(int i) const {
  return i == 0 ? "gps" : NULL;
}
const char* HWTSensorManager::getSettingValue(int i) const {
  if (i == 0) {
    return _location->isGpsOn() ? "1" : "0";
  }
  return NULL;
}
bool HWTSensorManager::setSettingValue(const char* name, const char* value) {
  if (strcmp(name, "gps") == 0) {
    if (strcmp(value, "0") == 0) {
      stop_gps();
    } else {
      start_gps();
    }
    return true;
  }
  return false;  // not supported
}
