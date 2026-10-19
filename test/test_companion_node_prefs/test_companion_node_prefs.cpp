#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../examples/companion_radio/NodePrefs.h"

#include "PrefsStreams.h"
#include "RadioPrefsChecks.h"

// Characterization tests for the companion radio's NodePrefs. They pin down
// what /prefs.json contains and what the radio CLI does today, so a
// refactor of the prefs classes can prove it keeps both unchanged.

namespace {

void fillAll(NodePrefs& p) {
  fillRadioAndGps(p);
  strcpy(p.node_name, "Test Companion");
  p.node_lat = 53.551086;
  p.node_lon = 9.993682;
  p.setRepeatEn(true);
  p.autoadd_max_hops = 3;
  strcpy(p.default_scope_name, "de-hh");
  for (int i = 0; i < 16; i++) p.default_scope_key[i] = (uint8_t)(0xA0 + i);
  p.ble_pin = 123456;
  p.buzzer_quiet = 1;
  p.vibe_quiet = 1;
  p.autoadd_config = 0x05;
  p.manual_add_contacts = 1;
  p.telemetry_mode_base = 2;
  p.telemetry_mode_loc = 1;
  p.telemetry_mode_env = 2;
  p.tz_offset = 1.5f;
}

// Prefs file as written by v1.18.0 for the values of fillAll().
const char* kPrefsJson =
    "{name:\"Test Companion\",lat:53.551086,lon:9.993682,radio:{freq:869.6180,"
    "bw:62.5000,sf:8,cr:6,cad:1,int_thr:14,rxgain:1,fem_rxgain:1,fem_txgain:2,"
    "tx:21,af:1.5000,rxdelay:3.2500,f_txdelay:0.7500,d_txdelay:0.2500,"
    "agc_int:5,hash_mode:2,multi_ack:1},gps:{en:1,int:900,adv_loc:1},"
    "repeat:{disable:0},comp:{auto_max:3,defs_nm:\"de-hh\",defs_key:\"A0A1A2A3A4A5A6A7A8A9AAABACADAEAF\","
    "pin:123456,buzz_q:1,vibe_q:1,auto_add:5,man_add:1,tel_base:2,"
    "tel_loc:1,tel_env:2,tz_offset:1.5000},custom:{}}";

}  // namespace

TEST(CompanionNodePrefs, WritesTheKnownPrefsJson) {
  NodePrefs prefs;
  fillAll(prefs);

  CaptureStream out;
  ASSERT_TRUE(prefs.saveSerial(out));
  EXPECT_EQ(kPrefsJson, out.text());
}

TEST(CompanionNodePrefs, ReadsEveryPersistedFieldBack) {
  NodePrefs expected;
  fillAll(expected);

  NodePrefs loaded;
  ReplayStream in(kPrefsJson);
  ASSERT_TRUE(loaded.loadSerial(in));

  expectRadioAndGpsEqual(expected, loaded);
  EXPECT_STREQ(expected.node_name, loaded.node_name);
  EXPECT_DOUBLE_EQ(expected.node_lat, loaded.node_lat);
  EXPECT_DOUBLE_EQ(expected.node_lon, loaded.node_lon);
  EXPECT_EQ(expected.isRepeatEn(), loaded.isRepeatEn());
  EXPECT_EQ(expected.autoadd_max_hops, loaded.autoadd_max_hops);
  EXPECT_STREQ(expected.default_scope_name, loaded.default_scope_name);
  EXPECT_EQ(0, memcmp(expected.default_scope_key, loaded.default_scope_key, sizeof(loaded.default_scope_key)));
  EXPECT_EQ(expected.ble_pin, loaded.ble_pin);
  EXPECT_EQ(expected.buzzer_quiet, loaded.buzzer_quiet);
  EXPECT_EQ(expected.vibe_quiet, loaded.vibe_quiet);
  EXPECT_EQ(expected.autoadd_config, loaded.autoadd_config);
  EXPECT_EQ(expected.manual_add_contacts, loaded.manual_add_contacts);
  EXPECT_EQ(expected.telemetry_mode_base, loaded.telemetry_mode_base);
  EXPECT_EQ(expected.telemetry_mode_loc, loaded.telemetry_mode_loc);
  EXPECT_EQ(expected.telemetry_mode_env, loaded.telemetry_mode_env);
  EXPECT_FLOAT_EQ(expected.tz_offset, loaded.tz_offset);
}

TEST(CompanionNodePrefs, RadioCliReadsAndWritesTheNodeFields) {
  NodePrefs prefs;
  expectRadioCliDrivesNodeFields(prefs);
}

TEST(CompanionNodePrefs, CustomStoreReachesTheFemGains) {
  NodePrefs prefs;
  expectCustomStoreReachesFemGains(prefs);
}

#if 0
// Re-enable test once we can SET fem_ values in companion
TEST(CompanionNodePrefs, RxGainSettingsRoundTripIndependently) {
  NodePrefs saved;
  saved.rx_boosted_gain = 0;
  saved.radio_fem_rxgain = 1;
  saved.radio_fem_txgain = 0;

  CaptureStream output;
  ASSERT_TRUE(saved.saveSerial(output));
  EXPECT_NE(std::string::npos, output.text().find("rxgain:0"));
  EXPECT_NE(std::string::npos, output.text().find("fem_rxgain:1"));
  EXPECT_NE(std::string::npos, output.text().find("fem_txgain:0"));

  ReplayStream input("{radio:{rxgain:1,fem_rxgain:0,fem_txgain:1}}");
  NodePrefs loaded;
  loaded.rx_boosted_gain = 0;
  loaded.radio_fem_rxgain = 1;
  loaded.radio_fem_txgain = 0;

  ASSERT_TRUE(loaded.loadSerial(input));
  EXPECT_EQ(1, loaded.rx_boosted_gain);
  EXPECT_EQ(0, loaded.radio_fem_rxgain);
  EXPECT_EQ(1, loaded.radio_fem_txgain);
}
#endif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
