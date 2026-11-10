// CommonCLI::handleSetCmd(): every "set <name> <value>" of the chain. The radio
// settings ("set radio", "set tx", "set rxdelay" and the rest) are answered earlier,
// by CommonRadioPrefs, and are pinned by test/mocks/RadioPrefsChecks.h; only their
// place in the order is checked here.

#include <gtest/gtest.h>

#include "CliHarness.h"

static const Calls SAVED = {"savePrefs advert_interval=90"};

// ---- allow.read.only ----------------------------------------------------------

TEST_F(CliTest, SetAllowReadOnlyTakesOnAndTreatsEverythingElseAsOff) {
  EXPECT_EQ(run("set allow.read.only on"), "OK");
  EXPECT_EQ(prefs.allow_read_only, 1);
  EXPECT_EQ(takeCalls(), SAVED);
  EXPECT_EQ(run("set allow.read.only off"), "OK");
  EXPECT_EQ(prefs.allow_read_only, 0);
  EXPECT_EQ(run("set allow.read.only onward"), "OK");        // "on" prefix, pinned as is
  EXPECT_EQ(prefs.allow_read_only, 1);
  EXPECT_EQ(run("set allow.read.only yes"), "OK");           // not "on": off, pinned as is
  EXPECT_EQ(prefs.allow_read_only, 0);
  EXPECT_EQ(run("set allow.read.only "), "OK");              // nothing: off
  EXPECT_EQ(prefs.allow_read_only, 0);
}

TEST_F(CliTest, SetAllowReadOnlyNeedsItsTrailingSpace) {
  EXPECT_EQ(run("set allow.read.only"), "unknown config: allow.read.only");
  EXPECT_EQ(run("set allow.read.onlyx on"), "unknown config: allow.read.onlyx on");
}

// ---- flood.advert.interval ----------------------------------------------------------

TEST_F(CliTest, SetFloodAdvertIntervalTakesHoursFromThreeTo168OrZeroForOff) {
  EXPECT_EQ(run("set flood.advert.interval 24"), "OK");
  EXPECT_EQ(prefs.flood_advert_interval, 24);
  // the timer is told after the value changed, and the save comes last
  EXPECT_EQ(takeCalls(), (Calls{"updateFloodAdvertTimer flood_advert_interval=24",
                                "savePrefs advert_interval=90"}));
  for (const char* v : {"3", "168", "0"}) {
    EXPECT_EQ(run(std::string("set flood.advert.interval ") + v), "OK") << v;
  }
  EXPECT_EQ(prefs.flood_advert_interval, 0);
  takeCalls();
}

TEST_F(CliTest, SetFloodAdvertIntervalRefusesOneTwoAndMoreThan168) {
  for (const char* v : {"1", "2", "169", "1000"}) {
    EXPECT_EQ(run(std::string("set flood.advert.interval ") + v), "Error: interval range is 3-168 hours") << v;
  }
  EXPECT_EQ(prefs.flood_advert_interval, 12);       // unchanged
  EXPECT_TRUE(takeCalls().empty());                 // no timer update, no save
}

TEST_F(CliTest, SetFloodAdvertIntervalReadsGarbageAsZeroPinnedAsIs) {
  EXPECT_EQ(run("set flood.advert.interval abc"), "OK");
  EXPECT_EQ(prefs.flood_advert_interval, 0);
  prefs.flood_advert_interval = 12;
  EXPECT_EQ(run("set flood.advert.interval -5"), "OK");   // no sign: reads as 0
  EXPECT_EQ(prefs.flood_advert_interval, 0);
  prefs.flood_advert_interval = 12;
  EXPECT_EQ(run("set flood.advert.interval 24h"), "OK");   // stops at the first non digit
  EXPECT_EQ(prefs.flood_advert_interval, 24);
}

// ---- advert.interval --------------------------------------------------------------

