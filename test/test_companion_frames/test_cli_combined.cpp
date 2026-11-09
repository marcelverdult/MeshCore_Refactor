// The companion's text CLI with all three optional blocks built in (PIN_BUZZER,
// ENABLE_WIFI_INTERFACE and DISPLAY_CLASS, as several real boards have them). Step
// 13d-a of docs/refactoring/0013-cli-command-table.md. What the single builds cannot
// show: the three blocks side by side in /prefs.json, and a command of one block next
// to the others after a restart.

#include "CliHarness.h"

#if defined(PIN_BUZZER) && defined(ENABLE_WIFI_INTERFACE) && defined(DISPLAY_CLASS)

using namespace companion;

namespace {
bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}
}  // namespace

TEST_F(CliTest, TheThreeBlocksShareOnePrefsFileInOrder) {
  say("set buzzer off");
  say("set wifi.ssid Net");
  say("set ui.show_clock 1");
  std::string f = prefsFile();
  size_t comp = f.find("comp:{");
  size_t wifi = f.find("wifi:{");
  size_t ui = f.find("ui:{");
  ASSERT_NE(comp, std::string::npos);
  ASSERT_NE(wifi, std::string::npos);
  ASSERT_NE(ui, std::string::npos);
  EXPECT_LT(comp, wifi);
  EXPECT_LT(wifi, ui);
  EXPECT_TRUE(contains(f, "buzz_q:1"));
  EXPECT_TRUE(contains(f, "ssid:\"Net\""));
  EXPECT_TRUE(contains(f, "show_clock:1"));
}

TEST_F(CliTest, EveryBlockSurvivesARestartTogether) {
  say("set name Combo");
  say("set pin 111111");
  say("set tz.offset 5.75");
  say("set buzzer off");
  say("set wifi.ssid Net");
  say("set wifi.pwd Pw");
  say("set wifi.enabled 0");
  say("set ui.auto_off_delay 99");
  say("set ui.nd_name 0");
  reboot();
  EXPECT_EQ(say("get name"), "> Combo");
  EXPECT_EQ(say("get tz.offset"), "> 5.75");
  EXPECT_EQ(say("get buzzer"), "> off");
  EXPECT_EQ(say("get wifi.ssid"), "> Net");
  EXPECT_EQ(say("get wifi.pwd"), "> Pw");
  EXPECT_EQ(say("get wifi.enabled"), "> 0");
  EXPECT_EQ(say("get ui.auto_off_delay"), "> 99 seconds");
  EXPECT_EQ(say("get ui.nd_name"), "> 0");
  EXPECT_TRUE(contains(prefsFile(), "pin:111111"));
}

TEST_F(CliTest, ACommandOfOneBlockLeavesTheOthersAlone) {
  say("set buzzer off");
  say("set wifi.clear");
  say("set ui.show_clock 1");
  EXPECT_EQ(say("get buzzer"), "> off");
  EXPECT_EQ(say("get wifi.enabled"), "> 1");
  EXPECT_EQ(say("get ui.show_clock"), "> 1");
}

#endif
