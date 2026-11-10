// CommonCLI: "sensor get|set|list" (all builds) and "gps ..." (only ENV_INCLUDE_GPS=1 builds).

#include <gtest/gtest.h>

#include "CliHarness.h"

class SensorCliTest : public CliTest {
protected:
  void addSettings(int n) {
    for (int i = 0; i < n; i++) {
      sens.settings.push_back({"var" + std::to_string(i), "v" + std::to_string(i)});
    }
  }
};

// ---- sensor get ------------------------------------------------------------------------

TEST_F(SensorCliTest, GetShowsTheValueOfAKnownSetting) {
  sens.settings = {{"gps", "1"}, {"name", "x y"}};
  EXPECT_EQ(run("sensor get gps"), "> 1");
  EXPECT_EQ(run("sensor get name"), "> x y");
  EXPECT_TRUE(takeCalls().empty());
  EXPECT_TRUE(sens.set_calls.empty());
}

TEST_F(SensorCliTest, GetOfAnUnknownOrEmptyKeyAnswersNull) {
  sens.settings = {{"gps", "1"}};
  EXPECT_EQ(run("sensor get nope"), "null");
  EXPECT_EQ(run("sensor get gp"), "null");              // whole key only, no prefix match
  EXPECT_EQ(run("sensor get "), "null");
}

TEST_F(SensorCliTest, GetNeedsTheSpaceAfterTheWordGet) {
  sens.settings = {{"gps", "1"}};
  EXPECT_EQ(run("sensor get"), "Unknown command");
}

// ---- sensor set ------------------------------------------------------------------------

TEST_F(SensorCliTest, SetPassesKeyAndValueToTheSensorManager) {
  sens.settings = {{"alpha", "1"}};
  EXPECT_EQ(run("sensor set alpha 42"), "ok");
  EXPECT_EQ(sens.set_calls, (std::vector<std::string>{"alpha=42"}));
  EXPECT_EQ(run("sensor get alpha"), "> 42");
}

TEST_F(SensorCliTest, SetWithoutAValueSendsTheTextNull) {
  EXPECT_EQ(run("sensor set alpha"), "ok");
  EXPECT_EQ(sens.set_calls, (std::vector<std::string>{"alpha=null"}));
}

TEST_F(SensorCliTest, SetWithoutAnythingSendsAnEmptyKey) {
  EXPECT_EQ(run("sensor set "), "ok");
  EXPECT_EQ(sens.set_calls, (std::vector<std::string>{"=null"}));
}

TEST_F(SensorCliTest, SetKeepsOnlyTheFirstWordOfTheValue) {
  EXPECT_EQ(run("sensor set alpha 1 2 3"), "ok");
  EXPECT_EQ(sens.set_calls, (std::vector<std::string>{"alpha=1"}));
}

TEST_F(SensorCliTest, SetTellsWhenTheSensorManagerRefuses) {
  sens.set_accepts = false;
  EXPECT_EQ(run("sensor set alpha 1"), "can't find custom var");
  EXPECT_EQ(sens.set_calls, (std::vector<std::string>{"alpha=1"}));
}

TEST_F(SensorCliTest, SetDoesNotTouchThePrefsOrSaveAnything) {
  sens.settings = {{"alpha", "1"}};
  EXPECT_EQ(run("sensor set alpha 2"), "ok");
  EXPECT_FALSE(prefs.isDirty());
  EXPECT_TRUE(takeCalls().empty());
}

// ---- sensor list -----------------------------------------------------------------------

TEST_F(SensorCliTest, ListWithoutSettingsSaysThereAreNone) {
  EXPECT_EQ(run("sensor list"), "no custom var");
}

TEST_F(SensorCliTest, ListShowsCountAndEveryNameValuePair) {
  sens.settings = {{"gps", "1"}, {"alpha", "x"}, {"beta", ""}};
  EXPECT_EQ(run("sensor list"), "3 vars\ngps=1\nalpha=x\nbeta=");
}

TEST_F(SensorCliTest, ListStartsAtTheGivenIndex) {
  addSettings(5);
  EXPECT_EQ(run("sensor list 3"), "5 vars\nvar3=v3\nvar4=v4");
  EXPECT_EQ(run("sensor list 4"), "5 vars\nvar4=v4");
}

TEST_F(SensorCliTest, ListBeyondTheEndSaysThereAreNone) {
  addSettings(2);
  EXPECT_EQ(run("sensor list 2"), "no custom var");
  EXPECT_EQ(run("sensor list 99"), "no custom var");
}

