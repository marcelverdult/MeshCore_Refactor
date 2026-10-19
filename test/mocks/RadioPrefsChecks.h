#pragma once

// Shared expectations for the radio and GPS settings of a NodePrefs class.
// The server roles (repeater, room server, sensor) and the companion radio each
// have their own NodePrefs; both must expose the radio/GPS settings the same
// way, so the same checks run against both.

#include <gtest/gtest.h>
#include <cstring>
#include <string>

#include "target.h"

// Distinct, non-default values for every radio and GPS field.
template <class P>
void fillRadioAndGps(P& p) {
  p.freq = 869.618f;
  p.bw = 62.5f;
  p.sf = 8;
  p.cr = 6;
  p.cad_enabled = 1;
  p.interference_threshold = 14;
  p.rx_boosted_gain = 1;
  p.radio_fem_rxgain = 1;
  p.radio_fem_txgain = 2;
  p.tx_power_dbm = 21;
  p.airtime_factor = 1.5f;
  p.rx_delay_base = 3.25f;
  p.tx_delay_factor = 0.75f;
  p.direct_tx_delay_factor = 0.25f;
  p.agc_reset_interval = 5;
  p.path_hash_mode = 2;
  p.multi_acks = 1;

  p.gps_enabled = 1;
  p.gps_interval = 900;
  p.advert_loc_policy = 1;
}

template <class P>
void expectRadioAndGpsEqual(const P& expected, const P& actual) {
  EXPECT_FLOAT_EQ(expected.freq, actual.freq);
  EXPECT_FLOAT_EQ(expected.bw, actual.bw);
  EXPECT_EQ(expected.sf, actual.sf);
  EXPECT_EQ(expected.cr, actual.cr);
  EXPECT_EQ(expected.cad_enabled, actual.cad_enabled);
  EXPECT_EQ(expected.interference_threshold, actual.interference_threshold);
  EXPECT_EQ(expected.rx_boosted_gain, actual.rx_boosted_gain);
  EXPECT_EQ(expected.radio_fem_rxgain, actual.radio_fem_rxgain);
  EXPECT_EQ(expected.radio_fem_txgain, actual.radio_fem_txgain);
  EXPECT_EQ(expected.tx_power_dbm, actual.tx_power_dbm);
  EXPECT_FLOAT_EQ(expected.airtime_factor, actual.airtime_factor);
  EXPECT_FLOAT_EQ(expected.rx_delay_base, actual.rx_delay_base);
  EXPECT_FLOAT_EQ(expected.tx_delay_factor, actual.tx_delay_factor);
  EXPECT_FLOAT_EQ(expected.direct_tx_delay_factor, actual.direct_tx_delay_factor);
  EXPECT_EQ(expected.agc_reset_interval, actual.agc_reset_interval);
  EXPECT_EQ(expected.path_hash_mode, actual.path_hash_mode);
  EXPECT_EQ(expected.multi_acks, actual.multi_acks);

  EXPECT_EQ(expected.gps_enabled, actual.gps_enabled);
  EXPECT_EQ(expected.gps_interval, actual.gps_interval);
  EXPECT_EQ(expected.advert_loc_policy, actual.advert_loc_policy);
}

// Runs one CLI command through the prefs' radio handler and returns the reply.
template <class P>
std::string radioCli(P& p, const char* command) {
  char reply[160];
  reply[0] = 0;
  EXPECT_TRUE(p.getRadioPrefs()->handleCommand(command, 0, reply)) << command;
  return reply;
}

