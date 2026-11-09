// The companion's text CLI, display commands (built with DISPLAY_CLASS): set and get of
// the nine ui.* settings. Step 13d-a of docs/refactoring/0013-cli-command-table.md. See
// test_cli_core.cpp for the commands every build has.
//
// The defaults below are the ones NodePrefs.h gives a build that sets none of
// UI_SHOW_CLOCK, AUTO_OFF_MILLIS, BACKLIGHT_BTN, UI_SCREENOFF_BLANK and
// EINK_MAX_PARTIAL_REFRESH, which is how this environment is built. A real variant may
// set them, and then starts with other values; the commands do not change.

#include "CliHarness.h"

#ifdef DISPLAY_CLASS

using namespace companion;

namespace {

bool contains(const std::string& text, const std::string& part) {
  return text.find(part) != std::string::npos;
}

struct UiKey {
  const char* key;       // the command word after "set ui." and "get ui."
  const char* json;      // its key in the "ui" block of /prefs.json
  const char* dflt;      // what `get` answers on a fresh node
  const char* set_after_zero;   // what `set ui.<key>` with no value answers
  const char* get_after_zero;   // what `get` answers after that
};

const UiKey UI_KEYS[] = {
  {"show_clock",         "show_clock",    "> 0",            "> now 0",                                 "> 0"},
  {"auto_off_delay",     "auto_off",      "> 15 seconds",   "> now 0 seconds",                         "> 0 seconds"},
  {"backlight_delay",    "bl_dly",        "> 5 seconds",    "> now 0 seconds",                         "> 0 seconds"},
  {"backlight_on_keys",  "bl_keys",       "> 1",            "> now 0",                                 "> 0"},
  {"backlight_on_msgs",  "bl_msgs",       "> 0",            "> now 0",                                 "> 0"},
  {"auto_off_blank",     "off_blank",     "> 0",            "> now 0",                                 "> 0"},
  {"auto_off_keep_msgs", "off_keep_msgs", "> 1",            "> now 0",                                 "> 0"},
  {"max_partial",        "max_partial",   "> 0",            "> now 0 (applied at next screen wakeup)", "> 0"},
  {"nd_name",            "nd_name",       "> 1",            "> ui.nd_name is now 0",                   "> 0"},
};

}  // namespace

// ---- defaults and the plain case ------------------------------------------------

TEST_F(CliTest, TheUiSettingsStartAtTheirDefaults) {
  for (const UiKey& k : UI_KEYS) {
    CliResult r = run(std::string("get ui.") + k.key);
    EXPECT_EQ(r.text, k.dflt) << k.key;
    EXPECT_EQ(r.saves, 0) << k.key;
  }
}

TEST_F(CliTest, ShowClockNeedsAnyNumberButZeroForOn) {
  CliResult r = run("set ui.show_clock 1");
  EXPECT_EQ(r.text, "> now 1");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get ui.show_clock"), "> 1");
  EXPECT_EQ(say("set ui.show_clock 7"), "> now 1");      // pinned as is: stored as 1
  EXPECT_EQ(say("set ui.show_clock 300"), "> now 1");
  EXPECT_EQ(say("set ui.show_clock -2"), "> now 1");
  EXPECT_EQ(say("set ui.show_clock 0"), "> now 0");
  EXPECT_EQ(say("get ui.show_clock"), "> 0");
  EXPECT_TRUE(contains(prefsFile(), "show_clock:0"));
}

TEST_F(CliTest, AutoOffDelayIsInSeconds) {
  CliResult r = run("set ui.auto_off_delay 30");
  EXPECT_EQ(r.text, "> now 30 seconds");
  EXPECT_EQ(r.saves, 1);
  EXPECT_EQ(say("get ui.auto_off_delay"), "> 30 seconds");
  EXPECT_TRUE(contains(prefsFile(), "auto_off:30"));
  EXPECT_EQ(say("set ui.auto_off_delay 100000"), "> now 100000 seconds");   // no upper limit
}

TEST_F(CliTest, ANegativeAutoOffDelayIsStoredAsAnUnsignedNumber) {
  // pinned as is: the field is unsigned, the reply prints it as a signed int
  EXPECT_EQ(say("set ui.auto_off_delay -1"), "> now -1 seconds");
  EXPECT_EQ(say("get ui.auto_off_delay"), "> -1 seconds");
  EXPECT_TRUE(contains(prefsFile(), "auto_off:4294967295"));
}