TEST_F(CliTest, SetAdvertIntervalTakesMinutesFrom60To240OrZeroAndStoresHalf) {
  EXPECT_EQ(run("set advert.interval 120"), "OK");
  EXPECT_EQ(prefs.advert_interval, 60);             // stored in units of 2 minutes
  EXPECT_EQ(takeCalls(), (Calls{"updateAdvertTimer advert_interval=60",
                                "savePrefs advert_interval=60"}));
  EXPECT_EQ(run("set advert.interval 61"), "OK");
  EXPECT_EQ(prefs.advert_interval, 30);             // odd minutes round down: reads back as 60
  EXPECT_EQ(run("set advert.interval 240"), "OK");
  EXPECT_EQ(prefs.advert_interval, 120);
  EXPECT_EQ(run("set advert.interval 0"), "OK");
  EXPECT_EQ(prefs.advert_interval, 0);
  takeCalls();
}

TEST_F(CliTest, SetAdvertIntervalRefusesBelow60AndAbove240) {
  for (const char* v : {"1", "59", "241", "9999"}) {
    EXPECT_EQ(run(std::string("set advert.interval ") + v), "Error: interval range is 60-240 minutes") << v;
  }
  EXPECT_EQ(prefs.advert_interval, 90);
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, EverySaveSwitchesTheLocalAdvertOffWhenItIsBelowAnHourPinnedAsIs) {
  // CommonCLI::savePrefs() runs after every command that saves: an advert interval
  // under an hour (stored value under 30) is set to 0 there, whatever the command was.
  prefs.advert_interval = 20;                       // 40 minutes, as an old config could hold
  EXPECT_EQ(run("set name Beta"), "OK");
  EXPECT_EQ(prefs.advert_interval, 0);
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=0"}));   // the role sees the 0
  prefs.advert_interval = 30;                       // exactly an hour: kept
  EXPECT_EQ(run("set name Gamma"), "OK");
  EXPECT_EQ(prefs.advert_interval, 30);
}

// ---- guest.password, name, repeat, lat, lon, owner.info ----------------------------

TEST_F(CliTest, SetGuestPasswordIsCutToFifteenCharactersAndNotEchoed) {
  EXPECT_EQ(run("set guest.password open sesame"), "OK");
  EXPECT_STREQ(prefs.guest_password, "open sesame");
  EXPECT_EQ(run("set guest.password 0123456789abcdefXYZ"), "OK");
  EXPECT_STREQ(prefs.guest_password, "0123456789abcde");
  EXPECT_EQ(run("set guest.password "), "OK");
  EXPECT_STREQ(prefs.guest_password, "");
  EXPECT_EQ(takeCalls().size(), 3u);
  EXPECT_STREQ(prefs.password, "adminpw");          // the admin password is not touched
}

TEST_F(CliTest, SetNameStoresValidNamesAndCutsThemAt31Characters) {
  EXPECT_EQ(run("set name Repeater 7 (north)"), "OK");
  EXPECT_STREQ(prefs.node_name, "Repeater 7 (north)");
  EXPECT_EQ(run("set name " + std::string(40, 'x')), "OK");
  EXPECT_EQ(std::string(prefs.node_name), std::string(31, 'x'));
  EXPECT_EQ(run("set name "), "OK");                 // an empty name is accepted, pinned as is
  EXPECT_STREQ(prefs.node_name, "");
  EXPECT_EQ(takeCalls().size(), 3u);
}

TEST_F(CliTest, SetNameRefusesEachReservedCharacter) {
  for (char c : std::string("[]\\:,?*")) {
    EXPECT_EQ(run(std::string("set name Bad") + c + "name"), "Error, bad chars") << c;
  }
  EXPECT_STREQ(prefs.node_name, "Alpha");
  EXPECT_TRUE(takeCalls().empty());
  // the whole text is checked, not only the part that fits: a reserved character
  // past the 31 that would be kept still refuses the name
  EXPECT_EQ(run("set name " + std::string(35, 'x') + "?"), "Error, bad chars");
  EXPECT_STREQ(prefs.node_name, "Alpha");
}

TEST_F(CliTest, SetRepeatIsOnForEverythingExceptOff) {
  EXPECT_EQ(run("set repeat off"), "OK - repeat is now OFF");
  EXPECT_EQ(prefs.disable_fwd, 1);
  EXPECT_EQ(run("set repeat on"), "OK - repeat is now ON");
  EXPECT_EQ(prefs.disable_fwd, 0);
  EXPECT_EQ(run("set repeat offline"), "OK - repeat is now OFF");   // "off" prefix, pinned as is
  EXPECT_EQ(run("set repeat maybe"), "OK - repeat is now ON");      // anything else is on
  EXPECT_EQ(run("set repeat "), "OK - repeat is now ON");
  EXPECT_EQ(takeCalls().size(), 5u);
}