// The radio CLI must read and write the node's own fields, mark the prefs
// dirty, and push TX power / RX gain to the radio driver.
template <class P>
void expectRadioCliDrivesNodeFields(P& p) {
  radio_driver.reset();
  fillRadioAndGps(p);
  p.clearDirty();

  EXPECT_EQ("> 869.6179809,62.5,8,6", radioCli(p, "get radio"));
  EXPECT_EQ("> 869.6179809", radioCli(p, "get freq"));
  EXPECT_EQ("> 1.5", radioCli(p, "get af"));
  EXPECT_EQ("> 40.0%", radioCli(p, "get dutycycle"));
  EXPECT_EQ("> 14", radioCli(p, "get int.thresh"));
  EXPECT_EQ("> on", radioCli(p, "get cad"));
  EXPECT_EQ("> on", radioCli(p, "get radio.rxgain"));
  EXPECT_EQ("> 21", radioCli(p, "get tx"));
  EXPECT_EQ("> 3.25", radioCli(p, "get rxdelay"));
  EXPECT_EQ("> 20", radioCli(p, "get agc.reset.interval"));
  EXPECT_EQ("> 2", radioCli(p, "get path.hash.mode"));
  EXPECT_EQ("> 1", radioCli(p, "get multi.acks"));
  EXPECT_EQ("> 0.75", radioCli(p, "get txdelay"));
  EXPECT_EQ("> 0.25", radioCli(p, "get direct.txdelay"));
  EXPECT_FALSE(p.isDirty());

  // A command that changes a setting must mark the prefs dirty (otherwise the
  // change is never saved); a rejected command must leave them clean.
  auto accepted = [&p](const char* command) {
    p.clearDirty();
    std::string reply = radioCli(p, command);
    EXPECT_TRUE(p.isDirty()) << command;
    return reply;
  };
  auto rejected = [&p](const char* command) {
    p.clearDirty();
    std::string reply = radioCli(p, command);
    EXPECT_FALSE(p.isDirty()) << command;
    return reply;
  };

  EXPECT_EQ("OK - reboot to apply", accepted("set radio 433.5,125,9,5"));
  EXPECT_FLOAT_EQ(433.5f, p.freq);
  EXPECT_FLOAT_EQ(125.0f, p.bw);
  EXPECT_EQ(9, p.sf);
  EXPECT_EQ(5, p.cr);

  EXPECT_EQ("Error, invalid radio params", rejected("set radio 433.5,6,9,5"));
  EXPECT_FLOAT_EQ(125.0f, p.bw);

  EXPECT_EQ("OK", accepted("set af 2"));
  EXPECT_FLOAT_EQ(2.0f, p.airtime_factor);
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af 10"));
  EXPECT_FLOAT_EQ(2.0f, p.airtime_factor);
  EXPECT_EQ("OK - 25.0%", accepted("set dutycycle 25"));
  EXPECT_FLOAT_EQ(3.0f, p.airtime_factor);
  EXPECT_EQ("ERROR: dutycycle must be 1-100", rejected("set dutycycle 0"));

  EXPECT_EQ("OK", accepted("set int.thresh 9"));
  EXPECT_EQ(9, p.interference_threshold);
  EXPECT_EQ("OK", accepted("set cad off"));
  EXPECT_EQ(0, p.cad_enabled);

  EXPECT_EQ("OK", accepted("set radio.rxgain off"));
  EXPECT_EQ(0, p.rx_boosted_gain);
  EXPECT_EQ(1, radio_driver.rx_boost_calls);
  EXPECT_FALSE(radio_driver.rx_boosted);
  // The setting is stored even when the radio cannot apply it.
  radio_driver.rx_boost_supported = false;
  EXPECT_EQ("Error: unsupported", accepted("set radio.rxgain on"));
  EXPECT_EQ(1, p.rx_boosted_gain);

  EXPECT_EQ("OK", accepted("set tx 17"));
  EXPECT_EQ(17, p.tx_power_dbm);
  EXPECT_EQ(1, radio_driver.tx_power_calls);
  EXPECT_EQ(17, radio_driver.tx_power);

  EXPECT_EQ("OK", accepted("set rxdelay 4.5"));
  EXPECT_FLOAT_EQ(4.5f, p.rx_delay_base);
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay 21"));

  EXPECT_EQ("OK - interval rounded to 36", accepted("set agc.reset.interval 38"));
  EXPECT_EQ(9, p.agc_reset_interval);

  EXPECT_EQ("OK", accepted("set path.hash.mode 1"));
  EXPECT_EQ(1, p.path_hash_mode);
  EXPECT_EQ("Error, must be 0,1, or 2", rejected("set path.hash.mode 3"));

  EXPECT_EQ("OK", accepted("set multi.acks 0"));
  EXPECT_EQ(0, p.multi_acks);

  EXPECT_EQ("OK", accepted("set txdelay 1.5"));
  EXPECT_FLOAT_EQ(1.5f, p.tx_delay_factor);
  EXPECT_EQ("Error, must be 0-2", rejected("set txdelay 3"));
  EXPECT_EQ("OK", accepted("set direct.txdelay 0.5"));
  EXPECT_FLOAT_EQ(0.5f, p.direct_tx_delay_factor);
  EXPECT_EQ("Error, must be 0-2", rejected("set direct.txdelay 3"));

  char reply[16];
  EXPECT_FALSE(p.getRadioPrefs()->handleCommand("get name", 0, reply));
}

// Board code reaches the FEM gains only through the "custom" key/value store,
// which falls back to the radio settings.
template <class P>
void expectCustomStoreReachesFemGains(P& p) {
  fillRadioAndGps(p);
  p.clearDirty();

  char value[8];
  ASSERT_TRUE(p.getCustom()->getByKey("fem_rxgain", value, sizeof(value)));
  EXPECT_STREQ("1", value);
  ASSERT_TRUE(p.getCustom()->getByKey("fem_txgain", value, sizeof(value)));
  EXPECT_STREQ("2", value);

  ASSERT_TRUE(p.getCustom()->setByKey("fem_rxgain", "0"));
  EXPECT_EQ(0, p.radio_fem_rxgain);
  EXPECT_TRUE(p.isDirty());
}
