#pragma once

#include <helpers/ConfigSerializer.h>
#include <helpers/CommonRadioPrefs.h>
#include <helpers/CommonGPSPrefs.h>
#include <helpers/DynamicConfigSerializer.h>

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1
#define ADVERT_LOC_PREFS      2

#define LOOP_DETECT_OFF       0
#define LOOP_DETECT_MINIMAL   1
#define LOOP_DETECT_MODERATE  2
#define LOOP_DETECT_STRICT    3

// The version of the layout and meaning of /prefs.json. A file without the key is
// version 0. ServerMesh::begin() brings an older file up to this version once, on the
// first boot of a firmware that knows a newer one; see ServerMesh::upgradePrefs().
#define NODE_PREFS_VERSION    1

/**
 * \brief  Persisted settings of the server roles (repeater, room server, sensor),
 *         stored as /prefs.json and edited through CommonCLI.
 *
 * The radio and GPS settings are inherited from RadioPrefsFields and GPSPrefsFields,
 * which the companion radio's NodePrefs shares, so both read and write them the
 * same way. Every other setting is specific to the server roles and lives below.
 */
class NodePrefs : public ConfigSerializer, public RadioPrefsFields, public GPSPrefsFields {
public:
  // in-memory backing data (radio and GPS fields are inherited, see above)
  char node_name[32];
  double node_lat = 0, node_lon = 0;
  char password[16];
  uint8_t disable_fwd = 0;
  uint8_t advert_interval = 0;       // minutes / 2
  uint8_t flood_advert_interval = 0; // hours
  char guest_password[16];
  uint32_t guard;
  uint8_t allow_read_only = 0;
  uint8_t flood_max = 0;
  uint8_t flood_max_unscoped = 0;
  uint8_t flood_max_advert = 0;
  // Bridge settings
  uint8_t bridge_enabled = 0; // boolean
  uint16_t bridge_delay = 0;  // milliseconds (default 500 ms)
  uint8_t bridge_pkt_src = 0; // 0 = logTx, 1 = logRx (default logTx)
  uint32_t bridge_baud = 0;   // 9600, 19200, 38400, 57600, 115200 (default 115200)
  uint8_t bridge_channel = 0; // 1-14 (ESP-NOW only)
  char bridge_secret[16]; // for XOR encryption of bridge packets (ESP-NOW only)
  // Power setting
  uint8_t powersaving_enabled = 0; // boolean
  uint32_t discovery_mod_timestamp = 0;
  float adc_multiplier = 0;
  char owner_info[120];
  uint8_t loop_detect = 0;
  uint8_t extra_sf[4];
  uint8_t prefs_version = 0;  // 0 until loaded or upgraded, see NODE_PREFS_VERSION

private:
  CommonRadioPrefs radio;

  class BridgePrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->bridge_enabled); // boolean
      def("delay", _parent->bridge_delay);  // milliseconds (default 500 ms)
      def("src", _parent->bridge_pkt_src); // 0 = logTx, 1 = logRx (default logTx)
      def("baud", _parent->bridge_baud);   // 9600, 19200, 38400, 57600, 115200 (default 115200)
      def("ch", _parent->bridge_channel); // 1-14 (ESP-NOW only)
      def("secret", _parent->bridge_secret, sizeof(_parent->bridge_secret)); // for XOR encryption of bridge packets (ESP-NOW only)
    }
  public:
    BridgePrefs(NodePrefs* parent) : _parent(parent) { }
  };
  BridgePrefs bridge;

  CommonGPSPrefs gps;

  class PowerPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("adc_mult", _parent->adc_multiplier);
      def("pwr_sav_en", _parent->powersaving_enabled);
    }
  public:
    PowerPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  PowerPrefs power;

  class RepeatPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("disable", _parent->disable_fwd);
      def("f_max", _parent->flood_max);
      def("f_max_uns", _parent->flood_max_unscoped);
      def("f_max_adv", _parent->flood_max_advert);
      def("loop", _parent->loop_detect);
    }
  public:
    RepeatPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RepeatPrefs repeat;

  class RoomPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("rd_only", _parent->allow_read_only);
    }
  public:
    RoomPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RoomPrefs room;

  DynamicConfigSerializer custom;

protected:
  void structure() override {
    def("ver", prefs_version);
    def("name", node_name, sizeof(node_name));
    def("pass", password, sizeof(password));
    def("guest", guest_password, sizeof(guest_password));
    def("owner", owner_info, sizeof(owner_info));
    def("adv_int", advert_interval);
    def("f_adv_int", flood_advert_interval);
    def("disc_mod", discovery_mod_timestamp);
    // A fixed size byte array, encoded as hex like the companion's scope key.
    // Top level rather than in the "radio" object, because that object is bound
    // to RadioPrefsFields and extra_sf is a field of NodePrefs.
    def("extra_sf", (void *) extra_sf, sizeof(extra_sf));
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("bridge", bridge);
    def("gps", gps);
    def("repeat", repeat);
    def("room", room);
    def("power", power);
    def("custom", custom);
  }

public:
  NodePrefs() : ConfigSerializer(), bridge(this), gps(*this), radio(*this), power(this), repeat(this), room(this), custom(&radio) {
    node_name[0] = 0;
    password[0] = 0;
    guest_password[0] = 0;
    bridge_secret[0] = 0;
    owner_info[0] = 0;
    memset(extra_sf, 0, sizeof(extra_sf));
  }

  CommonRadioPrefs* getRadioPrefs() { return &radio; }
  KeyValueStore* getCustom() { return &custom; }

  bool isDirty() const override { return ConfigSerializer::isDirty() || radio.isDirty() || custom.isDirty(); }
  void clearDirty() override { ConfigSerializer::clearDirty(); radio.clearDirty(); custom.clearDirty(); }
};