TEST_F(CliTest, SetLatAndLonTakeAnyNumberWithoutARangeCheckPinnedAsIs) {
  EXPECT_EQ(run("set lat 48.1371"), "OK");
  EXPECT_EQ(run("set lon -122.5"), "OK");
  EXPECT_DOUBLE_EQ(prefs.node_lat, 48.1371);
  EXPECT_DOUBLE_EQ(prefs.node_lon, -122.5);
  EXPECT_EQ(run("set lat 999"), "OK");                  // not a latitude, accepted
  EXPECT_DOUBLE_EQ(prefs.node_lat, 999.0);
  EXPECT_EQ(run("set lon abc"), "OK");                  // not a number: 0
  EXPECT_DOUBLE_EQ(prefs.node_lon, 0.0);
  EXPECT_EQ(takeCalls().size(), 4u);
}

TEST_F(CliTest, SetOwnerInfoTurnsBarsIntoNewlinesAndCutsAt119Characters) {
  EXPECT_EQ(run("set owner.info Jo|Berlin|jo@example.org"), "OK");
  EXPECT_STREQ(prefs.owner_info, "Jo\nBerlin\njo@example.org");
  EXPECT_EQ(takeCalls(), SAVED);
  EXPECT_EQ(run("set owner.info " + std::string(130, 'o')), "OK");
  EXPECT_EQ(std::string(prefs.owner_info), std::string(119, 'o'));
  EXPECT_EQ(run("set owner.info "), "OK");
  EXPECT_STREQ(prefs.owner_info, "");
}

// ---- flood.max, flood.max.unscoped, flood.max.advert -----------------------------------

TEST_F(CliTest, SetFloodMaxFieldsEachTakeUpTo64AndKeepTheOthers) {
  EXPECT_EQ(run("set flood.max 5"), "OK");
  EXPECT_EQ(run("set flood.max.unscoped 6"), "OK");
  EXPECT_EQ(run("set flood.max.advert 7"), "OK");
  EXPECT_EQ(prefs.flood_max, 5);
  EXPECT_EQ(prefs.flood_max_unscoped, 6);
  EXPECT_EQ(prefs.flood_max_advert, 7);
  EXPECT_EQ(run("set flood.max 64"), "OK");
  EXPECT_EQ(run("set flood.max 0"), "OK");
  EXPECT_EQ(takeCalls().size(), 5u);
}

TEST_F(CliTest, SetFloodMaxFieldsRefuseMoreThan64) {
  for (const char* name : {"flood.max", "flood.max.unscoped", "flood.max.advert"}) {
    EXPECT_EQ(run(std::string("set ") + name + " 65"), "Error, max 64") << name;
  }
  EXPECT_EQ(prefs.flood_max, 20);
  EXPECT_EQ(prefs.flood_max_unscoped, 21);
  EXPECT_EQ(prefs.flood_max_advert, 22);
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetFloodMaxWrapsAt256BeforeTheRangeCheckPinnedAsIs) {
  // The value is read into a uint8_t first, so 300 is 44 when it is compared with 64
  // (and 320 is 64, and 256 is 0), and -1 is 255.
  EXPECT_EQ(run("set flood.max 300"), "OK");
  EXPECT_EQ(prefs.flood_max, 44);
  EXPECT_EQ(run("set flood.max 256"), "OK");
  EXPECT_EQ(prefs.flood_max, 0);
  EXPECT_EQ(run("set flood.max -1"), "Error, max 64");
  EXPECT_EQ(run("set flood.max abc"), "OK");            // reads as 0
  EXPECT_EQ(prefs.flood_max, 0);
}

TEST_F(CliTest, SetFloodMaxLongerNamesAreNotCaughtByTheShorterOne) {
  EXPECT_EQ(run("set flood.max.unscoped 9"), "OK");
  EXPECT_EQ(prefs.flood_max, 20);
  EXPECT_EQ(prefs.flood_max_unscoped, 9);
  EXPECT_EQ(run("set flood.max.other 9"), "unknown config: flood.max.other 9");
  EXPECT_EQ(run("set flood.max"), "unknown config: flood.max");
}