TEST_F(CliTest, BacklightDelayHasThreeKindsOfAnswer) {
  CliResult s = run("set ui.backlight_delay 12");
  EXPECT_EQ(s.text, "> now 12 seconds");
  EXPECT_EQ(s.saves, 1);
  EXPECT_EQ(say("get ui.backlight_delay"), "> 12 seconds");
  EXPECT_EQ(say("set ui.backlight_delay 0"), "> now 0 seconds");
  EXPECT_EQ(say("get ui.backlight_delay"), "> 0 seconds");
  EXPECT_EQ(say("set ui.backlight_delay -1"), "> now always on (-1)");
  EXPECT_EQ(say("get ui.backlight_delay"), "> always on (-1)");
  EXPECT_EQ(say("set ui.backlight_delay -2"), "> now disabled (<-1)");
  EXPECT_EQ(say("get ui.backlight_delay"), "> disabled (<-1)");
  EXPECT_EQ(say("set ui.backlight_delay -500"), "> now disabled (<-1)");
  EXPECT_EQ(say("get ui.backlight_delay"), "> disabled (<-1)");
  EXPECT_TRUE(contains(prefsFile(), "bl_dly:-500"));   // the value itself is kept
}

TEST_F(CliTest, TheByteSizedSettingsKeepTheLowByteOfTheNumber) {
  // pinned as is: atoi() into a uint8_t, no range check, so 300 reads back as 44 and -1 as 255
  struct Row { const char* key; const char* reply_fmt; };
  const char* keys[] = {"backlight_on_keys", "backlight_on_msgs", "auto_off_blank", "auto_off_keep_msgs",
                        "max_partial", "nd_name"};
  for (const char* key : keys) {
    std::string base = std::string("ui.") + key;
    EXPECT_TRUE(contains(say("set " + base + " 300"), " 44")) << key;
    EXPECT_EQ(say("get " + base), "> 44") << key;
    EXPECT_TRUE(contains(say("set " + base + " -1"), " 255")) << key;
    EXPECT_EQ(say("get " + base), "> 255") << key;
    EXPECT_TRUE(contains(say("set " + base + " 256"), " 0")) << key;
    EXPECT_EQ(say("get " + base), "> 0") << key;
    EXPECT_TRUE(contains(say("set " + base + " 7"), " 7")) << key;
    EXPECT_EQ(say("get " + base), "> 7") << key;
  }
}

TEST_F(CliTest, EachSetAnswersInItsOwnWords) {
  EXPECT_EQ(say("set ui.backlight_on_keys 2"), "> now 2");
  EXPECT_EQ(say("set ui.backlight_on_msgs 3"), "> now 3");
  EXPECT_EQ(say("set ui.auto_off_blank 4"), "> now 4");
  EXPECT_EQ(say("set ui.auto_off_keep_msgs 5"), "> now 5");
  EXPECT_EQ(say("set ui.max_partial 6"), "> now 6 (applied at next screen wakeup)");
  EXPECT_EQ(say("set ui.nd_name 8"), "> ui.nd_name is now 8");
}

TEST_F(CliTest, EachSetSavesOnceAndEachGetSavesNothing) {
  for (const UiKey& k : UI_KEYS) {
    EXPECT_EQ(run(std::string("set ui.") + k.key + " 2").saves, 1) << k.key;
    EXPECT_EQ(run(std::string("get ui.") + k.key).saves, 0) << k.key;
  }
}

TEST_F(CliTest, ASettingChangeDoesNotTouchTheOthers) {
  say("set ui.show_clock 1");
  say("set ui.auto_off_delay 21");
  say("set ui.backlight_delay 22");
  say("set ui.backlight_on_keys 23");
  say("set ui.backlight_on_msgs 24");
  say("set ui.auto_off_blank 25");
  say("set ui.auto_off_keep_msgs 26");
  say("set ui.max_partial 27");
  say("set ui.nd_name 28");
  EXPECT_EQ(say("get ui.show_clock"), "> 1");
  EXPECT_EQ(say("get ui.auto_off_delay"), "> 21 seconds");
  EXPECT_EQ(say("get ui.backlight_delay"), "> 22 seconds");
  EXPECT_EQ(say("get ui.backlight_on_keys"), "> 23");
  EXPECT_EQ(say("get ui.backlight_on_msgs"), "> 24");
  EXPECT_EQ(say("get ui.auto_off_blank"), "> 25");
  EXPECT_EQ(say("get ui.auto_off_keep_msgs"), "> 26");
  EXPECT_EQ(say("get ui.max_partial"), "> 27");
  EXPECT_EQ(say("get ui.nd_name"), "> 28");
}

