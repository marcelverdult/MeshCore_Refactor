// The companion's text CLI, WiFi commands (built with ENABLE_WIFI_INTERFACE): set and
// get of wifi.ssid, wifi.pwd and wifi.enabled, set wifi.clear, get wifi.status and
// get wifi.ip. Step 13d-a of docs/refactoring/0013-cli-command-table.md. WiFi itself is
// the stand-in in test/mocks/WiFi.h. See test_cli_core.cpp for the commands every build
// has.

#include "CliHarness.h"

#ifdef ENABLE_WIFI_INTERFACE

using namespace companion;

namespace {
bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}
}  // namespace

// ---- defaults -------------------------------------------------------------------

TEST_F(CliTest, WifiStartsWithoutANetworkAndEnabled) {
  EXPECT_EQ(say("get wifi.ssid"), "> (not set)");
  EXPECT_EQ(say("get wifi.pwd"), "> ");
  EXPECT_EQ(say("get wifi.enabled"), "> 1");
}

// ---- wifi.ssid --------------------------------------------------------------------

TEST_F(CliTest, SetSsidStoresAndSaves) {
  CliResult r = run("set wifi.ssid HomeNet");
  EXPECT_EQ(r.text, "> wifi.ssid is now HomeNet (set wifi.pwd too, then reboot)");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get wifi.ssid"), "> HomeNet");
  EXPECT_TRUE(contains(prefsFile(), "ssid:\"HomeNet\""));
}

TEST_F(CliTest, TheSsidIsTheRestOfTheLine) {
  EXPECT_EQ(say("set wifi.ssid My Home  Net "), "> wifi.ssid is now My Home  Net  (set wifi.pwd too, then reboot)");
  EXPECT_EQ(say("get wifi.ssid"), "> My Home  Net ");   // pinned as is: nothing is trimmed
}

TEST_F(CliTest, ALongSsidIsCutAt32CharactersWithoutAnError) {
  std::string s33(33, 's');
  CliResult r = run("set wifi.ssid " + s33);
  EXPECT_EQ(r.text, "> wifi.ssid is now " + std::string(32, 's') + " (set wifi.pwd too, then reboot)");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get wifi.ssid"), "> " + std::string(32, 's'));
  std::string s150(150, 't');
  EXPECT_EQ(say("set wifi.ssid " + s150), "> wifi.ssid is now " + std::string(32, 't') + " (set wifi.pwd too, then reboot)");
  EXPECT_EQ(say("xx|get wifi.ssid"), "xx|> " + std::string(32, 't'));
}

TEST_F(CliTest, AnEmptySsidIsAcceptedAndReadsAsNotSet) {
  EXPECT_EQ(say("set wifi.ssid HomeNet"), "> wifi.ssid is now HomeNet (set wifi.pwd too, then reboot)");
  CliResult r = run("set wifi.ssid ");
  EXPECT_EQ(r.text, "> wifi.ssid is now  (set wifi.pwd too, then reboot)");   // pinned as is
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get wifi.ssid"), "> (not set)");
}

TEST_F(CliTest, TheSsidSurvivesARestart) {
  EXPECT_EQ(say("set wifi.ssid Kept"), "> wifi.ssid is now Kept (set wifi.pwd too, then reboot)");
  reboot();
  EXPECT_EQ(say("get wifi.ssid"), "> Kept");
}

// ---- wifi.pwd ---------------------------------------------------------------------

TEST_F(CliTest, SetPwdStoresAndSavesAndTheReplyHidesIt) {
  CliResult r = run("set wifi.pwd s3cret pass");
  EXPECT_EQ(r.text, "> wifi.pwd updated (reboot to apply)");
  EXPECT_EQ(r.saves, 1);
  EXPECT_TRUE(contains(prefsFile(), "pwd:\"s3cret pass\""));
}

TEST_F(CliTest, GetPwdAnswersThePasswordInClearText) {
  // pinned as is, and worth a second look: the password goes out over the app frame and
  // also to any contact that may use the remote CLI. Recorded in note 0013 under
  // "Not fixed".
  EXPECT_EQ(say("set wifi.pwd s3cret pass"), "> wifi.pwd updated (reboot to apply)");
  EXPECT_EQ(say("get wifi.pwd"), "> s3cret pass");
  EXPECT_EQ(runFrame("get wifi.pwd").text, "> s3cret pass");
}

TEST_F(CliTest, ALongPasswordIsCutAt63CharactersWithoutAnError) {
  std::string p64(64, 'p');
  EXPECT_EQ(say("set wifi.pwd " + p64), "> wifi.pwd updated (reboot to apply)");
  EXPECT_EQ(say("get wifi.pwd"), "> " + std::string(63, 'p'));
  std::string p150(150, 'q');
  EXPECT_EQ(say("set wifi.pwd " + p150), "> wifi.pwd updated (reboot to apply)");
  EXPECT_EQ(say("get wifi.pwd"), "> " + std::string(63, 'q'));
}

TEST_F(CliTest, AnEmptyPasswordIsAccepted) {
  EXPECT_EQ(say("set wifi.pwd abc"), "> wifi.pwd updated (reboot to apply)");
  CliResult r = run("set wifi.pwd ");
  EXPECT_EQ(r.text, "> wifi.pwd updated (reboot to apply)");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get wifi.pwd"), "> ");
}

TEST_F(CliTest, ThePasswordSurvivesARestart) {
  EXPECT_EQ(say("set wifi.pwd kept"), "> wifi.pwd updated (reboot to apply)");
  reboot();
  EXPECT_EQ(say("get wifi.pwd"), "> kept");
}

// ---- wifi.clear -------------------------------------------------------------------

