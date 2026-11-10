// The companion's text CLI, MyMesh::handleCommand(): the order in which a command is
// looked at, and the nine commands every build has (reboot, poweroff and shutdown,
// set name, get name, set pin, board, ver, get tz.offset, set tz.offset). Step 13d-a of
// docs/refactoring/0013-cli-command-table.md pins all of it before the if chain
// becomes a table. The commands that exist only with PIN_BUZZER, ENABLE_WIFI_INTERFACE
// or DISPLAY_CLASS are in test_cli_buzzer.cpp, test_cli_wifi.cpp and test_cli_ui.cpp.
//
// Expectations describe the firmware as it is, including what looks odd; such places
// say "pinned as is". Replies are exact. "Saved" means /prefs.json was written once
// while the command ran.

#include "CliHarness.h"

using namespace companion;

namespace {

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

std::string lower(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

const std::string NODE_NAME_AFTER_BOOT = "A0A1A2A3";   // the first four bytes of the key, in hex

}  // namespace

// ---- order of dispatch ---------------------------------------------------------

TEST_F(CliTest, LeadingSpacesAreSkipped) {
  EXPECT_EQ(say("get name"), "> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(say("   get name"), "> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(say(" set name Bob"), "OK");
  EXPECT_EQ(say("  \x20 get name"), "> Bob");
}

TEST_F(CliTest, OnlyTheSpaceIsSkippedNotTabsOrNewlines) {
  EXPECT_EQ(say("\tget name"), "Unknown command");
  EXPECT_EQ(say("\nget name"), "Unknown command");
  EXPECT_EQ(say("get name\n"), "Unknown command");   // pinned as is: no trimming at the end
}

TEST_F(CliTest, EmptyAndBlankCommandsAreUnknown) {
  EXPECT_EQ(say(""), "Unknown command");
  EXPECT_EQ(say(" "), "Unknown command");
  EXPECT_EQ(say("      "), "Unknown command");
}

TEST_F(CliTest, TheCallerAppendsUnknownCommandAndNothingIsSaved) {
  for (const char* c : {"help", "get", "set", "get nosuchkey", "set nosuchkey 1", "GET NAME", "Get name",
                        "name", "reset", "factory", "advert", "clock", "get  name"}) {
    CliResult r = run(c);
    EXPECT_EQ(r.text, "Unknown command") << c;
    EXPECT_EQ(r.saves, 0) << c;
  }
  EXPECT_TRUE(board.hook_commands.size() >= 13);   // each one went to the board hook first
  EXPECT_EQ(board.reboot_calls, 0);
  EXPECT_EQ(board.power_off_calls, 0);
}

// ---- the optional prefix "xx|" ---------------------------------------------------

TEST_F(CliTest, ThePrefixIsReflectedAheadOfTheReply) {
  EXPECT_EQ(say("xx|get name"), "xx|> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(say("a1|ver"), "a1|" + std::string(FIRMWARE_VERSION) + " (Build: " + FIRMWARE_BUILD_DATE + ")");
  EXPECT_EQ(say("..|board"), "..|Mock");
  CliResult r = run("12|set name Bob");
  EXPECT_EQ(r.text, "12|OK");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("12|get name"), "12|> Bob");
}

TEST_F(CliTest, ThePrefixIsAnyTwoBytesFollowedByABar) {
  EXPECT_EQ(say("\xc3\xa4|get name"), "\xc3\xa4|> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(say("|||get name"), "|||> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(say("  xx|get name"), "xx|> " + NODE_NAME_AFTER_BOOT);   // leading spaces go first
}

TEST_F(CliTest, ThePrefixNeedsMoreThanFourCharactersInAll) {
  // pinned as is: strlen(command) > 4. "xx|a" is four, so it is a command called "xx|a".
  EXPECT_EQ(say("xx|a"), "Unknown command");
  EXPECT_EQ(say("xx|"), "Unknown command");
  EXPECT_EQ(say("xx|ab"), "xx|Unknown command");
}

TEST_F(CliTest, TheBarMustBeTheThirdCharacter) {
  EXPECT_EQ(say("x|get name"), "Unknown command");
  EXPECT_EQ(say("xxx|get name"), "Unknown command");
  EXPECT_EQ(say("xx get name"), "Unknown command");
}

TEST_F(CliTest, NoSpacesAreSkippedAfterThePrefix) {
  EXPECT_EQ(say("xx| get name"), "xx|Unknown command");   // pinned as is
}

TEST_F(CliTest, ACommandThatWritesNothingLeavesJustThePrefix) {
  EXPECT_EQ(say("xx|reboot"), "xx|");
  EXPECT_EQ(board.reboot_calls, 1);
  EXPECT_EQ(say("xx|poweroff"), "xx|");
  EXPECT_EQ(board.power_off_calls, 1);
}

// ---- radio settings, board hook, then the commands --------------------------------

TEST_F(CliTest, TheRadioTableComesFirstAndTheBoardNeverSeesItsCommands) {
  EXPECT_EQ(say("get radio"), "> 915.0,250,10,5");
  EXPECT_TRUE(board.hook_commands.empty());
  CliResult r = run("set rxdelay 3");
  EXPECT_EQ(r.text, "OK");
  EXPECT_EQ(r.saves, 1);                       // the table's change is saved
  EXPECT_TRUE(board.hook_commands.empty());
  EXPECT_EQ(run("get radio").saves, 0);         // a read changes nothing, so nothing is saved
}

TEST_F(CliTest, ARadioCommandWithAPrefixIsSavedToo) {
  CliResult r = run("xx|set rxdelay 4");
  EXPECT_EQ(r.text, "xx|OK");
  EXPECT_EQ(r.saves, 1);
  EXPECT_TRUE(contains(prefsFile(), "rxdelay:4.0000"));
}

TEST_F(CliTest, ARadioSettingErrorIsNotSaved) {
  CliResult r = run("set rxdelay 99");
  EXPECT_EQ(r.text, "Error, must be 0-20");
  EXPECT_EQ(r.saves, 0);
}

TEST_F(CliTest, TheBoardHookSeesTheCommandWithoutThePrefixAndWithTheTimestamp) {
  board.hook_handles = true;
  board.hook_reply = "from board";
  EXPECT_EQ(run("xx|get name", 4242).text, "xx|from board");
  ASSERT_EQ(board.hook_commands.size(), 1u);
  EXPECT_EQ(board.hook_commands[0], "get name");
  EXPECT_EQ(board.hook_timestamps[0], 4242u);
}

TEST_F(CliTest, ALeadingSpaceIsNotPassedToTheBoardHook) {
  board.hook_handles = true;
  board.hook_reply = "x";
  say("   get name");
  ASSERT_EQ(board.hook_commands.size(), 1u);
  EXPECT_EQ(board.hook_commands[0], "get name");
}

TEST_F(CliTest, ACommandTheBoardHandlesDoesNotReachItsBranch) {
  board.hook_handles = true;
  board.hook_reply = "mine";
  EXPECT_EQ(say("reboot"), "mine");
  EXPECT_EQ(board.reboot_calls, 0);
  EXPECT_EQ(say("poweroff"), "mine");
  EXPECT_EQ(board.power_off_calls, 0);
  EXPECT_EQ(say("get name"), "mine");
  EXPECT_EQ(say("anything at all"), "mine");   // and so no "Unknown command"
}

TEST_F(CliTest, ACommandTheBoardDoesNotHandleFallsThrough) {
  board.hook_handles = false;
  EXPECT_EQ(say("get name"), "> " + NODE_NAME_AFTER_BOOT);
  ASSERT_EQ(board.hook_commands.size(), 1u);
  EXPECT_EQ(board.hook_commands[0], "get name");
  EXPECT_EQ(say("nothing"), "Unknown command");
  EXPECT_EQ(board.hook_commands.size(), 2u);
}

TEST_F(CliTest, ABoardCommandThatChangedAPrefIsSaved) {
  ASSERT_NE(board.dynamic_prefs, nullptr);   // the node handed its store to the board at boot
  board.hook_handles = true;
  board.hook_reply = "ok";
  board.hook_side_effect = [] { board.dynamic_prefs->setByKey("fem_txgain", "1"); };
  CliResult r = run("fem.tx 1");
  EXPECT_EQ(r.text, "ok");
  EXPECT_EQ(r.saves, 1);
}

TEST_F(CliTest, ABoardCommandThatChangedNothingIsNotSaved) {
  board.hook_handles = true;
  board.hook_reply = "ok";
  CliResult r = run("fem.tx 1");
  EXPECT_EQ(r.text, "ok");
  EXPECT_EQ(r.saves, 0);
}

TEST_F(CliTest, AChangeTheBoardMadeButDidNotAnswerForIsNotSavedByItself) {
  // pinned as is: the save after the hook is only on the "handled" path
  board.hook_handles = false;
  board.hook_side_effect = [] { board.dynamic_prefs->setByKey("fem_txgain", "1"); };
  CliResult r = run("get name");
  EXPECT_EQ(r.text, "> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(r.saves, 0);
  board.hook_side_effect = nullptr;
  // the next save picks the change up
  EXPECT_EQ(run("set name Bob").saves, 1);
  EXPECT_TRUE(contains(prefsFile(), "fem_txgain:1"));
}

// ---- reboot, poweroff, shutdown -----------------------------------------------------

TEST_F(CliTest, RebootCallsTheBoardOnceAndSavesNothing) {
  CliResult r = run("reboot");
  EXPECT_EQ(r.text, "");
  EXPECT_EQ(r.saves, 0);
  EXPECT_EQ(board.reboot_calls, 1);
  EXPECT_EQ(board.power_off_calls, 0);
}

TEST_F(CliTest, PoweroffAndShutdownBothPowerOff) {
  CliResult a = run("poweroff");
  EXPECT_EQ(a.text, "");
  EXPECT_EQ(a.saves, 0);
  EXPECT_EQ(board.power_off_calls, 1);
  CliResult b = run("shutdown");
  EXPECT_EQ(b.text, "");
  EXPECT_EQ(b.saves, 0);
  EXPECT_EQ(board.power_off_calls, 2);
  EXPECT_EQ(board.reboot_calls, 0);
}

TEST_F(CliTest, RebootPoweroffAndShutdownMatchTheWholeCommand) {
  // unlike the repeater's CLI, which matches the start of the line
  for (const char* c : {"reboot ", "rebootx", "reboot now", "Reboot", "REBOOT", "poweroff ", "poweroffx",
                        "shutdown now", "shutdownx", "power off", "shut down"}) {
    EXPECT_EQ(say(c), "Unknown command") << c;
  }
  EXPECT_EQ(board.reboot_calls, 0);
  EXPECT_EQ(board.power_off_calls, 0);
}

// ---- set name, get name -------------------------------------------------------------

TEST_F(CliTest, SetNameStoresAndSaves) {
  CliResult r = run("set name Bob");
  EXPECT_EQ(r.text, "OK");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get name"), "> Bob");
  EXPECT_TRUE(contains(prefsFile(), "name:\"Bob\""));
}

TEST_F(CliTest, TheNameSurvivesARestart) {
  EXPECT_EQ(say("set name Persisted Name"), "OK");
  reboot();
  EXPECT_EQ(say("get name"), "> Persisted Name");
}

TEST_F(CliTest, TheNameIsTheRestOfTheLineSpacesAndAll) {
  EXPECT_EQ(say("set name Bob  the   Builder "), "OK");
  EXPECT_EQ(say("get name"), "> Bob  the   Builder ");   // pinned as is: nothing is trimmed
}

TEST_F(CliTest, ANameWithNonAsciiCharactersIsAccepted) {
  EXPECT_EQ(say("set name Caf\xc3\xa9 \xe2\x9c\x93"), "OK");
  EXPECT_EQ(say("get name"), "> Caf\xc3\xa9 \xe2\x9c\x93");
}

TEST_F(CliTest, SetNameRefusesEachOfTheSevenBadCharactersAndSavesNothing) {
  EXPECT_EQ(say("set name Keep"), "OK");
  for (char bad : {'[', ']', '\\', ':', ',', '?', '*'}) {
    std::string cmd = std::string("set name a") + bad + "b";
    CliResult r = run(cmd);
    EXPECT_EQ(r.text, "Error, bad chars") << cmd;
    EXPECT_EQ(r.saves, 0) << cmd;
    EXPECT_EQ(say("get name"), "> Keep") << cmd;
  }
}

TEST_F(CliTest, OtherPunctuationInANameIsFine) {
  for (const char* n : {"a/b", "a.b", "a-b", "a_b", "a'b", "a\"b", "a(b)", "a{b}", "a|b", "a&b", "a#b", "a@b"}) {
    EXPECT_EQ(say(std::string("set name ") + n), "OK") << n;
    EXPECT_EQ(say("get name"), std::string("> ") + n) << n;
  }
}

TEST_F(CliTest, ALongNameIsCutAt31CharactersWithoutAnError) {
  std::string name31(31, 'n');
  EXPECT_EQ(say("set name " + name31), "OK");
  EXPECT_EQ(say("get name"), "> " + name31);
  std::string name32(32, 'm');
  EXPECT_EQ(say("set name " + name32), "OK");
  EXPECT_EQ(say("get name"), "> " + std::string(31, 'm'));   // pinned as is: cut, no error
  std::string name150(150, 'k');
  EXPECT_EQ(say("set name " + name150), "OK");
  EXPECT_EQ(say("get name"), "> " + std::string(31, 'k'));
  EXPECT_EQ(say("xx|get name"), "xx|> " + std::string(31, 'k'));
}

TEST_F(CliTest, ABadCharacterBeyondTheCutStillRefusesTheName) {
  // pinned as is: the whole text is checked, then it is cut
  std::string name = std::string(40, 'n') + "?";
  EXPECT_EQ(say("set name " + name), "Error, bad chars");
}

TEST_F(CliTest, AnEmptyNameIsAccepted) {
  EXPECT_EQ(say("set name Bob"), "OK");
  CliResult r = run("set name ");
  EXPECT_EQ(r.text, "OK");                 // pinned as is: the node then has no name
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get name"), "> ");
}

TEST_F(CliTest, SetNameNeedsTheSpaceAfterTheKey) {
  EXPECT_EQ(say("set name"), "Unknown command");
  EXPECT_EQ(say("set namex Bob"), "Unknown command");
  EXPECT_EQ(say("setname Bob"), "Unknown command");
  EXPECT_EQ(say("get name"), "> " + NODE_NAME_AFTER_BOOT);
}

TEST_F(CliTest, GetNameMatchesTheWholeCommand) {
  for (const char* c : {"get name ", "get namex", "get names", "get name Bob", "get  name"}) {
    EXPECT_EQ(say(c), "Unknown command") << c;
  }
}

// ---- set pin ----------------------------------------------------------------------

TEST_F(CliTest, SetPinStoresAndSavesAndPadsToSixDigits) {
  CliResult r = run("set pin 123456");
  EXPECT_EQ(r.text, "> pin is now 123456");
  EXPECT_EQ(r.saves, 1);
  EXPECT_TRUE(contains(prefsFile(), "pin:123456"));
  EXPECT_EQ(say("set pin 12"), "> pin is now 000012");
  EXPECT_EQ(say("set pin 0"), "> pin is now 000000");
  EXPECT_TRUE(contains(prefsFile(), "pin:0,"));
}

TEST_F(CliTest, SetPinHasNoRangeCheck) {
  // pinned as is: atoi() and no limit, so the pin is not limited to six digits or to
  // positive numbers
  EXPECT_EQ(say("set pin 1234567"), "> pin is now 1234567");
  EXPECT_EQ(say("set pin 99999999"), "> pin is now 99999999");
  CliResult neg = run("set pin -5");
  EXPECT_EQ(neg.text, "> pin is now -00005");
  EXPECT_EQ(neg.saves, 1);
  EXPECT_TRUE(contains(prefsFile(), "pin:4294967291"));   // stored as an unsigned number
}

TEST_F(CliTest, SetPinReadsWhatAtoiReads) {
  EXPECT_EQ(say("set pin abc"), "> pin is now 000000");
  EXPECT_EQ(say("set pin 12abc"), "> pin is now 000012");
  EXPECT_EQ(say("set pin   34"), "> pin is now 000034");
  EXPECT_EQ(say("set pin +56"), "> pin is now 000056");
  EXPECT_EQ(say("set pin "), "> pin is now 000000");   // nothing after the space: 0, and saved
}

TEST_F(CliTest, SetPinNeedsTheSpaceAndThereIsNoGetPin) {
  EXPECT_EQ(say("set pin"), "Unknown command");
  EXPECT_EQ(say("set pinx 12"), "Unknown command");
  EXPECT_EQ(say("get pin"), "Unknown command");
}

TEST_F(CliTest, SetPinShowsInTheFrameProtocolAfterARestart) {
  // CMD_APP_START answers with the pin; the CLI wrote the same field
  EXPECT_EQ(say("set pin 424242"), "> pin is now 424242");
  reboot();
  EXPECT_TRUE(contains(prefsFile(), "pin:424242"));
}

// ---- board, ver ---------------------------------------------------------------------

TEST_F(CliTest, BoardAnswersTheManufacturerName) {
  CliResult r = run("board");
  EXPECT_EQ(r.text, "Mock");
  EXPECT_EQ(r.saves, 0);
}

TEST_F(CliTest, VerAnswersVersionAndBuildDate) {
  CliResult r = run("ver");
  EXPECT_EQ(r.text, std::string(FIRMWARE_VERSION) + " (Build: " + FIRMWARE_BUILD_DATE + ")");
  EXPECT_EQ(r.saves, 0);
}

TEST_F(CliTest, BoardAndVerMatchTheWholeCommand) {
  for (const char* c : {"board ", "boardx", "board name", "Board", "ver ", "verx", "version", "Ver", "VER"}) {
    EXPECT_EQ(say(c), "Unknown command") << c;
  }
}

// ---- get tz.offset, set tz.offset ---------------------------------------------------

TEST_F(CliTest, TheTimeZoneStartsAtZero) {
  EXPECT_EQ(say("get tz.offset"), "> 0.0");
}

TEST_F(CliTest, SetTzOffsetStoresAndSaves) {
  CliResult r = run("set tz.offset 2");
  EXPECT_EQ(r.text, "OK");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get tz.offset"), "> 2.0");
  EXPECT_TRUE(contains(prefsFile(), "tz_offset:2"));
  reboot();
  EXPECT_EQ(say("get tz.offset"), "> 2.0");
}

TEST_F(CliTest, GetTzOffsetShowsTwoDecimalsOnlyForQuarterHours) {
  // pinned as is: "%.2f" when the offset is an odd number of quarter hours, "%.1f" otherwise
  struct Row { const char* set; const char* get; };
  const Row rows[] = {
    {"0", "> 0.0"},        {"1", "> 1.0"},         {"5.5", "> 5.5"},       {"5.75", "> 5.75"},
    {"5.25", "> 5.25"},    {"0.25", "> 0.25"},     {"0.75", "> 0.75"},     {"-3.5", "> -3.5"},
    {"-5.75", "> -5.75"},  {"-0.5", "> -0.5"},     {"9.5", "> 9.5"},       {"-12", "> -12.0"},
    {"14", "> 14.0"},      {"1e1", "> 10.0"},
  };
  for (const Row& row : rows) {
    EXPECT_EQ(say(std::string("set tz.offset ") + row.set), "OK") << row.set;
    EXPECT_EQ(say("get tz.offset"), row.get) << row.set;
  }
}

TEST_F(CliTest, OtherFractionsAreRoundedToOneDecimalOnTheWayOut) {
  // pinned as is: the value is kept as entered, only the answer is rounded
  EXPECT_EQ(say("set tz.offset 3.14"), "OK");
  EXPECT_EQ(say("get tz.offset"), "> 3.1");
  EXPECT_EQ(say("set tz.offset 0.125"), "OK");
  EXPECT_EQ(say("get tz.offset"), "> 0.1");
  EXPECT_EQ(say("set tz.offset 5.1"), "OK");
  EXPECT_EQ(say("get tz.offset"), "> 5.1");
}

TEST_F(CliTest, SetTzOffsetAcceptsMinus12To14AndRefusesTheRest) {
  EXPECT_EQ(say("set tz.offset -12"), "OK");
  EXPECT_EQ(say("set tz.offset 14"), "OK");
  EXPECT_EQ(say("get tz.offset"), "> 14.0");
  for (const char* bad : {"-12.01", "-13", "14.01", "15", "100", "-1000", "1e3", "inf", "-inf"}) {
    CliResult r = run(std::string("set tz.offset ") + bad);
    EXPECT_EQ(r.text, "Error, must be from -12 to +14") << bad;
    EXPECT_EQ(r.saves, 0) << bad;
    EXPECT_EQ(say("get tz.offset"), "> 14.0") << bad;   // unchanged
  }
}

TEST_F(CliTest, SetTzOffsetReadsWhatAtofReads) {
  EXPECT_EQ(say("set tz.offset abc"), "OK");   // pinned as is: no digits reads as 0
  EXPECT_EQ(say("get tz.offset"), "> 0.0");
  EXPECT_EQ(say("set tz.offset 5.5abc"), "OK");
  EXPECT_EQ(say("get tz.offset"), "> 5.5");
  EXPECT_EQ(say("set tz.offset   +2"), "OK");
  EXPECT_EQ(say("get tz.offset"), "> 2.0");
  CliResult r = run("set tz.offset ");
  EXPECT_EQ(r.text, "OK");                     // nothing after the space is 0, and saved
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get tz.offset"), "> 0.0");
}

TEST_F(CliTest, SetTzOffsetRefusesNaNLikeAnyOtherValueOutsideTheRange) {
  // atof("nan") is NaN, and every comparison with NaN is false. The check is written as
  // "not (-12 <= tz <= 14)", so NaN is refused, nothing is saved and the offset stays.
  ASSERT_EQ(say("set tz.offset 5.5"), "OK");
  for (const char* bad : {"nan", "NaN", "-nan", "nan(1)"}) {
    CliResult r = run(std::string("set tz.offset ") + bad);
    EXPECT_EQ(r.text, "Error, must be from -12 to +14") << bad;
    EXPECT_EQ(r.saves, 0) << bad;
    EXPECT_EQ(say("get tz.offset"), "> 5.5") << bad;   // unchanged
  }
  EXPECT_FALSE(contains(prefsFile(), "tz_offset:nan"));
}

TEST_F(CliTest, TzOffsetCommandsNeedTheirExactSpelling) {
  EXPECT_EQ(say("get tz.offset "), "Unknown command");
  EXPECT_EQ(say("get tz.offsetx"), "Unknown command");
  EXPECT_EQ(say("get tz"), "Unknown command");
  EXPECT_EQ(say("set tz.offset"), "Unknown command");   // the space after the key is part of the match
  EXPECT_EQ(say("set tz.offsetx 2"), "Unknown command");
  EXPECT_EQ(say("set tz 2"), "Unknown command");
}

// ---- the same through the app's frame ----------------------------------------------

TEST_F(CliTest, TheAppFrameRunsTheSameCommandsWithTimestampZero) {
  CliResult a = runFrame("get name");
  EXPECT_EQ(a.text, "> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(a.saves, 0);
  CliResult b = runFrame("set name Frame");
  EXPECT_EQ(b.text, "OK");
  EXPECT_EQ(b.saves, 1);
  EXPECT_EQ(say("get name"), "> Frame");
  board.hook_handles = false;
  runFrame("get radio");
  runFrame("ver");
  ASSERT_GE(board.hook_timestamps.size(), 1u);
  for (uint32_t ts : board.hook_timestamps) EXPECT_EQ(ts, 0u);   // the radio table took "get radio"
}

TEST_F(CliTest, TheAppFrameAppendsUnknownCommandAndReflectsThePrefix) {
  EXPECT_EQ(runFrame("nothing").text, "Unknown command");
  EXPECT_EQ(runFrame("xx|nothing").text, "xx|Unknown command");
  EXPECT_EQ(runFrame("xx|get name").text, "xx|> " + NODE_NAME_AFTER_BOOT);
}

TEST_F(CliTest, TheAppFrameForReboot) {
  CliResult r = runFrame("reboot");
  EXPECT_EQ(r.text, "");          // the reply frame is the code byte alone
  EXPECT_EQ(board.reboot_calls, 1);
}

TEST_F(CliTest, TheAppFrameReplyBufferStartsEmptyForEachCommand) {
  // the node's reply_buf is reused; a command that writes nothing must not show the
  // previous reply
  EXPECT_EQ(runFrame("get name").text, "> " + NODE_NAME_AFTER_BOOT);
  EXPECT_EQ(runFrame("reboot").text, "");
  EXPECT_EQ(runFrame("poweroff").text, "");
}

TEST_F(CliTest, ACommandFromAContactWithoutTheCliFlagIsNotRun) {
  uint8_t area[CLI_GUARD + CLI_REPLY_CAPACITY + CLI_GUARD];
  memset(area, CLI_GUARD_BYTE, sizeof(area));
  char* reply = reinterpret_cast<char*>(area + CLI_GUARD);
  reply[0] = 0;
  g_events.clear();
  h.mesh().cliRemote(0x00, 1234, "set name Intruder", reply);
  h.mesh().cliRemote(0x0F, 1234, "set name Intruder", reply);   // every other flag, but not 0x10
  EXPECT_EQ(std::string(reply), "");
  EXPECT_EQ(eventCount("open:/prefs.json"), 0);
  EXPECT_TRUE(board.hook_commands.empty());
  EXPECT_EQ(say("get name"), "> " + NODE_NAME_AFTER_BOOT);
}

#if !defined(PIN_BUZZER)
TEST_F(CliTest, WithoutPinBuzzerThereAreNoBuzzerCommands) {
  EXPECT_EQ(say("get buzzer"), "Unknown command");
  EXPECT_EQ(say("set buzzer off"), "Unknown command");
}
#endif

#if !defined(ENABLE_WIFI_INTERFACE)
TEST_F(CliTest, WithoutTheWifiInterfaceThereAreNoWifiCommands) {
  for (const char* c : {"set wifi.ssid x", "set wifi.pwd x", "get wifi.pwd", "set wifi.clear", "get wifi.ssid",
                        "set wifi.enabled 1", "get wifi.enabled", "get wifi.status", "get wifi.ip"}) {
    EXPECT_EQ(say(c), "Unknown command") << c;
  }
  EXPECT_FALSE(contains(prefsFile(), "wifi"));
}
#endif

#if !defined(DISPLAY_CLASS)
TEST_F(CliTest, WithoutADisplayThereAreNoUiCommands) {
  for (const char* k : {"show_clock", "auto_off_delay", "backlight_delay", "backlight_on_keys",
                        "backlight_on_msgs", "auto_off_blank", "auto_off_keep_msgs", "max_partial", "nd_name"}) {
    EXPECT_EQ(say(std::string("set ui.") + k + " 1"), "Unknown command") << k;
    EXPECT_EQ(say(std::string("get ui.") + k), "Unknown command") << k;
  }
}
#endif