// ---- loop.detect -------------------------------------------------------------------

TEST_F(CliTest, SetLoopDetectTakesTheFourModes) {
  EXPECT_EQ(run("set loop.detect off"), "OK");
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_OFF);
  EXPECT_EQ(run("set loop.detect minimal"), "OK");
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_MINIMAL);
  EXPECT_EQ(run("set loop.detect moderate"), "OK");
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_MODERATE);
  EXPECT_EQ(run("set loop.detect strict"), "OK");
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_STRICT);
  EXPECT_EQ(takeCalls().size(), 4u);
}

TEST_F(CliTest, SetLoopDetectRefusesAnythingElseWithoutSaving) {
  for (const char* v : {"", "on", "OFF", "high", "1"}) {
    EXPECT_EQ(run(std::string("set loop.detect ") + v), "Error, must be: off, minimal, moderate, or strict") << v;
  }
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_MODERATE);
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetLoopDetectMatchesOnThePrefixPinnedAsIs) {
  EXPECT_EQ(run("set loop.detect offline"), "OK");
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_OFF);
  EXPECT_EQ(run("set loop.detect strictly"), "OK");
  EXPECT_EQ(prefs.loop_detect, LOOP_DETECT_STRICT);
}

// ---- freq (serial only) ---------------------------------------------------------------

TEST_F(CliTest, SetFreqIsForTheSerialConsoleAndNeedsAReboot) {
  EXPECT_EQ(run("set freq 868.125"), "OK - reboot to apply");
  EXPECT_FLOAT_EQ(prefs.freq, 868.125f);
  EXPECT_EQ(takeCalls(), SAVED);
  EXPECT_EQ(run("set freq 5"), "OK - reboot to apply");   // no range check here, pinned as is
  EXPECT_FLOAT_EQ(prefs.freq, 5.0f);
}

TEST_F(CliTest, SetFreqOverTheAirIsAnUnknownConfig) {
  EXPECT_EQ(run("set freq 868.125", 1760000001), "unknown config: freq 868.125");
  EXPECT_FLOAT_EQ(prefs.freq, 869.618f);
  EXPECT_TRUE(takeCalls().empty());
}

// ---- prv.key ---------------------------------------------------------------------------

static std::string keyHex(uint8_t first) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < PRV_KEY_SIZE; i++) {
    uint8_t b = (uint8_t)(first + i);
    s += digits[b >> 4];
    s += digits[b & 15];
  }
  return s;
}

TEST_F(CliTest, SetPrvKeyStoresAValidKeyAndRepliesWithTheNewPublicKey) {
  std::string hex = keyHex(0x10);
  std::string reply = run("set prv.key " + hex);
  // the stub derives the public key as the first 32 bytes XOR 0x5A; the CLI prints hex
  // in upper case (pinned as is: the key it was given may be in either case)
  std::string pub;
  static const char* digits = "0123456789ABCDEF";
  for (int i = 0; i < PUB_KEY_SIZE; i++) {
    uint8_t b = (uint8_t)(0x10 + i) ^ 0x5A;
    pub += digits[b >> 4];
    pub += digits[b & 15];
  }
  EXPECT_EQ(reply, "OK, reboot to apply! New pubkey: " + pub);
  EXPECT_EQ(takeCalls(), (Calls{"saveIdentity " + pub}));    // the prefs are not saved
  EXPECT_EQ(identityControl().validate_calls, 1);
  EXPECT_EQ(identityControl().last_validated[0], 0x10);
  EXPECT_EQ(identityControl().last_validated[63], 0x10 + 63);
}

TEST_F(CliTest, SetPrvKeyAlsoWorksOverTheAirPinnedAsIs) {
  // unlike "get prv.key", which is for the serial console only
  EXPECT_EQ(run("set prv.key " + keyHex(1), 1760000001).substr(0, 33), "OK, reboot to apply! New pubkey: ");
  EXPECT_EQ(takeCalls().size(), 1u);
}

