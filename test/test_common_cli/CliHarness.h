#pragma once

// Host harness for the text CLI (src/helpers/CommonCLI.cpp), step 13a of the
// refactoring series (docs/refactoring/0013-cli-command-table.md).
//
// CommonCLI is built from the real source. Everything it talks to is a recording
// stand-in: the board, the clock, the sensor manager with its location provider,
// the role callbacks, the identity, and the in-memory file system for the types.
// RegionMap and TransportKeyStore are the real classes, because the region
// commands only mean something against them.
//
// A test says `run("get name")` and gets the reply text back. The reply buffer is
// 160 bytes, like the ones the roles pass in, with guard bytes on both sides, so a
// command that writes beyond it fails the test. Before each command the buffer holds
// the text "<untouched>", so a command that leaves the reply alone is visible too.

#include <gtest/gtest.h>

#include <cstdio>
#include <new>
#include <cstring>
#include <string>
#include <vector>

#include "target.h"   // radio_driver, which CommonRadioPrefs.cpp reaches directly
#include <helpers/CommonCLI.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/sensors/LocationProvider.h>

#include <RTClib.h>

// ---- identity -----------------------------------------------------------------
// The identity stubs (identity_stubs.cpp) read these. Deterministic, not
// cryptographic: the private key is accepted if validate_ok is set, and the public key
// derived from it is the first 32 bytes XOR 0x5A.
struct IdentityStubControl {
  bool validate_ok = true;
  int validate_calls = 0;
  uint8_t last_validated[PRV_KEY_SIZE] = {0};
  int read_from_calls = 0;
};
IdentityStubControl& identityControl();

// ---- board --------------------------------------------------------------------
class CliBoard : public mesh::MainBoard {
public:
  int power_off_calls = 0;
  int reboot_calls = 0;

  uint16_t batt_mv = 3987;
  std::string manufacturer = "Acme Radio";

  bool usb_detect = false, usb_powered = false;
  bool solar_detect = false, solar_active = false;
  bool ext_powered = false, charger = false;

  float adc = 0.0f;                 // what getAdcMultiplier() reports
  bool adc_supported = true;        // what setAdcMultiplier() answers
  std::vector<float> adc_set;       // every value setAdcMultiplier() was given

  bool ota_ok = false;
  std::string ota_reply;            // what startOTAUpdate() writes into reply
  std::vector<std::string> ota_ids;

  bool bootloader_ok = false;
  std::string bootloader_ver = "";

  uint32_t reset_reason = 3;
  uint8_t shutdown_reason = 4;
  uint16_t boot_mv = 3500;

  // The variant hook, called before the generic chain.
  bool hook_handles = false;
  std::string hook_reply;
  std::vector<std::string> hook_commands;
  std::vector<uint32_t> hook_timestamps;
  // Set by a hook that changes a pref, to see the "save if dirty" step after it.
  NodePrefs* hook_dirty_prefs = nullptr;

  uint16_t getBattMilliVolts() override { return batt_mv; }
  const char* getManufacturerName() const override { return manufacturer.c_str(); }
  void reboot() override { reboot_calls++; }
  void powerOff() override { power_off_calls++; }
  uint8_t getStartupReason() const override { return 0; }

  bool setAdcMultiplier(float m) override { adc_set.push_back(m); return adc_supported; }
  float getAdcMultiplier() const override { return adc; }

  bool startOTAUpdate(const char* id, char reply[]) override {
    ota_ids.push_back(id);
    strcpy(reply, ota_reply.c_str());
    return ota_ok;
  }
  bool getBootloaderVersion(char* version, size_t max_len) override {
    if (!bootloader_ok) return false;
    snprintf(version, max_len, "%s", bootloader_ver.c_str());
    return true;
  }

