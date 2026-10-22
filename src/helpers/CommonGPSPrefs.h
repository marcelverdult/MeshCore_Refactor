#pragma once
#include "ConfigSerializer.h"

/**
 * \brief  The GPS settings every node role persists.
 *
 * Defined once here and inherited by both NodePrefs classes (see RadioPrefsFields
 * in CommonRadioPrefs.h for why inheritance is used).
 */
struct GPSPrefsFields {
  // ordered by size, so the struct has no padding inside
  uint32_t gps_interval = 0;      // read interval in seconds
  uint8_t gps_enabled = 0;        // boolean
  uint8_t advert_loc_policy = 0;  // ADVERT_LOC_* : which location adverts carry
};

/**
 * \brief  The "gps" object of /prefs.json, bound to one GPSPrefsFields.
 */
class CommonGPSPrefs : public ConfigSerializer {
  GPSPrefsFields* _f;

protected:
  // JSON keys and their order are the persisted format: do not rename or reorder.
  void structure() override {
    def("en", _f->gps_enabled);
    def("int", _f->gps_interval);
    def("adv_loc", _f->advert_loc_policy);
  }

public:
  explicit CommonGPSPrefs(GPSPrefsFields& fields) : _f(&fields) { }
};
