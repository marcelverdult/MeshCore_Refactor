// The companion's text CLI, buzzer commands (built with PIN_BUZZER): `get buzzer` and
// `set buzzer`. Step 13d-a of docs/refactoring/0013-cli-command-table.md. See
// test_cli_core.cpp for the commands every build has.

#include "CliHarness.h"

#ifdef PIN_BUZZER

using namespace companion;

namespace {
bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}
}  // namespace

TEST_F(CliTest, TheBuzzerIsOnByDefault) {
  CliResult r = run("get buzzer");
  EXPECT_EQ(r.text, "> on");
  EXPECT_EQ(r.saves, 0);
}

TEST_F(CliTest, SetBuzzerOffAndOnStoresAndSaves) {
  CliResult off = run("set buzzer off");
  EXPECT_EQ(off.text, "OK");
  EXPECT_EQ(off.saves, 1);
  EXPECT_EQ(say("get buzzer"), "> off");
  EXPECT_TRUE(contains(prefsFile(), "buzz_q:1"));   // the stored flag is "quiet"
  CliResult on = run("set buzzer on");
  EXPECT_EQ(on.text, "OK");
  EXPECT_EQ(on.saves, 1);
  EXPECT_EQ(say("get buzzer"), "> on");
  EXPECT_TRUE(contains(prefsFile(), "buzz_q:0"));
}

TEST_F(CliTest, TheBuzzerSettingSurvivesARestart) {
  EXPECT_EQ(say("set buzzer off"), "OK");
  reboot();
  EXPECT_EQ(say("get buzzer"), "> off");
  EXPECT_EQ(say("set buzzer on"), "OK");
  reboot();
  EXPECT_EQ(say("get buzzer"), "> on");
}

TEST_F(CliTest, SetBuzzerRefusesAnythingButOnAndOffAndSavesNothing) {
  EXPECT_EQ(say("set buzzer off"), "OK");
  for (const char* v : {"", "ON", "OFF", "On", "1", "0", "true", "yes", "on ", "off ", " off", "offx", "o", "maybe"}) {
    CliResult r = run(std::string("set buzzer ") + v);
    EXPECT_EQ(r.text, "Error: state must be on or off") << "[" << v << "]";
    EXPECT_EQ(r.saves, 0) << "[" << v << "]";
    EXPECT_EQ(say("get buzzer"), "> off") << "[" << v << "]";   // unchanged
  }
}

TEST_F(CliTest, BuzzerCommandsNeedTheirExactSpelling) {
  EXPECT_EQ(say("set buzzer"), "Unknown command");   // the space after the key is part of the match
  EXPECT_EQ(say("set buzzerx on"), "Unknown command");
  EXPECT_EQ(say("get buzzer "), "Unknown command");
  EXPECT_EQ(say("get buzzerx"), "Unknown command");
  EXPECT_EQ(say("get buzz"), "Unknown command");
}

TEST_F(CliTest, BuzzerCommandsWorkWithAPrefixAndFromTheAppFrame) {
  EXPECT_EQ(say("xx|set buzzer off"), "xx|OK");
  EXPECT_EQ(say("xx|get buzzer"), "xx|> off");
  EXPECT_EQ(runFrame("get buzzer").text, "> off");
  CliResult f = runFrame("set buzzer on");
  EXPECT_EQ(f.text, "OK");
  EXPECT_EQ(f.saves, 1);
}

#endif   // PIN_BUZZER