  bool isExternalPowered() override { return ext_powered; }
  bool isChargerActive() override { return charger; }
  bool hasUsbPowerDetect() const override { return usb_detect; }
  bool isUsbPowered() override { return usb_powered; }
  bool hasSolarChargerDetect() const override { return solar_detect; }
  bool isSolarChargerActive() override { return solar_active; }
  uint16_t getBootVoltage() override { return boot_mv; }
  uint32_t getResetReason() const override { return reset_reason; }
  const char* getResetReasonString(uint32_t reason) override {
    static char s[32];
    snprintf(s, sizeof(s), "reset-%u", (unsigned)reason);
    return s;
  }
  uint8_t getShutdownReason() const override { return shutdown_reason; }
  const char* getShutdownReasonString(uint8_t reason) override {
    static char s[32];
    snprintf(s, sizeof(s), "shutdown-%u", (unsigned)reason);
    return s;
  }

  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) override {
    hook_commands.push_back(command);
    hook_timestamps.push_back(sender_timestamp);
    if (hook_dirty_prefs) hook_dirty_prefs->getRadioPrefs()->markDirty();
    if (!hook_handles) return false;
    strcpy(reply, hook_reply.c_str());
    return true;
  }
};

// ---- clock --------------------------------------------------------------------
class CliClock : public mesh::RTCClock {
public:
  uint32_t now = 1760000000;
  std::vector<uint32_t> set_calls;
  CliClock() : mesh::RTCClock() { }
  uint32_t getCurrentTime() override { return now; }
  void setCurrentTime(uint32_t t) override { set_calls.push_back(t); now = t; }
};

// ---- sensors ------------------------------------------------------------------
class CliLocation : public LocationProvider {
public:
  bool enabled = true, valid = true;
  long sats = 7;
  int sync_calls = 0;
  std::string diag = "diag-text";

  void syncTime() override { sync_calls++; }
  long getLatitude() override { return 0; }
  long getLongitude() override { return 0; }
  long getAltitude() override { return 0; }
  long satellitesCount() override { return sats; }
  bool isValid() override { return valid; }
  long getTimestamp() override { return 0; }
  void reset() override { }
  void begin() override { }
  void stop() override { }
  void loop() override { }
  bool isActive() override { return enabled; }
  void formatDiagnostics(char* out, size_t out_size) override { snprintf(out, out_size, "%s", diag.c_str()); }
};

class CliSensors : public SensorManager {
public:
  struct Setting { std::string name, value; };
  std::vector<Setting> settings;
  std::vector<std::string> set_calls;          // "name=value", in order
  bool set_accepts = true;                     // what setSettingValue() answers
  CliLocation* location = nullptr;

  int getNumSettings() const override { return (int)settings.size(); }
  const char* getSettingName(int i) const override { return settings[i].name.c_str(); }
  const char* getSettingValue(int i) const override { return settings[i].value.c_str(); }
  bool setSettingValue(const char* name, const char* value) override {
    set_calls.push_back(std::string(name) + "=" + value);
    if (!set_accepts) return false;
    for (auto& s : settings) {
      if (s.name == name) { s.value = value; return true; }
    }
    return true;   // accepted, but there is no such setting to update
  }
  LocationProvider* getLocationProvider() override { return location; }
};

// ---- role callbacks -------------------------------------------------------------
// Every call is appended to `calls` as one line, with the arguments that matter and,
// where the order against a prefs change matters, the value of the pref at that time.
class CliCallbacks : public CommonCLICallbacks {
public:
  NodePrefs* prefs = nullptr;
  mesh::LocalIdentity self_id;
  std::vector<std::string> calls;

  std::string fw_ver = "v9.9.9";
  std::string build_date = "01 Jan 2030";
  std::string role = "repeater";
  bool format_ok = true;
  bool save_regions_ok = true;
  bool rx_boost_ok = true;
  bool side_detectors_ok = true;
  std::string neighbors_reply = "neighbors-reply";
  std::string stats_reply = "stats-reply";
  std::string radio_stats_reply = "radio-stats-reply";
  std::string packet_stats_reply = "packet-stats-reply";

  std::vector<std::vector<uint8_t>> removed_neighbors;   // the key bytes passed
  std::vector<uint8_t> side_sfs;                          // last configSideDetectors() list

  void log(const std::string& s) { calls.push_back(s); }
  void clear() { calls.clear(); removed_neighbors.clear(); }