TEST_F(CliTest, SetPrvKeyRefusesAKeyThatIsNotExactly128HexCharacters) {
  EXPECT_EQ(run("set prv.key " + keyHex(0x10).substr(0, 126)), "Error, bad key");
  EXPECT_EQ(run("set prv.key " + keyHex(0x10) + "00"), "Error, bad key");
  EXPECT_EQ(run("set prv.key "), "Error, bad key");
  EXPECT_EQ(identityControl().validate_calls, 0);    // never asked: the length failed first
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetPrvKeyRefusesAKeyTheIdentityRejects) {
  identityControl().validate_ok = false;
  EXPECT_EQ(run("set prv.key " + keyHex(0x10)), "Error, bad key");
  EXPECT_EQ(identityControl().validate_calls, 1);
  EXPECT_TRUE(takeCalls().empty());
}

// ---- adc.multiplier ----------------------------------------------------------------------

TEST_F(CliTest, SetAdcMultiplierAppliesItToTheBoardAndSaves) {
  EXPECT_EQ(run("set adc.multiplier 2.5"), "OK - multiplier set to 2.500");
  EXPECT_FLOAT_EQ(prefs.adc_multiplier, 2.5f);
  EXPECT_EQ(brd.adc_set, (std::vector<float>{2.5f}));
  EXPECT_EQ(takeCalls(), SAVED);
}

TEST_F(CliTest, SetAdcMultiplierZeroMeansTheBoardDefault) {
  EXPECT_EQ(run("set adc.multiplier 0"), "OK - using default board multiplier");
  EXPECT_FLOAT_EQ(prefs.adc_multiplier, 0.0f);
  EXPECT_EQ(run("set adc.multiplier abc"), "OK - using default board multiplier");   // reads as 0
  EXPECT_EQ(brd.adc_set, (std::vector<float>{0.0f, 0.0f}));
}

TEST_F(CliTest, SetAdcMultiplierOnABoardWithoutOneResetsThePrefAndDoesNotSave) {
  brd.adc_supported = false;
  prefs.adc_multiplier = 1.5f;
  EXPECT_EQ(run("set adc.multiplier 2.5"), "Error: unsupported");
  EXPECT_FLOAT_EQ(prefs.adc_multiplier, 0.0f);
  EXPECT_EQ(brd.adc_set, (std::vector<float>{2.5f}));
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetAdcMultiplierHasNoRangeCheckHerePinnedAsIs) {
  // the loader limits the stored value to 0..10 (sanitisePrefs), this command does not
  EXPECT_EQ(run("set adc.multiplier 50"), "OK - multiplier set to 50.000");
  EXPECT_FLOAT_EQ(prefs.adc_multiplier, 50.0f);
}

// ---- build dependent: bridge -----------------------------------------------------------------

#if defined(WITH_BRIDGE)

TEST_F(CliTest, SetBridgeEnabledTellsTheRoleBeforeSaving) {
  EXPECT_EQ(run("set bridge.enabled on"), "OK");
  EXPECT_EQ(prefs.bridge_enabled, 1);
  EXPECT_EQ(run("set bridge.enabled off"), "OK");
  EXPECT_EQ(prefs.bridge_enabled, 0);
  EXPECT_EQ(run("set bridge.enabled yes"), "OK");        // only "on" counts, pinned as is
  EXPECT_EQ(prefs.bridge_enabled, 0);
  EXPECT_EQ(takeCalls(), (Calls{"setBridgeState true", "savePrefs advert_interval=90",
                                "setBridgeState false", "savePrefs advert_interval=90",
                                "setBridgeState false", "savePrefs advert_interval=90"}));
}