TEST_F(SensorCliTest, ListReadsTheIndexLeniently) {
  addSettings(3);
  EXPECT_EQ(run("sensor list 1abc"), "3 vars\nvar1=v1\nvar2=v2");      // digits up to the first other character
  EXPECT_EQ(run("sensor list x"), "3 vars\nvar0=v0\nvar1=v1\nvar2=v2");   // no digits: 0
  EXPECT_EQ(run("sensor list -1"), "3 vars\nvar0=v0\nvar1=v1\nvar2=v2");  // a minus sign is not a digit
}

TEST_F(SensorCliTest, ListWithOneCharacterAfterTheWordTakesItAsTheIndexPinnedAsIs) {
  addSettings(3);
  // "sensor listX": strlen > 11, so the number is read from command+12, which is past the X
  EXPECT_EQ(run("sensor listX"), "3 vars\nvar0=v0\nvar1=v1\nvar2=v2");
  EXPECT_EQ(run("sensor listX2"), "3 vars\nvar2=v2");
}

TEST_F(SensorCliTest, ListSplitsIntoPagesAndSaysWhereToContinue) {
  addSettings(40);
  std::string page1 = run("sensor list");
  ASSERT_GT(page1.size(), 0u);
  EXPECT_EQ(page1.substr(0, 17), "40 vars\nvar0=v0\nv");
  size_t pos = page1.rfind("... next:");
  ASSERT_NE(pos, std::string::npos);
  EXPECT_LT(page1.size(), 160u);
  int next = atoi(page1.c_str() + pos + 9);
  EXPECT_GT(next, 5);
  EXPECT_LT(next, 40);
  // the page ends with the last full line, then the pointer, with no line break in between
  EXPECT_EQ(page1[pos - 1], '\n');

  // following the pointer continues exactly where the page stopped
  std::string page2 = run("sensor list " + std::to_string(next));
  EXPECT_EQ(page2.substr(0, 8), "40 vars\n");
  EXPECT_EQ(page2.substr(8, 3 + std::to_string(next).size()), "var" + std::to_string(next));
}

TEST_F(SensorCliTest, ListWalksAllSettingsOnceAcrossPages) {
  addSettings(40);
  std::vector<std::string> seen;
  int at = 0, guard = 0;
  while (at < 40 && guard++ < 20) {
    std::string r = run("sensor list " + std::to_string(at));
    size_t pos = r.rfind("... next:");
    std::string body = r.substr(8, pos == std::string::npos ? std::string::npos : pos - 8);
    size_t s = 0;
    while (s < body.size()) {
      size_t e = body.find('\n', s);
      seen.push_back(body.substr(s, e == std::string::npos ? std::string::npos : e - s));
      if (e == std::string::npos) break;
      s = e + 1;
    }
    at = (pos == std::string::npos) ? 40 : atoi(r.c_str() + pos + 9);
  }
  ASSERT_EQ(seen.size(), 40u);
  for (int i = 0; i < 40; i++) {
    EXPECT_EQ(seen[i], "var" + std::to_string(i) + "=v" + std::to_string(i));
  }
}

TEST_F(SensorCliTest, ListCutsASingleOversizedEntryAndMovesOn) {
  sens.settings = {{"big", std::string(400, 'x')}, {"small", "1"}};
  std::string r = run("sensor list");
  EXPECT_EQ(r.substr(0, 13), "2 vars\nbig=xx");
  EXPECT_LT(r.size(), 160u);
  EXPECT_NE(r.find("... next:1"), std::string::npos);
  EXPECT_EQ(run("sensor list 1"), "2 vars\nsmall=1");
}

TEST_F(SensorCliTest, ListAnOversizedEntryThatIsTheLastOneHasNoNextPointer) {
  sens.settings = {{"big", std::string(400, 'x')}};
  std::string r = run("sensor list");
  EXPECT_EQ(r.find("next:"), std::string::npos);
  EXPECT_LT(r.size(), 160u);
}

// ---- gps ---------------------------------------------------------------------------------

#if ENV_INCLUDE_GPS == 1

class GpsCliTest : public SensorCliTest {
protected:
  void SetUp() override {
    SensorCliTest::SetUp();
    sens.location = &loc;
    sens.settings = {{"gps", "1"}};
  }
};

