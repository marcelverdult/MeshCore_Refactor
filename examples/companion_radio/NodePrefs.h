#pragma once
#include <cstdint> // For uint8_t, uint32_t
#include <helpers/ConfigSerializer.h>
#include <helpers/CommonRadioPrefs.h>
#include <helpers/CommonGPSPrefs.h>
#include <helpers/DynamicConfigSerializer.h>

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

/**
 * \brief  Persisted settings of the companion radio, stored as /prefs.json.
 *
 * The radio and GPS settings are inherited from RadioPrefsFields and GPSPrefsFields,
 * which the server roles' NodePrefs (helpers/CommonNodePrefs.h) shares, so both read
 * and write them the same way. Every other setting is companion specific.
 */
class NodePrefs : public ConfigSerializer, public RadioPrefsFields, public GPSPrefsFields {  // persisted to file
public:
  char node_name[32];
  double node_lat = 0, node_lon = 0;
  uint8_t manual_add_contacts = 0;
  uint8_t telemetry_mode_base = 0;
  uint8_t telemetry_mode_loc = 0;
  uint8_t telemetry_mode_env = 0;
  uint32_t ble_pin = 0;
  uint8_t  buzzer_quiet = 0;
  uint8_t  vibe_quiet = 0;
  uint8_t autoadd_config = 0;    // bitmask for auto-add contacts config
  uint8_t _client_repeat = 0;  // DEPRECATED -> use repeat.disable_fwd
  uint8_t autoadd_max_hops = 0;  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)
  char default_scope_name[31];
  uint8_t default_scope_key[16];
  float tz_offset = 0;
#ifdef ENABLE_WIFI_INTERFACE
  #ifndef WIFI_SSID
    #define WIFI_SSID ""
  #endif
  char wifi_ssid[33] = {0};   // if empty, the compile-time WIFI_SSID is used
  char wifi_pwd[64] = {0};
  uint8_t wifi_enabled = 1; // enabled by default to allow wifi only builds to work. wifi won't be started if ssid is empty
  // use ssid from prefs, or fallback to ssid from build flags
  const char* getWifiSSID() const { return wifi_ssid[0] ? wifi_ssid : WIFI_SSID; }
#endif

private:
  CommonRadioPrefs radio;

  CommonGPSPrefs gps;

  class RepeatPrefs : public ConfigSerializer {  // COPIED from CommonCLI (for now)
  public:
    uint8_t disable_fwd = 1;
  protected:
    void structure() override {
      def("disable", disable_fwd);
      //def("f_max", flood_max);
      //def("f_max_uns", flood_max_unscoped);
      //def("f_max_adv", flood_max_advert);
      //def("loop", loop_detect);
    }
  };
  RepeatPrefs repeat;

  class CompanionPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("auto_max", _parent->autoadd_max_hops);  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)
      def("defs_nm", _parent->default_scope_name, sizeof(_parent->default_scope_name));
      def("defs_key", (void *) _parent->default_scope_key, sizeof(_parent->default_scope_key));
      def("pin", _parent->ble_pin);
      def("buzz_q", _parent->buzzer_quiet);
      def("vibe_q", _parent->vibe_quiet);
      def("auto_add", _parent->autoadd_config);    // bitmask for auto-add contacts config
      def("man_add", _parent->manual_add_contacts);
      def("tel_base", _parent->telemetry_mode_base);
      def("tel_loc", _parent->telemetry_mode_loc);
      def("tel_env", _parent->telemetry_mode_env);
      def("tz_offset", _parent->tz_offset);
    }
  public:
    CompanionPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  CompanionPrefs companion;

  DynamicConfigSerializer custom;

#ifdef ENABLE_WIFI_INTERFACE
  class WiFiPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("ssid", _parent->wifi_ssid, sizeof(_parent->wifi_ssid));
      def("pwd", _parent->wifi_pwd, sizeof(_parent->wifi_pwd));
      def("enabled", _parent->wifi_enabled);
    }
  public:
    WiFiPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  WiFiPrefs wifi;
#endif

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    //def("adv_int", advert_interval);
    //def("f_adv_int", flood_advert_interval);
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("gps", gps);
    def("repeat", repeat);
    def("comp", companion);
    def("custom", custom);
#ifdef ENABLE_WIFI_INTERFACE
    def("wifi", wifi);
#endif
  }
public:
  NodePrefs() : radio(*this), gps(*this), companion(this), custom(&radio)
#ifdef ENABLE_WIFI_INTERFACE
    , wifi(this)
#endif
  {
    node_name[0] = 0;
    default_scope_name[0] = 0;
    memset(default_scope_key, 0, sizeof(default_scope_key));
  }
  // new accessor methods
  bool isRepeatEn() const { return repeat.disable_fwd == 0; }
  void setRepeatEn(bool en) { repeat.disable_fwd = en ? 0 : 1; }

  CommonRadioPrefs* getRadioPrefs() { return &radio; }
  KeyValueStore* getCustom() { return &custom; }

  bool isDirty() const override { return ConfigSerializer::isDirty() || radio.isDirty() || custom.isDirty(); }
  void clearDirty() override { ConfigSerializer::clearDirty(); radio.clearDirty(); custom.clearDirty(); }
};