TEST_F(CliTest, TheUiSettingsSurviveARestartAndAreTheUiBlockOfThePrefsFile) {
  say("set ui.show_clock 1");
  say("set ui.auto_off_delay 21");
  say("set ui.backlight_delay -1");
  say("set ui.backlight_on_keys 0");
  say("set ui.backlight_on_msgs 1");
  say("set ui.auto_off_blank 1");
  say("set ui.auto_off_keep_msgs 0");
  say("set ui.max_partial 9");
  say("set ui.nd_name 0");
  EXPECT_TRUE(contains(prefsFile(),
      "ui:{show_clock:1,auto_off:21,bl_dly:-1,bl_keys:0,bl_msgs:1,max_partial:9,off_blank:1,off_keep_msgs:0,nd_name:0}"));
  reboot();
  EXPECT_EQ(say("get ui.show_clock"), "> 1");
  EXPECT_EQ(say("get ui.auto_off_delay"), "> 21 seconds");
  EXPECT_EQ(say("get ui.backlight_delay"), "> always on (-1)");
  EXPECT_EQ(say("get ui.backlight_on_keys"), "> 0");
  EXPECT_EQ(say("get ui.backlight_on_msgs"), "> 1");
  EXPECT_EQ(say("get ui.auto_off_blank"), "> 1");
  EXPECT_EQ(say("get ui.auto_off_keep_msgs"), "> 0");
  EXPECT_EQ(say("get ui.max_partial"), "> 9");
  EXPECT_EQ(say("get ui.nd_name"), "> 0");
}

// ---- pinned as is: the key is matched as a prefix, with no end of word -------------

TEST_F(CliTest, SetWithoutAValueStoresZeroAndSaves) {
  for (const UiKey& k : UI_KEYS) {
    say(std::string("set ui.") + k.key + " 5");
    CliResult r = run(std::string("set ui.") + k.key);
    EXPECT_EQ(r.text, k.set_after_zero) << k.key;
    EXPECT_EQ(r.saves, 1) << k.key;
    EXPECT_EQ(say(std::string("get ui.") + k.key), k.get_after_zero) << k.key;
  }
}

TEST_F(CliTest, TheValueMayFollowTheKeyDirectlyWithNoSpace) {
  EXPECT_EQ(say("set ui.show_clock1"), "> now 1");
  EXPECT_EQ(say("get ui.show_clock"), "> 1");
  EXPECT_EQ(say("set ui.auto_off_delay45"), "> now 45 seconds");
  EXPECT_EQ(say("get ui.auto_off_delay"), "> 45 seconds");
  EXPECT_EQ(say("set ui.nd_name0"), "> ui.nd_name is now 0");
  EXPECT_EQ(say("get ui.nd_name"), "> 0");
}

TEST_F(CliTest, AnyTextAfterTheKeyIsReadAsAValueOrAsZero) {
  EXPECT_EQ(say("set ui.show_clock1"), "> now 1");
  EXPECT_EQ(say("set ui.show_clockabc"), "> now 0");
  EXPECT_EQ(say("set ui.show_clock 1 2 3"), "> now 1");
  EXPECT_EQ(say("set ui.auto_off_delayXYZ"), "> now 0 seconds");
  EXPECT_EQ(say("set ui.max_partial_please 4"), "> now 0 (applied at next screen wakeup)");
}

TEST_F(CliTest, GetIgnoresWhatFollowsTheKey) {
  for (const UiKey& k : UI_KEYS) {
    std::string plain = say(std::string("get ui.") + k.key);
    EXPECT_EQ(say(std::string("get ui.") + k.key + "XYZ"), plain) << k.key;
    EXPECT_EQ(say(std::string("get ui.") + k.key + " 9"), plain) << k.key;
    EXPECT_EQ(say(std::string("get ui.") + k.key + "s"), plain) << k.key;
  }
}

TEST_F(CliTest, NoUiKeyIsAPrefixOfAnotherSoTheOrderOfTheBranchesDoesNotShow) {
  // 13d-b must still match by prefix, but no two keys compete for the same text
  for (const UiKey& a : UI_KEYS) {
    for (const UiKey& b : UI_KEYS) {
      if (&a == &b) continue;
      EXPECT_NE(std::string(b.key).rfind(a.key, 0), 0u) << a.key << " is a prefix of " << b.key;
    }
  }
}

TEST_F(CliTest, UiCommandsDoNotMatchBelowTheirFullKey) {
  for (const char* c : {"set ui.", "set ui", "set ui.show", "set ui.auto_off", "set ui.backlight", "set ui.nd",
                        "get ui.", "get ui", "get ui.show", "get ui.auto_off", "get ui.backlight", "get ui.nd",
                        "get ui.unknown", "set ui.unknown 1", "set UI.show_clock 1", "get UI.show_clock", "get  ui.show_clock",
                        "ui.show_clock"}) {
    EXPECT_EQ(say(c), "Unknown command") << c;
  }
}

TEST_F(CliTest, UiCommandsWorkWithAPrefixAndFromTheAppFrame) {
  EXPECT_EQ(say("xx|set ui.backlight_delay 9"), "xx|> now 9 seconds");
  EXPECT_EQ(say("xx|get ui.backlight_delay"), "xx|> 9 seconds");
  CliResult f = runFrame("set ui.max_partial 3");
  EXPECT_EQ(f.text, "> now 3 (applied at next screen wakeup)");
  EXPECT_EQ(f.saves, 1);
  EXPECT_EQ(runFrame("get ui.max_partial").text, "> 3");
}

#endif   // DISPLAY_CLASS
