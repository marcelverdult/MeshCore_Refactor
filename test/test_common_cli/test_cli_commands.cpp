// CommonCLI::handleCommand(): the order in which a command is looked at, and every
// command of the main chain that is not "get", "set", "region", "sensor" or "gps".
// "pinned as is" marks behaviour that looks unintended and is kept on purpose: this
// suite describes today's CLI, it does not judge it.

#include <gtest/gtest.h>

#include "CliHarness.h"

static const char* UNTOUCHED = "<untouched>";

// ---- the order: radio prefs, board hook, then the chain ---------------------------

TEST_F(CliTest, RadioCommandsAreAnsweredBeforeTheBoardHookAndTheChain) {
  brd.hook_handles = true;
  brd.hook_reply = "hooked";
  EXPECT_EQ(run("set rxdelay 3"), "OK");
  EXPECT_TRUE(brd.hook_commands.empty());          // the board never saw it
  EXPECT_FLOAT_EQ(prefs.rx_delay_base, 3.0f);
  // the radio object was dirty, so the prefs were saved, and the dirty flag is cleared
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
  EXPECT_FALSE(prefs.isDirty());
}

TEST_F(CliTest, RadioCommandThatChangesNothingDoesNotSave) {
  EXPECT_EQ(run("get rxdelay"), "> 1.5");
  EXPECT_TRUE(takeCalls().empty());
  EXPECT_EQ(run("set rxdelay 99"), "Error, must be 0-20");   // refused: nothing changed
  EXPECT_TRUE(takeCalls().empty());
  EXPECT_FLOAT_EQ(prefs.rx_delay_base, 1.5f);
}

TEST_F(CliTest, BoardHookCommandIsAnsweredBeforeTheChain) {
  brd.hook_handles = true;
  brd.hook_reply = "hooked";
  EXPECT_EQ(run("ver", 77), "hooked");
  ASSERT_EQ(brd.hook_commands.size(), 1u);
  EXPECT_EQ(brd.hook_commands[0], "ver");
  EXPECT_EQ(brd.hook_timestamps[0], 77u);
  EXPECT_TRUE(takeCalls().empty());                // no save: nothing was dirty
}

TEST_F(CliTest, BoardHookThatDirtiesThePrefsGetsThemSaved) {
  brd.hook_handles = true;
  brd.hook_reply = "hooked";
  brd.hook_dirty_prefs = &prefs;
  EXPECT_EQ(run("fem.anything"), "hooked");
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
  EXPECT_FALSE(prefs.isDirty());
}

TEST_F(CliTest, BoardHookThatDeclinesLeavesTheCommandToTheChain) {
  brd.hook_handles = false;
  EXPECT_EQ(run("ver"), "v9.9.9 (Build: 01 Jan 2030)");
  EXPECT_EQ(brd.hook_commands, (std::vector<std::string>{"ver"}));
  // a declined hook that left the prefs dirty does not save: the save only follows a
  // handled hook command
  brd.hook_dirty_prefs = &prefs;
  EXPECT_EQ(run("ver"), "v9.9.9 (Build: 01 Jan 2030)");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, UnknownAndEmptyCommands) {
  EXPECT_EQ(run("frobnicate"), "Unknown command");
  EXPECT_EQ(run(""), "Unknown command");
  EXPECT_EQ(run(" ver"), "Unknown command");       // no leading white space is skipped
  EXPECT_EQ(run("VER"), "Unknown command");        // case sensitive
  EXPECT_TRUE(takeCalls().empty());
}

// ---- prefix matching: pinned as is ------------------------------------------------
// Most commands are compared with memcmp() over the length of their name, so anything
// that starts with the name runs it. A table that matches whole words would change
// these; they are pinned so that is a decision, not an accident.