  void savePrefs() override {
    char b[64];
    snprintf(b, sizeof(b), "savePrefs advert_interval=%u", prefs ? (unsigned)prefs->advert_interval : 0u);
    log(b);
  }
  const char* getFirmwareVer() override { return fw_ver.c_str(); }
  const char* getBuildDate() override { return build_date.c_str(); }
  const char* getRole() override { return role.c_str(); }
  bool formatFileSystem() override { log("formatFileSystem"); return format_ok; }
  void sendSelfAdvertisement(int delay_millis, bool flood) override {
    log("sendSelfAdvertisement " + std::to_string(delay_millis) + (flood ? " flood" : " zerohop"));
  }
  void updateAdvertTimer() override {
    log("updateAdvertTimer advert_interval=" + std::to_string(prefs->advert_interval));
  }
  void updateFloodAdvertTimer() override {
    log("updateFloodAdvertTimer flood_advert_interval=" + std::to_string(prefs->flood_advert_interval));
  }
  void setLoggingOn(bool enable) override { log(enable ? "setLoggingOn true" : "setLoggingOn false"); }
  void eraseLogFile() override { log("eraseLogFile"); }
  void dumpLogFile() override { log("dumpLogFile"); }
  void setTxPower(int8_t p) override { log("setTxPower " + std::to_string(p)); }
  void formatNeighborsReply(char* reply) override { log("formatNeighborsReply"); strcpy(reply, neighbors_reply.c_str()); }
  void removeNeighbor(const uint8_t* pubkey, int key_len) override {
    log("removeNeighbor " + std::to_string(key_len));
    removed_neighbors.emplace_back(pubkey, pubkey + key_len);
  }
  void formatStatsReply(char* reply) override { log("formatStatsReply"); strcpy(reply, stats_reply.c_str()); }
  void formatRadioStatsReply(char* reply) override { log("formatRadioStatsReply"); strcpy(reply, radio_stats_reply.c_str()); }
  void formatPacketStatsReply(char* reply) override { log("formatPacketStatsReply"); strcpy(reply, packet_stats_reply.c_str()); }
  mesh::LocalIdentity& getSelfId() override { return self_id; }
  void saveIdentity(const mesh::LocalIdentity& new_id) override {
    char b[2 * PUB_KEY_SIZE + 16];
    strcpy(b, "saveIdentity ");
    mesh::Utils::toHex(b + 13, new_id.pub_key, PUB_KEY_SIZE);
    log(b);
  }
  void clearStats() override { log("clearStats"); }
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override {
    char b[96];
    snprintf(b, sizeof(b), "applyTempRadioParams %g %g %u %u %d", freq, bw, (unsigned)sf, (unsigned)cr, timeout_mins);
    log(b);
  }
  void startRegionsLoad() override { log("startRegionsLoad"); }
  bool saveRegions() override { log("saveRegions"); return save_regions_ok; }
  void onDefaultRegionChanged(const RegionEntry* r) override {
    log(std::string("onDefaultRegionChanged ") + (r ? r->name : "<null>"));
  }
  void setBridgeState(bool enable) override { log(enable ? "setBridgeState true" : "setBridgeState false"); }
  void restartBridge() override { log("restartBridge"); }
  bool setRxBoostedGain(bool enable) override {
    log(enable ? "setRxBoostedGain true" : "setRxBoostedGain false");
    return rx_boost_ok;
  }
#if defined(USE_LR2021)
  bool configSideDetectors(const uint8_t sfs[], uint8_t num, float bw) override {
    char b[64];
    snprintf(b, sizeof(b), "configSideDetectors num=%u bw=%g", (unsigned)num, bw);
    log(b);
    side_sfs.assign(sfs, sfs + num + 1);   // the list the CLI built, with its 0 terminator
    return side_detectors_ok;
  }
#endif
};

// ---- the fixture ----------------------------------------------------------------
class CliTest : public ::testing::Test {
protected:
  NodePrefs prefs;
  CliBoard brd;
  CliClock clk;
  CliSensors sens;
  CliLocation loc;
  TransportKeyStore tks;
  RegionMap regions;
  ClientACL acl;
  CliCallbacks cb;
  CommonCLI cli;

