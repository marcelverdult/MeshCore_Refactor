// CommonCLI: "set flood.max.type <type> <hops|off>" and "get flood.max.type" (added with the
// per payload type hop limits). test_routing_policy pins the parser and the list functions on
// their own; this file pins the CLI path: reply texts, what ends up in prefs.flood_max_types,
// and when the prefs are saved.

#include <gtest/gtest.h>

#include "CliHarness.h"

static const char* SAVED = "savePrefs advert_interval=90";

class FloodTypeCliTest : public CliTest {
protected:
  std::string list() { return std::string(prefs.flood_max_types); }
};

TEST_F(FloodTypeCliTest, GetOfAnEmptyListSaysNone) {
  EXPECT_EQ(list(), "");
  EXPECT_EQ(run("get flood.max.type"), "> none");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(FloodTypeCliTest, SetByNameStoresTheTypeNumberAndSaves) {
  EXPECT_EQ(run("set flood.max.type grpdata 4"), "OK");
  EXPECT_EQ(list(), "6:4");
  EXPECT_EQ(takeCalls(), (Calls{SAVED}));
  EXPECT_EQ(run("get flood.max.type"), "> grpdata:4");
}

TEST_F(FloodTypeCliTest, SetKnowsEveryAssignedName) {
  const char* names[] = {"req", "response", "txt", "ack", "advert", "grptxt", "grpdata", "anonreq",
                         "path", "trace", "multipart", "control", nullptr, nullptr, "fuota", "raw"};
  for (int t = 0; t < 16; t++) {
    if (!names[t]) continue;
    resetPrefs();
    EXPECT_EQ(run(std::string("set flood.max.type ") + names[t] + " 7"), "OK") << names[t];
    EXPECT_EQ(list(), std::to_string(t) + ":7") << names[t];
  }
}

TEST_F(FloodTypeCliTest, SetByNumberWorksForUnnamedTypesToo) {
  EXPECT_EQ(run("set flood.max.type 12 3"), "OK");
  EXPECT_EQ(list(), "12:3");
  EXPECT_EQ(run("get flood.max.type"), "> 12:3");        // no name: printed as the number
  EXPECT_EQ(run("set flood.max.type 0 5"), "OK");
  EXPECT_EQ(list(), "0:5,12:3");                          // kept sorted by type
}

TEST_F(FloodTypeCliTest, TheListStaysSortedWithoutDuplicates) {
  EXPECT_EQ(run("set flood.max.type trace 3"), "OK");
  EXPECT_EQ(run("set flood.max.type grpdata 4"), "OK");
  EXPECT_EQ(list(), "6:4,9:3");
  EXPECT_EQ(run("set flood.max.type trace 8"), "OK");     // changes the entry in place
  EXPECT_EQ(list(), "6:4,9:8");
  EXPECT_EQ(run("get flood.max.type"), "> grpdata:4 trace:8");
}

TEST_F(FloodTypeCliTest, OffRemovesAnEntryAndSavesEvenWhenThereWasNone) {
  ASSERT_EQ(run("set flood.max.type grpdata 4"), "OK");
  takeCalls();
  EXPECT_EQ(run("set flood.max.type grpdata off"), "OK");
  EXPECT_EQ(list(), "");
  EXPECT_EQ(takeCalls(), (Calls{SAVED}));
  EXPECT_EQ(run("set flood.max.type trace off"), "OK");   // nothing to remove: still OK, still saved
  EXPECT_EQ(takeCalls(), (Calls{SAVED}));
}

TEST_F(FloodTypeCliTest, ZeroAndSixtyFourAreTheLimitsOfTheHopRange) {
  EXPECT_EQ(run("set flood.max.type txt 0"), "OK");
  EXPECT_EQ(run("set flood.max.type ack 64"), "OK");
  EXPECT_EQ(list(), "2:0,3:64");
  EXPECT_EQ(run("set flood.max.type ack 65"), "Error, hops 0..64 or off");
  EXPECT_EQ(list(), "2:0,3:64");
}

TEST_F(FloodTypeCliTest, BadHopsAnswerTheRangeErrorAndChangeNothing) {
  ASSERT_EQ(run("set flood.max.type grpdata 4"), "OK");
  takeCalls();
  for (const char* c : {"set flood.max.type grpdata", "set flood.max.type grpdata ",
                        "set flood.max.type grpdata abc", "set flood.max.type grpdata -1",
                        "set flood.max.type grpdata 4x", "set flood.max.type grpdata 4 5",
                        "set flood.max.type grpdata 999", "set flood.max.type grpdata OFF",
                        "set flood.max.type grpdata off "}) {
    EXPECT_EQ(run(c), "Error, hops 0..64 or off") << c;
  }
  EXPECT_EQ(list(), "6:4");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(FloodTypeCliTest, UnknownTypesAnswerTheTypeErrorAndChangeNothing) {
  for (const char* c : {"set flood.max.type bogus 3", "set flood.max.type 16 3", "set flood.max.type 99 3",
                        "set flood.max.type GrpData 4", "set flood.max.type grp 4",
                        "set flood.max.type -1 4", "set flood.max.type 1x 4",
                        "set flood.max.type  ", "set flood.max.type verylongwordthatdoesnotfit 4"}) {
    EXPECT_EQ(run(c), "Error, unknown packet type") << c;
  }
  EXPECT_EQ(list(), "");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(FloodTypeCliTest, TheTypeIsCheckedBeforeTheHops) {
  EXPECT_EQ(run("set flood.max.type bogus abc"), "Error, unknown packet type");
}

TEST_F(FloodTypeCliTest, ExtraSpacesAroundTheWordsAreSkipped) {
  EXPECT_EQ(run("set flood.max.type   grpdata    4"), "OK");
  EXPECT_EQ(list(), "6:4");
}

TEST_F(FloodTypeCliTest, TheCommandNeedsTheSpaceAfterTheKey) {
  EXPECT_EQ(run("set flood.max.type"), "unknown config: flood.max.type");
  EXPECT_EQ(list(), "");
}

TEST_F(FloodTypeCliTest, ItDoesNotTouchTheOtherFloodLimits) {
  EXPECT_EQ(run("set flood.max.type grpdata 4"), "OK");
  EXPECT_EQ(prefs.flood_max, 20);
  EXPECT_EQ(prefs.flood_max_unscoped, 21);
  EXPECT_EQ(prefs.flood_max_advert, 22);
  EXPECT_EQ(run("get flood.max"), "> 20");
  EXPECT_EQ(run("get flood.max.advert"), "> 22");
  EXPECT_EQ(run("get flood.max.unscoped"), "> 21");
}

TEST_F(FloodTypeCliTest, TheOldFloodMaxCommandsStillMatchTheirOwnKeys) {
  EXPECT_EQ(run("set flood.max.advert 5"), "OK");
  EXPECT_EQ(run("set flood.max.unscoped 6"), "OK");
  EXPECT_EQ(run("set flood.max 7"), "OK");
  EXPECT_EQ(list(), "");
  EXPECT_EQ(prefs.flood_max_advert, 5);
  EXPECT_EQ(prefs.flood_max_unscoped, 6);
  EXPECT_EQ(prefs.flood_max, 7);
}

TEST_F(FloodTypeCliTest, GetMatchesByPrefixPinnedAsIs) {
  ASSERT_EQ(run("set flood.max.type grpdata 4"), "OK");
  EXPECT_EQ(run("get flood.max.typefoo"), "> grpdata:4");
  EXPECT_EQ(run("get flood.max.type grpdata"), "> grpdata:4");   // arguments are ignored
}

TEST_F(FloodTypeCliTest, WorksOverTheAirLikeOnTheSerialConsole) {
  EXPECT_EQ(run("set flood.max.type grpdata 4", 1234), "OK");
  EXPECT_EQ(run("get flood.max.type", 1234), "> grpdata:4");
}

TEST_F(FloodTypeCliTest, ADamagedStoredListShowsTheEntriesReadBeforeTheDamage) {
  strcpy(prefs.flood_max_types, "6:4,x:1,9:3");
  EXPECT_EQ(run("get flood.max.type"), "> grpdata:4");
}

TEST_F(FloodTypeCliTest, SettingOnADamagedListDropsTheEntriesAfterTheDamagePinnedAsIs) {
  strcpy(prefs.flood_max_types, "6:4,x:1,9:3");
  EXPECT_EQ(run("set flood.max.type trace 8"), "OK");
  EXPECT_EQ(list(), "6:4,9:8");     // the 9:3 after the damage was lost; trace is the new entry
  strcpy(prefs.flood_max_types, "6:4,x:1,2:3");
  EXPECT_EQ(run("set flood.max.type trace 8"), "OK");
  EXPECT_EQ(list(), "6:4,9:8");     // 2:3 is gone
}

TEST_F(FloodTypeCliTest, AFullListFitsTheBufferAndTheReply) {
  for (int t = 0; t < 16; t++) {
    ASSERT_EQ(run("set flood.max.type " + std::to_string(t) + " 64"), "OK") << t;
  }
  EXPECT_EQ(list().size(), 85u);
  std::string r = run("get flood.max.type");
  EXPECT_EQ(r,
            "> req:64 response:64 txt:64 ack:64 advert:64 grptxt:64 grpdata:64 anonreq:64 path:64 "
            "trace:64 multipart:64 control:64 12:64 13:64 fuota:64 raw:64");
  EXPECT_LT(r.size(), 160u);
}

// "Error, type list full" cannot be reached: the buffer holds the longest possible list
// (see FLOOD_MAX_TYPES_LEN), so a set that passes the range checks always fits. Listed as a
// gap in docs/refactoring/0013-cli-command-table.md.
