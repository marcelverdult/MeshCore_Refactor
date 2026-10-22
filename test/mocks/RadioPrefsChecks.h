#pragma once

// Shared expectations for the radio and GPS settings of a NodePrefs class.
// The server roles (repeater, room server, sensor) and the companion radio each
// have their own NodePrefs; both must expose the radio/GPS settings the same
// way, so the same checks run against both.

#include <gtest/gtest.h>
#include <cmath>
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

// NaN test that does not rely on float comparisons, which -ffast-math may assume
// never see a NaN (nRF52 builds use -Ofast).
inline bool isNanBits(float v) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  return (bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0;
}

// Edge cases of the radio CLI: boundaries, out of range values, values that do not
// fit the field, garbage, "nan", prefix look-alikes. The expected replies and field
// values are what the code returned before the CLI became table driven; some of them
// are odd (noted inline) and are kept on purpose, because changing them would change
// what users see.
template <class P>
void expectRadioCliEdgeCases(P& p) {
  radio_driver.reset();
  fillRadioAndGps(p);

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
  auto unhandled = [&p](const char* command) {
    char reply[160];
    reply[0] = 0;
    EXPECT_FALSE(p.getRadioPrefs()->handleCommand(command, 0, reply)) << command;
  };

  // af: strtof with an end check, range 0..9
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af abc"));
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af -1"));
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af 9.01"));
  EXPECT_FLOAT_EQ(1.5f, p.airtime_factor);
  EXPECT_EQ("OK", accepted("set af 9"));
  EXPECT_FLOAT_EQ(9.0f, p.airtime_factor);
  EXPECT_EQ("OK", accepted("set af  0"));
  EXPECT_FLOAT_EQ(0.0f, p.airtime_factor);
  // "set af nan" used to be accepted, and the result depended on the compiler:
  // no ordered comparison is true for a NaN, and -ffast-math (nRF52 builds use
  // -Ofast) may additionally assume that NaN never occurs. inRange() reads the
  // text, so the answer is now the same on every build and needs no guard here.
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af nan"));
  EXPECT_FALSE(isNanBits(p.airtime_factor));
  EXPECT_FLOAT_EQ(0.0f, p.airtime_factor);

  // dutycycle: derived from af, range 1..100
  EXPECT_EQ("ERROR: dutycycle must be 1-100", rejected("set dutycycle 0.5"));
  EXPECT_EQ("OK - 100.0%", accepted("set dutycycle 100"));
  EXPECT_FLOAT_EQ(0.0f, p.airtime_factor);
  EXPECT_EQ("> 100.0%", radioCli(p, "get dutycycle"));
  EXPECT_EQ("OK - 1.0%", accepted("set dutycycle 1"));
  EXPECT_FLOAT_EQ(99.0f, p.airtime_factor);            // odd: beyond the 0..9 of "set af"
  EXPECT_EQ("OK - 33.0%", accepted("set dutycycle 33"));

  // Neither command takes a value that is not a finite number. inRange()
  // decides that on the text, so the answer does not depend on -ffast-math and
  // the cases need no guard. "set dutycycle" writes airtime_factor through a
  // division and had the same hole as "set af".
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af -nan"));
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af inf"));
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af 1e40"));
  EXPECT_EQ("ERROR: dutycycle must be 1-100", rejected("set dutycycle nan"));
  EXPECT_EQ("ERROR: dutycycle must be 1-100", rejected("set dutycycle inf"));
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af +nan"));
  EXPECT_EQ("ERROR: af must be 0-9", rejected("set af infinity"));
  EXPECT_EQ("ERROR: dutycycle must be 1-100", rejected("set dutycycle  nan"));
  EXPECT_FALSE(isNanBits(p.airtime_factor));

  // int.thresh: atoi, no range, stored as uint8_t
  EXPECT_EQ("OK", accepted("set int.thresh 300"));
  EXPECT_EQ(44, p.interference_threshold);
  EXPECT_EQ("OK", accepted("set int.thresh -1"));
  EXPECT_EQ(255, p.interference_threshold);
  EXPECT_EQ("OK", accepted("set int.thresh abc"));
  EXPECT_EQ(0, p.interference_threshold);

  // cad: on only if the value starts with "on"
  EXPECT_EQ("OK", accepted("set cad onx"));
  EXPECT_EQ(1, p.cad_enabled);
  EXPECT_EQ("OK", accepted("set cad ON"));
  EXPECT_EQ(0, p.cad_enabled);
  EXPECT_EQ("OK", accepted("set cad 1"));
  EXPECT_EQ(0, p.cad_enabled);

  // radio.rxgain: same rule, pushed to the radio
  EXPECT_EQ("OK", accepted("set radio.rxgain onfoo"));
  EXPECT_EQ(1, p.rx_boosted_gain);
  EXPECT_TRUE(radio_driver.rx_boosted);

  // tx: "get tx" also matches with trailing text; values wrap into int8_t
  EXPECT_EQ("> 21", radioCli(p, "get tx foo"));
  EXPECT_EQ("OK", accepted("set tx -5"));
  EXPECT_EQ(-5, p.tx_power_dbm);
  EXPECT_EQ(-5, radio_driver.tx_power);
  EXPECT_EQ("> 251", radioCli(p, "get tx"));           // odd: printed as unsigned
  EXPECT_EQ("OK", accepted("set tx 300"));
  EXPECT_EQ(44, p.tx_power_dbm);

  // rxdelay: atof, range 0..20, NaN rejected, garbage reads as 0
  EXPECT_EQ("OK", accepted("set rxdelay 20"));
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay 20.01"));
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay -0.01"));
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay nan"));
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay inf"));
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay +nan"));
  EXPECT_EQ("Error, must be 0-20", rejected("set rxdelay INF"));
  EXPECT_FLOAT_EQ(20.0f, p.rx_delay_base);
  EXPECT_EQ("OK", accepted("set rxdelay abc"));
  EXPECT_FLOAT_EQ(0.0f, p.rx_delay_base);

  // agc.reset.interval: seconds, clamped to 0..1020, stored in 4 s units
  EXPECT_EQ("OK - interval rounded to 1020", accepted("set agc.reset.interval 1023"));
  EXPECT_EQ(255, p.agc_reset_interval);
  EXPECT_EQ("OK - interval rounded to 0", accepted("set agc.reset.interval -5"));
  EXPECT_EQ("OK - interval rounded to 0", accepted("set agc.reset.interval 3"));
  EXPECT_EQ("OK - interval rounded to 0", accepted("set agc.reset.interval abc"));
  EXPECT_EQ("> 0", radioCli(p, "get agc.reset.interval"));

  // path.hash.mode: atoi into uint8_t, must be < 3
  EXPECT_EQ("OK", accepted("set path.hash.mode 2"));
  EXPECT_EQ("Error, must be 0,1, or 2", rejected("set path.hash.mode -1"));
  EXPECT_EQ(2, p.path_hash_mode);
  EXPECT_EQ("OK", accepted("set path.hash.mode abc"));
  EXPECT_EQ(0, p.path_hash_mode);

  // multi.acks: atoi, no range, stored as uint8_t
  EXPECT_EQ("OK", accepted("set multi.acks 300"));
  EXPECT_EQ(44, p.multi_acks);

  // txdelay / direct.txdelay: atof, range 0..2, NaN rejected
  EXPECT_EQ("OK", accepted("set txdelay 2"));
  EXPECT_EQ("Error, must be 0-2", rejected("set txdelay 2.01"));
  EXPECT_EQ("Error, must be 0-2", rejected("set txdelay nan"));
  EXPECT_EQ("Error, must be 0-2", rejected("set txdelay inf"));
  EXPECT_EQ("Error, must be 0-2", rejected("set direct.txdelay nan"));
  EXPECT_EQ("OK", accepted("set direct.txdelay 0"));
  EXPECT_EQ("Error, must be 0-2", rejected("set direct.txdelay -1"));
  EXPECT_FLOAT_EQ(2.0f, p.tx_delay_factor);
  EXPECT_FLOAT_EQ(0.0f, p.direct_tx_delay_factor);

  // radio: all four parts must be in range
  EXPECT_EQ("Error, invalid radio params", rejected("set radio 869"));
  EXPECT_EQ("Error, invalid radio params", rejected("set radio 149.9,125,9,5"));
  EXPECT_EQ("Error, invalid radio params", rejected("set radio 433,6.9,9,5"));
  EXPECT_EQ("Error, invalid radio params", rejected("set radio 433,125,13,5"));
  EXPECT_EQ("Error, invalid radio params", rejected("set radio 433,125,9,9"));
  EXPECT_EQ("OK - reboot to apply", accepted("set radio 150,7,5,5"));
  EXPECT_EQ("> 150.0,7,5,5", radioCli(p, "get radio"));
  EXPECT_EQ("OK - reboot to apply", accepted("set radio 2500,500,12,8"));
  EXPECT_EQ("> 2500.0,500,12,8", radioCli(p, "get radio"));

  // look-alikes and commands other handlers own are not taken
  unhandled("get radiox");
  unhandled("get af2");
  unhandled("set afx 1");
  unhandled("set txdelayx 1");
  unhandled("get txdelay2");
  unhandled("set freq 869");
  unhandled("get name");

  // the key/value store only exposes the FEM gains
  char value[16];
  EXPECT_FALSE(p.getRadioPrefs()->getByKey("af", value, sizeof(value)));
  EXPECT_FALSE(p.getRadioPrefs()->setByKey("tx", "5"));
}
