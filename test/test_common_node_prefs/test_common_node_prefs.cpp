// Characterization tests for the NodePrefs of the server roles (repeater,
// room server, sensor). They pin down what /prefs.json contains and what the
// radio CLI does today, so a refactor of the prefs classes can prove it keeps
// the on-flash format and the CLI behaviour unchanged.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include <helpers/CommonNodePrefs.h>

#include "PrefsStreams.h"
#include "RadioPrefsChecks.h"

namespace {

void fillAll(NodePrefs& p) {
  fillRadioAndGps(p);
  strcpy(p.node_name, "Test Repeater");
  strcpy(p.password, "admin-pw");
  strcpy(p.guest_password, "guest-pw");
  strcpy(p.owner_info, "owner \"quoted\" info");
  p.advert_interval = 60;
  p.flood_advert_interval = 12;
  p.node_lat = 53.551086;
  p.node_lon = 9.993682;
  p.bridge_enabled = 1;
  p.bridge_delay = 500;
  p.bridge_pkt_src = 1;
  p.bridge_baud = 115200;
  p.bridge_channel = 6;
  strcpy(p.bridge_secret, "bridge-secret");
  p.disable_fwd = 1;
  p.flood_max = 32;
  p.flood_max_unscoped = 16;
  p.flood_max_advert = 8;
  p.loop_detect = 2;
  p.allow_read_only = 1;
  p.adc_multiplier = 1.25f;
  p.powersaving_enabled = 1;
}

// /prefs.json as written by v1.18.0 for the values of fillAll().
const char* kPrefsJson =
    "{name:\"Test Repeater\",pass:\"admin-pw\",guest:\"guest-pw\","
    "owner:\"owner \\\"quoted\\\" info\",adv_int:60,f_adv_int:12,lat:53.551086,"
    "lon:9.993682,radio:{freq:869.6180,bw:62.5000,sf:8,cr:6,cad:1,"
    "int_thr:14,rxgain:1,fem_rxgain:1,fem_txgain:2,tx:21,af:1.5000,"
    "rxdelay:3.2500,f_txdelay:0.7500,d_txdelay:0.2500,agc_int:5,hash_mode:2,"
    "multi_ack:1},bridge:{en:1,delay:500,src:1,baud:115200,ch:6,secret:\"bridge-secret\"},"
    "gps:{en:1,int:900,adv_loc:1},repeat:{disable:1,f_max:32,f_max_uns:16,"
    "f_max_adv:8,loop:2},room:{rd_only:1},power:{adc_mult:1.2500,pwr_sav_en:1},"
    "custom:{}}";

}  // namespace

TEST(CommonNodePrefs, WritesTheKnownPrefsJson) {
  NodePrefs prefs;
  fillAll(prefs);

  CaptureStream out;
  ASSERT_TRUE(prefs.saveSerial(out));
  EXPECT_EQ(kPrefsJson, out.text());
}

TEST(CommonNodePrefs, ReadsEveryPersistedFieldBack) {
  NodePrefs expected;
  fillAll(expected);

  NodePrefs loaded;
  ReplayStream in(kPrefsJson);
  ASSERT_TRUE(loaded.loadSerial(in));

  expectRadioAndGpsEqual(expected, loaded);
  EXPECT_STREQ(expected.node_name, loaded.node_name);
  EXPECT_STREQ(expected.password, loaded.password);
  EXPECT_STREQ(expected.guest_password, loaded.guest_password);
  EXPECT_STREQ(expected.owner_info, loaded.owner_info);
  EXPECT_EQ(expected.advert_interval, loaded.advert_interval);
  EXPECT_EQ(expected.flood_advert_interval, loaded.flood_advert_interval);
  EXPECT_DOUBLE_EQ(expected.node_lat, loaded.node_lat);
  EXPECT_DOUBLE_EQ(expected.node_lon, loaded.node_lon);
  EXPECT_EQ(expected.bridge_enabled, loaded.bridge_enabled);
  EXPECT_EQ(expected.bridge_delay, loaded.bridge_delay);
  EXPECT_EQ(expected.bridge_pkt_src, loaded.bridge_pkt_src);
  EXPECT_EQ(expected.bridge_baud, loaded.bridge_baud);
  EXPECT_EQ(expected.bridge_channel, loaded.bridge_channel);
  EXPECT_STREQ(expected.bridge_secret, loaded.bridge_secret);
  EXPECT_EQ(expected.disable_fwd, loaded.disable_fwd);
  EXPECT_EQ(expected.flood_max, loaded.flood_max);
  EXPECT_EQ(expected.flood_max_unscoped, loaded.flood_max_unscoped);
  EXPECT_EQ(expected.flood_max_advert, loaded.flood_max_advert);
  EXPECT_EQ(expected.loop_detect, loaded.loop_detect);
  EXPECT_EQ(expected.allow_read_only, loaded.allow_read_only);
  EXPECT_FLOAT_EQ(expected.adc_multiplier, loaded.adc_multiplier);
  EXPECT_EQ(expected.powersaving_enabled, loaded.powersaving_enabled);
}

TEST(CommonNodePrefs, RadioCliReadsAndWritesTheNodeFields) {
  NodePrefs prefs;
  expectRadioCliDrivesNodeFields(prefs);
}

TEST(CommonNodePrefs, CustomStoreReachesTheFemGains) {
  NodePrefs prefs;
  expectCustomStoreReachesFemGains(prefs);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
