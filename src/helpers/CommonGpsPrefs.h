#pragma once

#include "ConfigSerializer.h"
#include "SensorManager.h"

/**
 * \brief  The GPS settings every node role persists.
 *
 * Defined once here and inherited by both NodePrefs classes (see RadioPrefsFields
 * in CommonRadioPrefs.h for why inheritance is used).
 */
struct GPSPrefsFields {
  // ordered by size, so the struct has no padding inside
  uint32_t gps_interval = 0;      // read interval in seconds (legacy, only EnvironmentSensorManager uses it)
  uint32_t gps_period = 0;        // periodic acquisition interval in seconds, 0 = continuous
  int32_t gps_fence = -1;         // GPS_GUARD fence in metres, 0 = off, -1 = build default
  uint8_t gps_enabled = 0;        // boolean
  uint8_t gps_guard = 1;          // GPS_GUARD on/off
  uint8_t advert_loc_policy = 0;  // ADVERT_LOC_* : which location adverts carry
};

class CommonGpsPrefs : public ConfigSerializer {
  bool _is_dirty = false;

protected:
  CommonGpsPrefs() = default;

public:
  void markDirty() { _is_dirty = true; }
  void clearDirty() { _is_dirty = false; }
  bool isDirty() const { return _is_dirty; }

  virtual bool isGpsEnabled() const = 0;
  virtual void setGpsEnabled(bool enabled) = 0;
  virtual uint32_t getGpsPeriod() const = 0;
  virtual void setGpsPeriod(uint32_t seconds) = 0;

  // Reference position for the anti-spoofing guard (GPS_GUARD), e.g. the configured node position.
  virtual bool getGuardReference(double& lat, double& lon) const { return false; }
  // Saved fence radius for the guard; false = not set, use the build default.
  virtual bool getGuardFence(uint32_t& metres) const { return false; }
  virtual void setGuardFence(uint32_t metres) { }
  // Saved on/off state of the guard; false = not set, guard on.
  virtual bool getGuardEnabled(bool& enabled) const { return false; }
  virtual void setGuardEnabled(bool enabled) { }

  bool setGpsEnabled(SensorManager& sensors, bool enabled);
  void applyGuardReference(SensorManager& sensors) const;
  static void onClockSetManually(SensorManager& sensors);
  bool setGpsPeriod(SensorManager& sensors, uint32_t seconds);
  bool apply(SensorManager& sensors) const;
  bool handleCommand(const char* command, SensorManager& sensors, char* reply);
};

/**
 * \brief  The "gps" object of /prefs.json, bound to one GPSPrefsFields.
 *
 * Both NodePrefs classes own one. The optional position pointers give the anti-spoofing
 * guard (GPS_GUARD) its reference position; the server roles pass their node position.
 */
class FieldGpsPrefs : public CommonGpsPrefs {
  GPSPrefsFields* _f;
  const double* _lat;
  const double* _lon;

protected:
  // JSON keys and their order are the persisted format: do not rename or reorder.
  void structure() override {
    def("en", _f->gps_enabled);
    def("int", _f->gps_interval);
    def("period", _f->gps_period);
    def("adv_loc", _f->advert_loc_policy);
#ifdef GPS_GUARD
    def("fence", _f->gps_fence);
    def("guard", _f->gps_guard);
#endif
  }

public:
  explicit FieldGpsPrefs(GPSPrefsFields& fields, const double* lat = nullptr, const double* lon = nullptr)
    : _f(&fields), _lat(lat), _lon(lon) { }

  using CommonGpsPrefs::setGpsEnabled;
  using CommonGpsPrefs::setGpsPeriod;
  bool isGpsEnabled() const override { return _f->gps_enabled; }
  void setGpsEnabled(bool enabled) override { _f->gps_enabled = enabled; markDirty(); }
  uint32_t getGpsPeriod() const override { return _f->gps_period; }
  void setGpsPeriod(uint32_t seconds) override { _f->gps_period = seconds; markDirty(); }
  bool getGuardFence(uint32_t& metres) const override {
    if (_f->gps_fence < 0) return false;
    metres = (uint32_t)_f->gps_fence;
    return true;
  }
  void setGuardFence(uint32_t metres) override { _f->gps_fence = (int32_t)metres; markDirty(); }
  bool getGuardEnabled(bool& enabled) const override {
    enabled = _f->gps_guard != 0;
    return true;
  }
  void setGuardEnabled(bool enabled) override { _f->gps_guard = enabled ? 1 : 0; markDirty(); }
  bool getGuardReference(double& lat, double& lon) const override {
    if (_lat == nullptr || _lon == nullptr) return false;
    lat = *_lat;
    lon = *_lon;
    return true;
  }
};