TEST_F(CliTest, SetBridgeDelayTakes0To10000Milliseconds) {
  EXPECT_EQ(run("set bridge.delay 500"), "OK");
  EXPECT_EQ(prefs.bridge_delay, 500);
  EXPECT_EQ(run("set bridge.delay 10000"), "OK");
  EXPECT_EQ(run("set bridge.delay 0"), "OK");
  EXPECT_EQ(takeCalls().size(), 3u);
  EXPECT_EQ(run("set bridge.delay 10001"), "Error: delay must be between 0-10000 ms");
  EXPECT_EQ(prefs.bridge_delay, 0);                      // unchanged by the refused one
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetBridgeSourceIsRxForRxAndTxForEverythingElse) {
  EXPECT_EQ(run("set bridge.source rx"), "OK");
  EXPECT_EQ(prefs.bridge_pkt_src, 1);
  EXPECT_EQ(run("set bridge.source tx"), "OK");
  EXPECT_EQ(prefs.bridge_pkt_src, 0);
  EXPECT_EQ(run("set bridge.source rxy"), "OK");         // "rx" prefix, pinned as is
  EXPECT_EQ(prefs.bridge_pkt_src, 1);
  EXPECT_EQ(takeCalls().size(), 3u);
}

#else

TEST_F(CliTest, SetBridgeCommandsDoNotExistWithoutABridge) {
  EXPECT_EQ(run("set bridge.enabled on"), "unknown config: bridge.enabled on");
  EXPECT_EQ(run("set bridge.delay 5"), "unknown config: bridge.delay 5");
  EXPECT_EQ(run("set bridge.source rx"), "unknown config: bridge.source rx");
  EXPECT_EQ(run("set bridge.baud 9600"), "unknown config: bridge.baud 9600");
  EXPECT_EQ(run("set bridge.channel 3"), "unknown config: bridge.channel 3");
  EXPECT_EQ(run("set bridge.secret x"), "unknown config: bridge.secret x");
  EXPECT_TRUE(takeCalls().empty());
}

#endif

#if defined(WITH_RS232_BRIDGE)

TEST_F(CliTest, SetBridgeBaudRestartsTheBridgeBeforeSaving) {
  EXPECT_EQ(run("set bridge.baud 57600"), "OK");
  EXPECT_EQ(prefs.bridge_baud, 57600u);
  EXPECT_EQ(takeCalls(), (Calls{"restartBridge", "savePrefs advert_interval=90"}));
  EXPECT_EQ(run("set bridge.baud 9600"), "OK");
  EXPECT_EQ(run("set bridge.baud 115200"), "OK");
  EXPECT_EQ(takeCalls().size(), 4u);
}

TEST_F(CliTest, SetBridgeBaudRefusesTheRatesOutsideTheLimits) {
  for (const char* v : {"9599", "115201", "0", "-1", "abc"}) {
    EXPECT_EQ(run(std::string("set bridge.baud ") + v), "Error: baud rate must be between 9600-115200") << v;
  }
  EXPECT_EQ(prefs.bridge_baud, 0u);
  EXPECT_TRUE(takeCalls().empty());
}

#elif defined(WITH_BRIDGE)

TEST_F(CliTest, SetBridgeBaudDoesNotExistWithoutAnRs232Bridge) {
  EXPECT_EQ(run("set bridge.baud 9600"), "unknown config: bridge.baud 9600");
}

#endif

#if defined(WITH_ESPNOW_BRIDGE)

TEST_F(CliTest, SetBridgeChannelTakes1To14AndRestartsTheBridge) {
  EXPECT_EQ(run("set bridge.channel 6"), "OK");
  EXPECT_EQ(prefs.bridge_channel, 6);
  EXPECT_EQ(takeCalls(), (Calls{"restartBridge", "savePrefs advert_interval=90"}));
  EXPECT_EQ(run("set bridge.channel 1"), "OK");
  EXPECT_EQ(run("set bridge.channel 14"), "OK");
  EXPECT_EQ(takeCalls().size(), 4u);
}

TEST_F(CliTest, SetBridgeChannelRefusesZeroAndMoreThan14) {
  for (const char* v : {"0", "15", "-1", "abc"}) {
    EXPECT_EQ(run(std::string("set bridge.channel ") + v), "Error: channel must be between 1-14") << v;
  }
  EXPECT_EQ(prefs.bridge_channel, 0);
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetBridgeSecretIsCutToFifteenCharactersAndRestartsTheBridge) {
  EXPECT_EQ(run("set bridge.secret hunter2"), "OK");
  EXPECT_STREQ(prefs.bridge_secret, "hunter2");
  EXPECT_EQ(run("set bridge.secret 0123456789abcdefXYZ"), "OK");
  EXPECT_STREQ(prefs.bridge_secret, "0123456789abcde");
  EXPECT_EQ(takeCalls(), (Calls{"restartBridge", "savePrefs advert_interval=90",
                                "restartBridge", "savePrefs advert_interval=90"}));
}