// "gps on|off|period|fix|once|guard ..." are CommonGpsPrefs::handleCommand, merged from upstream
// (#3753). The wording of its replies is upstream's; these tests pin only how CommonCLI wires it.
TEST_F(GpsCliTest, OnTurnsTheProviderOnAndSavesThePref) {
  prefs.gps_enabled = 0;
  EXPECT_EQ(run("gps on"), "OK");
  EXPECT_TRUE(loc.isGpsOn());
  EXPECT_EQ(prefs.gps_enabled, 1);
  EXPECT_EQ(takeCalls().size(), 1u);
}

TEST_F(GpsCliTest, OffTurnsTheProviderOffAndSavesThePref) {
  prefs.gps_enabled = 1;
  run("gps on");
  takeCalls();
  EXPECT_EQ(run("gps off"), "OK");
  EXPECT_FALSE(loc.isGpsOn());
  EXPECT_EQ(prefs.gps_enabled, 0);
  EXPECT_EQ(takeCalls().size(), 1u);
}

TEST_F(GpsCliTest, PeriodIsStoredInThePrefsAndSwitchesTheGpsOn) {
  EXPECT_EQ(run("gps period 10m"), "OK");
  EXPECT_EQ(prefs.gps_period, 600u);
  EXPECT_EQ(prefs.gps_enabled, 1);
  EXPECT_EQ(run("gps period"), "> 600");
}

TEST_F(GpsCliTest, StatusWithoutAProviderSaysSo) {
  sens.location = nullptr;
  EXPECT_EQ(run("gps"), "Error, gps provider not found");
}

TEST_F(GpsCliTest, AnUnknownGpsWordIsNotACommand) {
  EXPECT_EQ(run("gpsx"), "Unknown command");
}

TEST_F(GpsCliTest, SetlocCopiesTheSensorPositionToThePrefsAndSaves) {
  sens.node_lat = 48.25;
  sens.node_lon = 11.5;
  EXPECT_EQ(run("gps setloc"), "OK");
  EXPECT_EQ(prefs.node_lat, 48.25);
  EXPECT_EQ(prefs.node_lon, 11.5);
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90"}));
}

TEST_F(GpsCliTest, AdvertShowsThePolicy) {
  prefs.advert_loc_policy = ADVERT_LOC_NONE;  EXPECT_EQ(run("gps advert"), "> none");
  prefs.advert_loc_policy = ADVERT_LOC_PREFS; EXPECT_EQ(run("gps advert"), "> prefs");
  prefs.advert_loc_policy = ADVERT_LOC_SHARE; EXPECT_EQ(run("gps advert"), "> share");
  prefs.advert_loc_policy = 9;                EXPECT_EQ(run("gps advert"), "Error, invalid policy");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(GpsCliTest, AdvertSetsAndSavesEachPolicy) {
  EXPECT_EQ(run("gps advert none"), "OK");
  EXPECT_EQ(prefs.advert_loc_policy, ADVERT_LOC_NONE);
  EXPECT_EQ(run("gps advert share"), "OK");
  EXPECT_EQ(prefs.advert_loc_policy, ADVERT_LOC_SHARE);
  EXPECT_EQ(run("gps advert prefs"), "OK");
  EXPECT_EQ(prefs.advert_loc_policy, ADVERT_LOC_PREFS);
  EXPECT_EQ(takeCalls().size(), 3u);
}

TEST_F(GpsCliTest, AdvertWithAnUnknownPolicyAnswersErrorAndChangesNothing) {
  prefs.advert_loc_policy = ADVERT_LOC_SHARE;
  EXPECT_EQ(run("gps advert bogus"), "Error, invalid policy");
  EXPECT_EQ(prefs.advert_loc_policy, ADVERT_LOC_SHARE);
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(GpsCliTest, AdvertPolicyWordsMatchByPrefixPinnedAsIs) {
  EXPECT_EQ(run("gps advert nonexistent"), "OK");
  EXPECT_EQ(prefs.advert_loc_policy, ADVERT_LOC_NONE);
  EXPECT_EQ(run("gps advert shared!"), "OK");
  EXPECT_EQ(prefs.advert_loc_policy, ADVERT_LOC_SHARE);
}

TEST_F(GpsCliTest, AdvertWithTrailingSpaceOnlyIsAnErrorPinnedAsIs) {
  EXPECT_EQ(run("gps advert "), "Error, invalid policy");     // longer than 10: reads the policy word, finds none
}

#else

TEST_F(SensorCliTest, GpsSetlocAndAdvertDoNotExistInBuildsWithoutGps) {
  for (const char* c : {"gps setloc", "gps advert"}) {
    EXPECT_EQ(run(c), "Unknown command") << c;
  }
  EXPECT_TRUE(sens.set_calls.empty());
}

#endif