TEST_F(CliTest, ClearEmptiesSsidAndPasswordAndSaves) {
  say("set wifi.ssid HomeNet");
  say("set wifi.pwd secret");
  CliResult r = run("set wifi.clear");
  EXPECT_EQ(r.text, "> wifi config cleared (reboot to apply)");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get wifi.ssid"), "> (not set)");
  EXPECT_EQ(say("get wifi.pwd"), "> ");
  reboot();
  EXPECT_EQ(say("get wifi.ssid"), "> (not set)");
}

TEST_F(CliTest, ClearLeavesTheEnabledFlagAlone) {
  say("set wifi.enabled 0");
  say("set wifi.clear");
  EXPECT_EQ(say("get wifi.enabled"), "> 0");
}

TEST_F(CliTest, ClearSavesEvenWhenThereWasNothingToClear) {
  EXPECT_EQ(run("set wifi.clear").saves, 1);
}

TEST_F(CliTest, ClearMatchesTheWholeCommand) {
  EXPECT_EQ(say("set wifi.clear "), "Unknown command");
  EXPECT_EQ(say("set wifi.clear now"), "Unknown command");
  EXPECT_EQ(say("set wifi.clearx"), "Unknown command");
}

// ---- wifi.enabled -----------------------------------------------------------------

TEST_F(CliTest, SetEnabledStoresZeroOrOneAndSaves) {
  CliResult off = run("set wifi.enabled 0");
  EXPECT_EQ(off.text, "> wifi.enabled is now 0 (reboot to apply)");
  EXPECT_EQ(off.saves, 1);
  EXPECT_EQ(say("get wifi.enabled"), "> 0");
  CliResult on = run("set wifi.enabled 1");
  EXPECT_EQ(on.text, "> wifi.enabled is now 1 (reboot to apply)");
  EXPECT_EQ(on.saves, 1);
  EXPECT_EQ(say("get wifi.enabled"), "> 1");
  EXPECT_TRUE(contains(prefsFile(), "enabled:1"));
}

TEST_F(CliTest, AnyNumberButZeroEnablesWifi) {
  // pinned as is: atoi() != 0 is 1, so -3 and 7 mean "on"; text and an empty value
  // mean "off"
  for (const char* v : {"2", "7", "-3", "100"}) {
    EXPECT_EQ(say(std::string("set wifi.enabled ") + v), "> wifi.enabled is now 1 (reboot to apply)") << v;
  }
  for (const char* v : {"abc", "off", "on", ""}) {
    EXPECT_EQ(say(std::string("set wifi.enabled ") + v), "> wifi.enabled is now 0 (reboot to apply)") << v;
  }
}

TEST_F(CliTest, TheEnabledFlagSurvivesARestart) {
  say("set wifi.enabled 0");
  reboot();
  EXPECT_EQ(say("get wifi.enabled"), "> 0");
}

// ---- wifi.status, wifi.ip ---------------------------------------------------------

TEST_F(CliTest, StatusIsConnectedOnlyForWlConnected) {
  WiFi.status_value = WL_CONNECTED;
  CliResult r = run("get wifi.status");
  EXPECT_EQ(r.text, "> connected");
  EXPECT_EQ(r.saves, 0);
  for (wl_status_t s : {WL_DISCONNECTED, WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_CONNECT_FAILED, WL_CONNECTION_LOST,
                        WL_NO_SHIELD}) {
    WiFi.status_value = s;
    EXPECT_EQ(say("get wifi.status"), "> disconnected") << (int)s;
  }
}

TEST_F(CliTest, IpIsTheAddressWhenConnectedAndNotConnectedOtherwise) {
  WiFi.address = IPAddress("192.168.1.50");
  WiFi.status_value = WL_CONNECTED;
  CliResult r = run("get wifi.ip");
  EXPECT_EQ(r.text, "> 192.168.1.50");
  EXPECT_EQ(r.saves, 0);
  EXPECT_EQ(say("xx|get wifi.ip"), "xx|> 192.168.1.50");
  WiFi.status_value = WL_DISCONNECTED;
  EXPECT_EQ(say("get wifi.ip"), "> (not connected)");   // the address is not shown without a connection
}

// ---- spelling, order, prefix ------------------------------------------------------

TEST_F(CliTest, WifiCommandsNeedTheirExactSpelling) {
  for (const char* c : {"set wifi.ssid", "set wifi.ssidx a", "set wifi.pwd", "set wifi.pwdx a", "set wifi.enabled",
                        "set wifi.enabledx 1", "get wifi.ssid ", "get wifi.ssidx", "get wifi.pwd ", "get wifi.enabled ",
                        "get wifi.status ", "get wifi.ip ", "get wifi.ipx", "get wifi", "set wifi", "get wifi.clear",
                        "set wifi.status 1", "set wifi.ip 1.2.3.4"}) {
    EXPECT_EQ(say(c), "Unknown command") << c;
  }
}

TEST_F(CliTest, WifiCommandsWorkWithAPrefixAndFromTheAppFrame) {
  EXPECT_EQ(say("xx|set wifi.ssid P"), "xx|> wifi.ssid is now P (set wifi.pwd too, then reboot)");
  EXPECT_EQ(say("xx|get wifi.enabled"), "xx|> 1");
  CliResult f = runFrame("set wifi.enabled 0");
  EXPECT_EQ(f.text, "> wifi.enabled is now 0 (reboot to apply)");
  EXPECT_EQ(f.saves, 1);
}

TEST_F(CliTest, TheWifiBlockIsPartOfThePrefsFile) {
  say("set wifi.ssid A");
  say("set wifi.pwd B");
  EXPECT_TRUE(contains(prefsFile(), "wifi:{ssid:\"A\",pwd:\"B\",enabled:1}"));
}

#endif   // ENABLE_WIFI_INTERFACE