  CliTest()
    : regions(tks), cli(brd, clk, sens, regions, acl, &prefs, &cb) {
    cb.prefs = &prefs;
  }

  void SetUp() override {
    radio_driver.reset();
    g_events.clear();
    identityControl() = IdentityStubControl();
    resetPrefs();
  }

  // Known, distinct, non-default values, so a command that fails to change something
  // (or changes the wrong thing) is visible.
  void resetPrefs() {
    // Not `prefs = NodePrefs()`: NodePrefs holds sub objects that point back at their
    // owner, and a copy would leave them pointing at the temporary.
    prefs.~NodePrefs();
    new (&prefs) NodePrefs();
    strcpy(prefs.node_name, "Alpha");
    strcpy(prefs.password, "adminpw");
    strcpy(prefs.guest_password, "guestpw");
    prefs.node_lat = 51.5;
    prefs.node_lon = -0.25;
    prefs.advert_interval = 90;          // 180 minutes
    prefs.flood_advert_interval = 12;    // hours
    prefs.flood_max = 20;
    prefs.flood_max_unscoped = 21;
    prefs.flood_max_advert = 22;
    prefs.loop_detect = LOOP_DETECT_MODERATE;
    prefs.freq = 869.618f;
    prefs.bw = 62.5f;
    prefs.sf = 8;
    prefs.cr = 6;
    prefs.tx_power_dbm = 14;
    prefs.airtime_factor = 1.0f;
    prefs.rx_delay_base = 1.5f;
    prefs.tx_delay_factor = 0.5f;
    prefs.direct_tx_delay_factor = 0.25f;
    prefs.clearDirty();
  }

  // Runs one command and returns the reply. `ts` is the sender timestamp: 0 is the
  // serial console, anything else is a remote admin on the air.
  std::string run(const std::string& command, uint32_t ts = 0) {
    struct Guarded {
      char pre[16];
      char reply[160];
      char post[16];
    } g;
    memset(g.pre, 0xA5, sizeof(g.pre));
    memset(g.post, 0xA5, sizeof(g.post));
    memset(g.reply, 0, sizeof(g.reply));
    strcpy(g.reply, "<untouched>");

    // The CLI reads a few bytes past the end of short commands (memcmp against the
    // full name), so the buffer is zero filled well beyond the text.
    char buf[512];
    memset(buf, 0, sizeof(buf));
    EXPECT_LT(command.size(), sizeof(buf) - 8);
    memcpy(buf, command.data(), command.size());

    cli.handleCommand(ts, buf, g.reply);

    for (size_t i = 0; i < sizeof(g.pre); i++) {
      EXPECT_EQ((uint8_t)g.pre[i], 0xA5) << "wrote before the reply buffer: " << command;
      EXPECT_EQ((uint8_t)g.post[i], 0xA5) << "wrote past the reply buffer: " << command;
    }
    EXPECT_LT(strlen(g.reply), sizeof(g.reply)) << "reply not terminated: " << command;
    last_command_after = buf;
    return std::string(g.reply);
  }

  // The command buffer as the CLI left it (it edits the buffer in places).
  std::string last_command_after;

  // The recorded callback calls, then forgotten.
  std::vector<std::string> takeCalls() {
    std::vector<std::string> c = cb.calls;
    cb.calls.clear();
    return c;
  }

  // The expected text of a clock reply for `secs`, built with the same DateTime the
  // firmware uses. The mock DateTime (test/mocks/RTClib.h) counts from 2000, the real
  // library from 1970, so the digits are not pinned here, only the format and that
  // the right instant is used.
  static std::string clockText(uint32_t secs, const char* prefix) {
    DateTime dt(secs);
    char b[64];
    snprintf(b, sizeof(b), "%s%02d:%02d - %d/%d/%d UTC", prefix, dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    return b;
  }

  static std::string ftoa(float f) { return StrHelper::ftoa(f); }
};

using Calls = std::vector<std::string>;