TEST_F(CliTest, CommandsMatchOnTheirPrefixPinnedAsIs) {
  EXPECT_EQ(run("version"), "v9.9.9 (Build: 01 Jan 2030)");
  EXPECT_EQ(run("verbose please"), "v9.9.9 (Build: 01 Jan 2030)");
  EXPECT_EQ(run("boards"), "Acme Radio");
  EXPECT_EQ(run("clockwork"), clockText(clk.now, ""));
  EXPECT_EQ(run("neighborsx"), "neighbors-reply");
  EXPECT_EQ(run("advertise"), "OK - Advert sent");
  EXPECT_EQ(run("advert.interval"), "OK - Advert sent");   // not a setting: a flood advert
  EXPECT_EQ(run("clear statsx"), "(OK - stats reset)");
  brd.reboot_calls = 0;
  run("rebootx");
  run("poweroffx");
  run("shutdownx");
  EXPECT_EQ(brd.reboot_calls, 1);
  EXPECT_EQ(brd.power_off_calls, 2);
}

// ---- power off, reboot ---------------------------------------------------------

TEST_F(CliTest, PowerOffAndShutdownCallThePowerOffHookAndLeaveTheReply) {
  EXPECT_EQ(run("poweroff"), UNTOUCHED);
  EXPECT_EQ(run("shutdown"), UNTOUCHED);
  EXPECT_EQ(brd.power_off_calls, 2);
  EXPECT_EQ(brd.reboot_calls, 0);
}

TEST_F(CliTest, RebootCallsTheBoardAndLeavesTheReply) {
  EXPECT_EQ(run("reboot"), UNTOUCHED);
  EXPECT_EQ(brd.reboot_calls, 1);
  EXPECT_TRUE(clk.set_calls.empty());
}

TEST_F(CliTest, ClkrebootResetsTheClockToTheFixedInstantThenReboots) {
  EXPECT_EQ(run("clkreboot"), UNTOUCHED);
  EXPECT_EQ(clk.set_calls, (std::vector<uint32_t>{1715770351}));
  EXPECT_EQ(brd.reboot_calls, 1);
}

// ---- adverts ------------------------------------------------------------------

TEST_F(CliTest, AdvertSendsAFloodAdvertAfterAnInitialDelay) {
  EXPECT_EQ(run("advert"), "OK - Advert sent");
  EXPECT_EQ(takeCalls(), (Calls{"sendSelfAdvertisement 1500 flood"}));
}

TEST_F(CliTest, AdvertZerohopSendsAZerohopAdvert) {
  EXPECT_EQ(run("advert.zerohop"), "OK - zerohop advert sent");
  EXPECT_EQ(run("advert.zerohop now"), "OK - zerohop advert sent");   // a space ends the name
  EXPECT_EQ(takeCalls(), (Calls{"sendSelfAdvertisement 1500 zerohop",
                                "sendSelfAdvertisement 1500 zerohop"}));
}

TEST_F(CliTest, AdvertZerohopWithGlueFallsBackToAFloodAdvertPinnedAsIs) {
  EXPECT_EQ(run("advert.zerohopx"), "OK - Advert sent");
  EXPECT_EQ(takeCalls(), (Calls{"sendSelfAdvertisement 1500 flood"}));
}

// ---- the clock ---------------------------------------------------------------

TEST_F(CliTest, ClockReportsTheTimeOfTheClock) {
  EXPECT_EQ(run("clock"), clockText(1760000000, ""));
  clk.now = 1760003661;
  EXPECT_EQ(run("clock"), clockText(1760003661, ""));
  EXPECT_TRUE(clk.set_calls.empty());
}

TEST_F(CliTest, ClockSyncMovesTheClockForwardToTheSenderTimePlusOne) {
  EXPECT_EQ(run("clock sync", 1760000500), clockText(1760000501, "OK - clock set: "));
  EXPECT_EQ(clk.set_calls, (std::vector<uint32_t>{1760000501}));
  EXPECT_EQ(clk.now, 1760000501u);
}

TEST_F(CliTest, ClockSyncNeverMovesTheClockBackwardsOrSideways) {
  EXPECT_EQ(run("clock sync", 1759999999), "ERR: clock cannot go backwards");
  EXPECT_EQ(run("clock sync", 1760000000), "ERR: clock cannot go backwards");   // equal is not forward
  EXPECT_EQ(run("clock sync", 0), "ERR: clock cannot go backwards");            // serial has no sender time
  EXPECT_TRUE(clk.set_calls.empty());
}