#elif defined(WITH_BRIDGE)

TEST_F(CliTest, SetBridgeChannelAndSecretDoNotExistWithoutAnEspNowBridge) {
  EXPECT_EQ(run("set bridge.channel 3"), "unknown config: bridge.channel 3");
  EXPECT_EQ(run("set bridge.secret x"), "unknown config: bridge.secret x");
}

#endif

// ---- extra.sf (LR2021 only) --------------------------------------------------------------

#if defined(USE_LR2021)

TEST_F(CliTest, SetExtraSfPassesTheListToTheRadioAndKeepsItWithAZeroTerminator) {
  EXPECT_EQ(run("set extra.sf 7,9,11"), "OK - extra SFs set");
  EXPECT_EQ(takeCalls(), (Calls{"configSideDetectors num=3 bw=62.5", "savePrefs advert_interval=90"}));
  EXPECT_EQ(cb.side_sfs, (std::vector<uint8_t>{7, 9, 11, 0}));
  EXPECT_EQ(prefs.extra_sf[0], 7);
  EXPECT_EQ(prefs.extra_sf[1], 9);
  EXPECT_EQ(prefs.extra_sf[2], 11);
  EXPECT_EQ(prefs.extra_sf[3], 0);
}

TEST_F(CliTest, SetExtraSfWithNothingClearsTheList) {
  memset(prefs.extra_sf, 5, sizeof(prefs.extra_sf));
  EXPECT_EQ(run("set extra.sf "), "OK - extra SFs set");
  EXPECT_EQ(cb.side_sfs, (std::vector<uint8_t>{0}));
  EXPECT_EQ(prefs.extra_sf[0], 0);
  EXPECT_EQ(prefs.extra_sf[1], 5);                      // only the terminator is written
}

TEST_F(CliTest, SetExtraSfRefusesFourValuesOrARadioThatSaysNo) {
  EXPECT_EQ(run("set extra.sf 7,8,9,10"), "Invalid extra SF config");
  EXPECT_TRUE(takeCalls().empty());                     // never offered to the radio
  cb.side_detectors_ok = false;
  EXPECT_EQ(run("set extra.sf 7"), "Invalid extra SF config");
  EXPECT_EQ(takeCalls(), (Calls{"configSideDetectors num=1 bw=62.5"}));   // no save
  EXPECT_EQ(prefs.extra_sf[0], 0);
}

#else

TEST_F(CliTest, SetExtraSfDoesNotExistWithoutTheLr2021) {
  EXPECT_EQ(run("set extra.sf 7,9"), "unknown config: extra.sf 7,9");
}

#endif

// ---- unknown settings --------------------------------------------------------------------------

TEST_F(CliTest, SetOfAnUnknownSettingEchoesItsText) {
  EXPECT_EQ(run("set nonsense 5"), "unknown config: nonsense 5");
  EXPECT_EQ(run("set "), "unknown config: ");
  EXPECT_EQ(run("set name"), "unknown config: name");      // "name" needs its space
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, SetOfAnUnknownSettingEchoesAtMost142Characters) {
  std::string reply = run("set " + std::string(200, 'z'));
  EXPECT_EQ(reply, "unknown config: " + std::string(142, 'z'));
  EXPECT_EQ(reply.size(), 158u);
}

// ---- order against the radio settings ------------------------------------------------------------

TEST_F(CliTest, RadioSettingsKeepTheirOwnAnswersInTheSetChain) {
  EXPECT_EQ(run("set tx 20"), "OK");
  EXPECT_EQ(prefs.tx_power_dbm, 20);
  EXPECT_EQ(radio_driver.tx_power, 20);
  EXPECT_EQ(run("set radio 869.525,125,8,5"), "OK - reboot to apply");
  EXPECT_EQ(run("set af 1"), "OK");
  EXPECT_EQ(run("set cad on"), "OK");
  EXPECT_EQ(takeCalls().size(), 4u);                       // each one saved
}