TEST_F(CliTest, TimeSetsTheClockToEpochSecondsAndOnlyForward) {
  EXPECT_EQ(run("time 1760000100"), clockText(1760000100, "OK - clock set: "));
  EXPECT_EQ(clk.set_calls, (std::vector<uint32_t>{1760000100}));
  // note the brackets: the other two "backwards" errors have none (pinned as is)
  EXPECT_EQ(run("time 1760000100"), "(ERR: clock cannot go backwards)");
  EXPECT_EQ(run("time 5"), "(ERR: clock cannot go backwards)");
  EXPECT_EQ(clk.set_calls.size(), 1u);
}

TEST_F(CliTest, TimeReadsDigitsOnly) {
  EXPECT_EQ(run("time 1760000200junk"), clockText(1760000200, "OK - clock set: "));   // stops at the first non digit
  EXPECT_EQ(run("time junk"), "(ERR: clock cannot go backwards)");                    // reads as 0
  EXPECT_EQ(run("time -5"), "(ERR: clock cannot go backwards)");                      // no sign
  EXPECT_EQ(run("time 4294967296"), "(ERR: clock cannot go backwards)");              // wraps to 0 in 32 bits
  EXPECT_EQ(run("time "), "(ERR: clock cannot go backwards)");
  EXPECT_EQ(run("time"), "Unknown command");                                          // the space is part of the name
}

// ---- OTA ---------------------------------------------------------------------

TEST_F(CliTest, StartOtaPassesTheNodeNameAndRepliesWhatTheBoardWrote) {
  brd.ota_ok = true;
  brd.ota_reply = "OTA ready: connect to Alpha";
  EXPECT_EQ(run("start ota"), "OTA ready: connect to Alpha");
  EXPECT_EQ(brd.ota_ids, (std::vector<std::string>{"Alpha"}));
}

TEST_F(CliTest, StartOtaThatTheBoardRefusesRepliesError) {
  brd.ota_ok = false;
  brd.ota_reply = "ignored text";
  EXPECT_EQ(run("start ota"), "Error");
  EXPECT_EQ(brd.ota_ids.size(), 1u);
}

TEST_F(CliTest, StartOtaIsNotShadowedByTheClockCommandOfAnotherName) {
  // "start ota" is tried before "clock" and "time ": only its own text reaches it
  EXPECT_EQ(run("start"), "Unknown command");
  EXPECT_TRUE(brd.ota_ids.empty());
}

// ---- neighbors ---------------------------------------------------------------

TEST_F(CliTest, NeighborsAsksTheRoleToFormatTheReply) {
  EXPECT_EQ(run("neighbors"), "neighbors-reply");
  EXPECT_EQ(takeCalls(), (Calls{"formatNeighborsReply"}));
}

TEST_F(CliTest, NeighborRemoveTakesAHexKeyOfAnyEvenLengthUpToTheFullKey) {
  std::string full(64, 'a');   // 32 bytes of 0xAA
  EXPECT_EQ(run("neighbor.remove " + full), "OK");
  EXPECT_EQ(run("neighbor.remove aabb"), "OK");              // a short prefix of a key: 2 bytes
  EXPECT_EQ(run("neighbor.remove 0102030405060708"), "OK");
  EXPECT_EQ(takeCalls(), (Calls{"removeNeighbor 32", "removeNeighbor 2", "removeNeighbor 8"}));
  ASSERT_EQ(cb.removed_neighbors.size(), 3u);
  EXPECT_EQ(cb.removed_neighbors[0], std::vector<uint8_t>(32, 0xAA));
  EXPECT_EQ(cb.removed_neighbors[1], (std::vector<uint8_t>{0xAA, 0xBB}));
  EXPECT_EQ(cb.removed_neighbors[2], (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST_F(CliTest, NeighborRemoveRefusesAnOddOrTooLongKey) {
  EXPECT_EQ(run("neighbor.remove abc"), "ERR: bad pubkey");
  EXPECT_EQ(run("neighbor.remove " + std::string(66, 'a')), "ERR: bad pubkey");   // longer than a key
  EXPECT_EQ(run("neighbor.remove " + std::string(63, 'a')), "ERR: bad pubkey");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, NeighborRemovePinnedAsIs) {
  // An empty key is "valid": zero bytes are removed, which the role takes to mean
  // nothing (or, depending on the role, everything). Non hex characters read as 0.
  EXPECT_EQ(run("neighbor.remove "), "OK");
  EXPECT_EQ(run("neighbor.remove zz"), "OK");
  EXPECT_EQ(takeCalls(), (Calls{"removeNeighbor 0", "removeNeighbor 1"}));
  EXPECT_EQ(cb.removed_neighbors[1], (std::vector<uint8_t>{0}));
  EXPECT_EQ(run("neighbor.remove"), "Unknown command");    // the space is part of the name
}

// ---- tempradio ---------------------------------------------------------------

TEST_F(CliTest, TempradioAppliesValidParametersWithoutTouchingThePrefs) {
  EXPECT_EQ(run("tempradio 869.5,125,8,5,30"), "OK - temp params for 30 mins");
  EXPECT_EQ(takeCalls(), (Calls{"applyTempRadioParams 869.5 125 8 5 30"}));
  EXPECT_FLOAT_EQ(prefs.freq, 869.618f);       // the persisted radio settings stay
  EXPECT_EQ(prefs.sf, 8);
  EXPECT_FALSE(prefs.isDirty());
}

TEST_F(CliTest, TempradioAcceptsTheEdgesOfEachRange) {
  EXPECT_EQ(run("tempradio 150,7,5,5,1"), "OK - temp params for 1 mins");
  EXPECT_EQ(run("tempradio 2500,500,12,8,1"), "OK - temp params for 1 mins");
  EXPECT_EQ(takeCalls(), (Calls{"applyTempRadioParams 150 7 5 5 1",
                                "applyTempRadioParams 2500 500 12 8 1"}));
}

TEST_F(CliTest, TempradioRefusesEachParameterJustOutsideItsRange) {
  const char* bad[] = {
    "tempradio 149.9,125,8,5,30", "tempradio 2500.1,125,8,5,30",   // frequency
    "tempradio 869.5,6.9,8,5,30", "tempradio 869.5,500.1,8,5,30",  // bandwidth (7 here, 7.8 in sanitisePrefs)
    "tempradio 869.5,125,4,5,30", "tempradio 869.5,125,13,5,30",   // spreading factor
    "tempradio 869.5,125,8,4,30", "tempradio 869.5,125,8,9,30",    // coding rate
    "tempradio 869.5,125,8,5,0",  "tempradio 869.5,125,8,5,-3",    // minutes
    "tempradio 869.5,125,8,5",    "tempradio 869.5,125,8",         // too few parts
    "tempradio 869.5",            "tempradio abc,def,ghi,jkl,mno",
  };
  for (const char* c : bad) {
    EXPECT_EQ(run(c), "Error, invalid params") << c;
  }
  EXPECT_TRUE(takeCalls().empty());
  EXPECT_EQ(run("tempradio"), "Unknown command");   // the space is part of the name
}

TEST_F(CliTest, TempradioIgnoresAPartBeyondTheFifthAndLeadingSpacesPinnedAsIs) {
  EXPECT_EQ(run("tempradio 869.5,125,8,5,30,99"), "OK - temp params for 30 mins");
  EXPECT_EQ(run("tempradio  869.5,125,8,5,30"), "OK - temp params for 30 mins");   // strtof() skips the space
  EXPECT_EQ(takeCalls(), (Calls{"applyTempRadioParams 869.5 125 8 5 30",
                                "applyTempRadioParams 869.5 125 8 5 30"}));
}

// ---- password -------------------------------------------------------------------

TEST_F(CliTest, PasswordSetsTheAdminPasswordSavesAndEchoesIt) {
  EXPECT_EQ(run("password newpass"), "password now: newpass");
  EXPECT_STREQ(prefs.password, "newpass");
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
}

TEST_F(CliTest, PasswordIsCutToFifteenCharactersAndTheEchoShowsTheCutPinnedAsIs) {
  EXPECT_EQ(run("password 0123456789abcdefXYZ"), "password now: 0123456789abcde");
  EXPECT_STREQ(prefs.password, "0123456789abcde");
}

TEST_F(CliTest, PasswordMayBeEmptyOrContainSpaces) {
  EXPECT_EQ(run("password "), "password now: ");
  EXPECT_STREQ(prefs.password, "");
  EXPECT_EQ(run("password two words"), "password now: two words");
  EXPECT_EQ(run("password"), "Unknown command");
}

// ---- clear stats, erase, ver, board, power ----------------------------------------

TEST_F(CliTest, ClearStatsResetsTheStats) {
  EXPECT_EQ(run("clear stats"), "(OK - stats reset)");
  EXPECT_EQ(takeCalls(), (Calls{"clearStats"}));
  EXPECT_EQ(run("clear"), "Unknown command");
}

TEST_F(CliTest, EraseFormatsTheFileSystemFromTheSerialConsoleOnly) {
  EXPECT_EQ(run("erase"), "File system erase: OK");
  cb.format_ok = false;
  EXPECT_EQ(run("erase"), "File system erase: Err");
  EXPECT_EQ(takeCalls(), (Calls{"formatFileSystem", "formatFileSystem"}));
  // over the air, and with anything after the word, it is not the erase command
  EXPECT_EQ(run("erase", 5), "Unknown command");
  EXPECT_EQ(run("erase now"), "Unknown command");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, VerShowsFirmwareVersionAndBuildDate) {
  EXPECT_EQ(run("ver"), "v9.9.9 (Build: 01 Jan 2030)");
  cb.fw_ver = "v1.18.0";
  cb.build_date = "08 Nov 2026";
  EXPECT_EQ(run("ver"), "v1.18.0 (Build: 08 Nov 2026)");
}

TEST_F(CliTest, BoardShowsTheManufacturerName) {
  EXPECT_EQ(run("board"), "Acme Radio");
  brd.manufacturer = "Other";
  EXPECT_EQ(run("board"), "Other");
}

TEST_F(CliTest, PowerReportsBatteryAndTheSourcesTheBoardCanDetect) {
  EXPECT_EQ(run("power"), "batt:3987 mV usb:n/a solar_chg:n/a ext:no charger:no");
  brd.usb_detect = true;
  brd.solar_detect = true;
  EXPECT_EQ(run("power"), "batt:3987 mV usb:no solar_chg:no ext:no charger:no");
  brd.usb_powered = true;
  brd.solar_active = true;
  brd.ext_powered = true;
  brd.charger = true;
  brd.batt_mv = 4200;
  EXPECT_EQ(run("power"), "batt:4200 mV usb:yes solar_chg:yes ext:yes charger:yes");
  // a source the board cannot detect reads n/a whatever its state says
  brd.usb_detect = false;
  EXPECT_EQ(run("power"), "batt:4200 mV usb:n/a solar_chg:yes ext:yes charger:yes");
}

TEST_F(CliTest, PowerIsAnExactWordUnlikeMostCommands) {
  EXPECT_EQ(run("power "), "Unknown command");
  EXPECT_EQ(run("powerx"), "Unknown command");
}

// ---- power saving: what a build supports decides the answer --------------------------

TEST_F(CliTest, PowersavingOnDependsOnThePlatform) {
  std::string r = run("powersaving on");
#if defined(NRF52_PLATFORM)
  EXPECT_EQ(r, "on - Immediate effect");
  EXPECT_EQ(prefs.powersaving_enabled, 1);
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
#elif defined(ESP32) && !defined(WITH_BRIDGE)
  EXPECT_EQ(r, "on - After 2 minutes");
  EXPECT_EQ(prefs.powersaving_enabled, 1);
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
#elif defined(WITH_BRIDGE)
  EXPECT_EQ(r, "Bridge not supported");
  EXPECT_EQ(prefs.powersaving_enabled, 0);
  EXPECT_TRUE(takeCalls().empty());
#else
  EXPECT_EQ(r, "Board not supported");
  EXPECT_EQ(prefs.powersaving_enabled, 0);
  EXPECT_TRUE(takeCalls().empty());
#endif
}

TEST_F(CliTest, PowersavingOffAlwaysTurnsItOffAndSaves) {
  prefs.powersaving_enabled = 1;
  EXPECT_EQ(run("powersaving off"), "off");
  EXPECT_EQ(prefs.powersaving_enabled, 0);
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
}

TEST_F(CliTest, PowersavingAloneReportsTheSetting) {
  prefs.powersaving_enabled = 0;
  EXPECT_EQ(run("powersaving"), "off");
  prefs.powersaving_enabled = 1;
  EXPECT_EQ(run("powersaving"), "on");
  EXPECT_EQ(run("powersaving maybe"), "on");       // any other tail: the report, pinned as is
  EXPECT_TRUE(takeCalls().empty());
}

// ---- log ---------------------------------------------------------------------------

TEST_F(CliTest, LogStartStopEraseWorkOverTheAirToo) {
  EXPECT_EQ(run("log start", 9), "   logging on");
  EXPECT_EQ(run("log stop", 9), "   logging off");
  EXPECT_EQ(run("log erase", 9), "   log erased");
  EXPECT_EQ(takeCalls(), (Calls{"setLoggingOn true", "setLoggingOn false", "eraseLogFile"}));
}

TEST_F(CliTest, LogAloneDumpsTheLogToTheSerialConsoleOnly) {
  EXPECT_EQ(run("log"), "   EOF");
  EXPECT_EQ(run("log dump"), "   EOF");               // any tail, pinned as is
  EXPECT_EQ(takeCalls(), (Calls{"dumpLogFile", "dumpLogFile"}));
  EXPECT_EQ(run("log", 9), "Unknown command");
  EXPECT_EQ(run("log dump", 9), "Unknown command");
  EXPECT_TRUE(takeCalls().empty());
}

// ---- stats-* (serial console only) --------------------------------------------------

TEST_F(CliTest, StatsCommandsAnswerOnlyOnTheSerialConsoleAndOnlyAsWholeWords) {
  EXPECT_EQ(run("stats-packets"), "packet-stats-reply");
  EXPECT_EQ(run("stats-radio"), "radio-stats-reply");
  EXPECT_EQ(run("stats-core"), "stats-reply");
  EXPECT_EQ(run("stats-packets extra"), "packet-stats-reply");   // a space ends the word
  EXPECT_EQ(takeCalls(), (Calls{"formatPacketStatsReply", "formatRadioStatsReply",
                                "formatStatsReply", "formatPacketStatsReply"}));
  EXPECT_EQ(run("stats-packetsx"), "Unknown command");
  EXPECT_EQ(run("stats-radiox"), "Unknown command");
  EXPECT_EQ(run("stats-corex"), "Unknown command");
  EXPECT_EQ(run("stats-packets", 5), "Unknown command");
  EXPECT_EQ(run("stats-radio", 5), "Unknown command");
  EXPECT_EQ(run("stats-core", 5), "Unknown command");
  EXPECT_TRUE(takeCalls().empty());
}

// ---- how get, set and region are routed ------------------------------------------------

TEST_F(CliTest, GetAndSetNeedTheirTrailingSpace) {
  EXPECT_EQ(run("get"), "Unknown command");
  EXPECT_EQ(run("set"), "Unknown command");
  EXPECT_EQ(run("getname"), "Unknown command");
  EXPECT_EQ(run("get name"), "> Alpha");
  EXPECT_EQ(run("set name Beta"), "OK");
}

TEST_F(CliTest, RegionIsRoutedByItsPrefix) {
  // an empty map exports its wildcard line; "regions" and "regionfoo" are the region
  // command too, because only the first six characters are compared (pinned as is)
  EXPECT_EQ(run("region"), "*^ F\n");
  EXPECT_EQ(run("regions"), "*^ F\n");
  EXPECT_EQ(run("regionfoo"), "*^ F\n");
}

TEST_F(CliTest, SensorCommandsNeedTheirSubcommandAndSpace) {
  EXPECT_EQ(run("sensor"), "Unknown command");
  EXPECT_EQ(run("sensor get"), "Unknown command");      // the space and the key are part of the name
}
