// CommonCLI::handleRegionCmd(): the "region" commands, against the real RegionMap.
// The map itself is pinned by test/test_region_map; this file pins what the CLI says
// about it and what it asks the role to do (load, save, default region changed).

#include <gtest/gtest.h>

#include "CliHarness.h"

static const char* PUT_OK = "OK - (flood allowed)";

class RegionCliTest : public CliTest {
protected:
  // A small tree:  * -> Europe -> (Germany -> Berlin), France
  void buildTree() {
    ASSERT_EQ(run("region put Europe"), PUT_OK);
    ASSERT_EQ(run("region put Germany Europe"), PUT_OK);
    ASSERT_EQ(run("region put Berlin Germany"), PUT_OK);
    ASSERT_EQ(run("region put France Europe"), PUT_OK);
    ASSERT_TRUE(takeCalls().empty());
  }
};

// ---- listing -------------------------------------------------------------------------

TEST_F(RegionCliTest, RegionAloneListsTheTreeWithOneSpaceOfIndentPerLevel) {
  EXPECT_EQ(run("region"), "*^ F\n");                // the empty map: the wildcard, which is home
  buildTree();
  EXPECT_EQ(run("region"),
            "*^ F\n"
            " Europe F\n"
            "  Germany F\n"
            "   Berlin F\n"
            "  France F\n");
}

TEST_F(RegionCliTest, RegionListingShowsDeniedRegionsWithoutTheFlagAndMarksTheHome) {
  buildTree();
  EXPECT_EQ(run("region denyf France"), "OK");
  EXPECT_EQ(run("region home Germany"), " home is now Germany");
  EXPECT_EQ(run("region"),
            "* F\n"
            " Europe F\n"
            "  Germany^ F\n"
            "   Berlin F\n"
            "  France\n");
}

TEST_F(RegionCliTest, RegionListingIsCutToTheReplyBuffer) {
  for (int i = 0; i < 20; i++) {
    ASSERT_EQ(run("region put Region" + std::to_string(i)), PUT_OK);
  }
  std::string reply = run("region");
  EXPECT_EQ(reply.size(), 159u);                     // 160 bytes with the terminator
  EXPECT_EQ(reply.substr(0, 12), "*^ F\n Region");
}

// ---- put -----------------------------------------------------------------------------

TEST_F(RegionCliTest, PutCreatesARegionUnderTheWildcardOrUnderAParentGivenByPrefix) {
  EXPECT_EQ(run("region put Europe"), PUT_OK);
  EXPECT_EQ(run("region put Germany Eur"), PUT_OK);       // parent by prefix
  EXPECT_EQ(run("region put Nordic *"), PUT_OK);          // explicit wildcard
  EXPECT_EQ(run("region"), "*^ F\n Europe F\n  Germany F\n Nordic F\n");
  EXPECT_TRUE(takeCalls().empty());                       // not saved until "region save"
}

TEST_F(RegionCliTest, PutRefusesAnUnknownParentABadNameAndAParentThatIsItself) {
  EXPECT_EQ(run("region put Germany Nowhere"), "Err - unknown parent");
  EXPECT_EQ(run("region put Bad!Name"), "Err - unable to put");
  EXPECT_EQ(run("region put Europe"), PUT_OK);
  EXPECT_EQ(run("region put Europe Europe"), "Err - unable to put");
  EXPECT_EQ(run("region"), "*^ F\n Europe F\n");
}

TEST_F(RegionCliTest, PutOfAnExistingRegionMovesItAndAllowsFloodAgain) {
  buildTree();
  EXPECT_EQ(run("region denyf Berlin"), "OK");
  EXPECT_EQ(run("region put Berlin Europe"), PUT_OK);     // re-parented, and flags reset to allowed
  EXPECT_EQ(run("region"),
            "*^ F\n"
            " Europe F\n"
            "  Germany F\n"
            "  Berlin F\n"          // the list is in creation order, not grouped by parent
            "  France F\n");
}

TEST_F(RegionCliTest, PutIgnoresWordsAfterTheParent) {
  EXPECT_EQ(run("region put Europe"), PUT_OK);
  EXPECT_EQ(run("region put Germany Europe and more"), PUT_OK);
  EXPECT_EQ(run("region"), "*^ F\n Europe F\n  Germany F\n");
}

TEST_F(RegionCliTest, PutRefusesAThirtyThirdRegion) {
  for (int i = 0; i < 32; i++) {
    ASSERT_EQ(run("region put R" + std::to_string(i)), PUT_OK) << i;
  }
  EXPECT_EQ(run("region put R32"), "Err - unable to put");
  EXPECT_EQ(run("region put R0 R1"), PUT_OK);             // moving an existing one still works
}

// ---- remove ----------------------------------------------------------------------------

TEST_F(RegionCliTest, RemoveTakesALeafAndNeedsTheFullName) {
  buildTree();
  EXPECT_EQ(run("region remove Berlin"), "OK");
  EXPECT_EQ(run("region remove Berlin"), "Err - not found");
  EXPECT_EQ(run("region remove Fran"), "Err - not found");   // by exact name, not by prefix
  EXPECT_EQ(run("region remove #France"), "OK");             // a leading '#' is ignored
  EXPECT_EQ(run("region"), "*^ F\n Europe F\n  Germany F\n");
}

TEST_F(RegionCliTest, RemoveRefusesARegionWithChildrenAndTheWildcardPinnedAsIs) {
  buildTree();
  EXPECT_EQ(run("region remove Europe"), "Err - not empty");
  EXPECT_EQ(run("region remove *"), "Err - not empty");      // the wildcard cannot go: same message
  EXPECT_EQ(run("region"),
            "*^ F\n Europe F\n  Germany F\n   Berlin F\n  France F\n");
}

// ---- allowf, denyf, get ------------------------------------------------------------------

TEST_F(RegionCliTest, DenyfAndAllowfSwitchTheFloodFlagWithoutSaving) {
  buildTree();
  EXPECT_EQ(run("region denyf Germany"), "OK");
  EXPECT_EQ(run("region get Germany"), " Germany (Europe) ");     // denied: no F, and a trailing space
  EXPECT_EQ(run("region allowf Germany"), "OK");
  EXPECT_EQ(run("region get Germany"), " Germany (Europe) F");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(RegionCliTest, DenyfAndAllowfFindRegionsByPrefixAndKnowTheWildcard) {
  buildTree();
  EXPECT_EQ(run("region denyf Ger"), "OK");
  EXPECT_EQ(run("region get Germany"), " Germany (Europe) ");
  EXPECT_EQ(run("region denyf *"), "OK");
  EXPECT_EQ(run("region get *"), " * ");                          // the wildcard has no parent to show
  EXPECT_EQ(run("region allowf *"), "OK");
  EXPECT_EQ(run("region get *"), " * F");
}

TEST_F(RegionCliTest, DenyfAllowfAndGetRefuseAnUnknownRegion) {
  buildTree();
  EXPECT_EQ(run("region denyf Nowhere"), "Err - unknown region");
  EXPECT_EQ(run("region allowf Nowhere"), "Err - unknown region");
  EXPECT_EQ(run("region get Nowhere"), "Err - unknown region");
}

TEST_F(RegionCliTest, GetShowsTheParentUnlessItIsTheWildcard) {
  buildTree();
  EXPECT_EQ(run("region get Europe"), " Europe F");
  EXPECT_EQ(run("region get Berlin"), " Berlin (Germany) F");
  EXPECT_EQ(run("region get Berl"), " Berlin (Germany) F");      // by prefix
  EXPECT_EQ(run("region get #Berlin"), " Berlin (Germany) F");
}

TEST_F(RegionCliTest, GetOfAPrefixThatMatchesSeveralTakesTheLastOnePinnedAsIs) {
  ASSERT_EQ(run("region put Alpha"), PUT_OK);
  ASSERT_EQ(run("region put Alps"), PUT_OK);
  EXPECT_EQ(run("region get Al"), " Alps F");                    // the later of two partial matches
  EXPECT_EQ(run("region get Alpha"), " Alpha F");                // a full match wins
}

// ---- home ----------------------------------------------------------------------------------

TEST_F(RegionCliTest, HomeSetsAndShowsTheHomeRegion) {
  buildTree();
  EXPECT_EQ(run("region home"), " home is *");
  EXPECT_EQ(run("region home Germ"), " home is now Germany");
  EXPECT_EQ(run("region home"), " home is Germany");
  EXPECT_EQ(run("region home Nowhere"), "Err - unknown region");
  EXPECT_EQ(run("region home *"), " home is now *");
  EXPECT_EQ(run("region home"), " home is *");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(RegionCliTest, HomeFallsBackToTheWildcardWhenTheHomeRegionIsRemoved) {
  buildTree();
  ASSERT_EQ(run("region home Berlin"), " home is now Berlin");
  ASSERT_EQ(run("region remove Berlin"), "OK");
  EXPECT_EQ(run("region home"), " home is *");
}

// ---- default -----------------------------------------------------------------------------------

TEST_F(RegionCliTest, DefaultAloneShowsTheDefaultScope) {
  EXPECT_EQ(run("region default"), " default scope is <null>");
  buildTree();
  EXPECT_EQ(run("region default Germany"), " default scope is now Germany");
  EXPECT_EQ(run("region default"), " default scope is Germany");
}

TEST_F(RegionCliTest, DefaultSetsAnExistingRegionTellsTheRoleAndSavesTheRegions) {
  buildTree();
  EXPECT_EQ(run("region denyf Germany"), "OK");
  EXPECT_EQ(run("region default Ger"), " default scope is now Germany");   // by prefix
  EXPECT_EQ(takeCalls(), (Calls{"onDefaultRegionChanged Germany", "saveRegions"}));
  EXPECT_EQ(run("region get Germany"), " Germany (Europe) F");      // flood is allowed on the default
}

TEST_F(RegionCliTest, DefaultCreatesAnUnknownRegionUnderTheWildcard) {
  EXPECT_EQ(run("region default Hamburg"), " default scope is now Hamburg");
  EXPECT_EQ(takeCalls(), (Calls{"onDefaultRegionChanged Hamburg", "saveRegions"}));
  EXPECT_EQ(run("region"), "*^ F\n Hamburg F\n");
}

TEST_F(RegionCliTest, DefaultNullClearsTheDefaultScope) {
  ASSERT_EQ(run("region default Hamburg"), " default scope is now Hamburg");
  takeCalls();
  EXPECT_EQ(run("region default <null>"), " default scope is now <null>");
  EXPECT_EQ(takeCalls(), (Calls{"onDefaultRegionChanged <null>", "saveRegions"}));
  EXPECT_EQ(run("region default"), " default scope is <null>");
}

TEST_F(RegionCliTest, DefaultWithABadNameSaysTheTableIsFullPinnedAsIs) {
  EXPECT_EQ(run("region default Bad!Name"), "Err - region table full");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(RegionCliTest, DefaultAnswersOkWhateverSaveRegionsReturnedPinnedAsIs) {
  cb.save_regions_ok = false;
  EXPECT_EQ(run("region default Hamburg"), " default scope is now Hamburg");
}

// ---- load, save, list ------------------------------------------------------------------------------

TEST_F(RegionCliTest, LoadStartsTheLoadModeOfTheRoleAndRepliesNothing) {
  EXPECT_EQ(run("region load"), "");
  EXPECT_EQ(takeCalls(), (Calls{"startRegionsLoad"}));
}

TEST_F(RegionCliTest, SaveStampsTheDiscoveryTimeSavesThePrefsThenTheRegions) {
  EXPECT_EQ(run("region save"), "OK");
  EXPECT_EQ(prefs.discovery_mod_timestamp, clk.now);
  EXPECT_EQ(takeCalls(), (Calls{"savePrefs advert_interval=90", "saveRegions"}));
  cb.save_regions_ok = false;
  EXPECT_EQ(run("region save"), "Err - save failed");
}

TEST_F(RegionCliTest, ListShowsTheNamesThatAllowOrDenyFlood) {
  buildTree();
  EXPECT_EQ(run("region list allowed"), "*,Europe,Germany,Berlin,France");
  EXPECT_EQ(run("region list denied"), "-none-");
  EXPECT_EQ(run("region denyf Germany"), "OK");
  EXPECT_EQ(run("region denyf *"), "OK");
  EXPECT_EQ(run("region list allowed"), "Europe,Berlin,France");
  EXPECT_EQ(run("region list denied"), "*,Germany");
}

TEST_F(RegionCliTest, ListRefusesAnythingButAllowedOrDenied) {
  EXPECT_EQ(run("region list everything"), "Err - use 'allowed' or 'denied'");
  EXPECT_EQ(run("region list"), "Err - ??");                 // the argument is missing
}

// ---- def (the one line form) ---------------------------------------------------------------------------

TEST_F(RegionCliTest, DefBuildsAChainWhereEachNameIsTheParentOfTheNext) {
  EXPECT_EQ(run("region def A B C"),
            "*^ F\n A F\n  B F\n   C F\n");
}

TEST_F(RegionCliTest, DefJumpMovesTheParentToAnotherRegion) {
  // "name|jump": create name under the current parent, then continue under jump
  EXPECT_EQ(run("region def A|* B"), "*^ F\n A F\n B F\n");
  EXPECT_EQ(run("region def C,A D"), "*^ F\n A F\n  D F\n B F\n C F\n");      // ',' works like '|'
}

TEST_F(RegionCliTest, DefAllowsFloodOnWhatItTouches) {
  ASSERT_EQ(run("region put X"), PUT_OK);
  ASSERT_EQ(run("region denyf X"), "OK");
  EXPECT_EQ(run("region def X"), "*^ F\n X F\n");
}

TEST_F(RegionCliTest, DefRefusesBadInputAndKeepsWhatWasDoneBeforeTheErrorPinnedAsIs) {
  EXPECT_EQ(run("region def"), "Err - empty def");
  EXPECT_EQ(run("region def   "), "Err - empty def");
  EXPECT_EQ(run("region def |A"), "Err - empty name");
  EXPECT_EQ(run("region def A|"), "Err - empty jump");
  EXPECT_EQ(run("region def A|Nowhere"), "Err - unknown jump: Nowhere");
  EXPECT_EQ(run("region def Bad!Name"), "Err - put failed: Bad!Name");
  // no rollback: the A that came before the bad token stays
  EXPECT_EQ(run("region def Good Bad!Name"), "Err - put failed: Bad!Name");
  EXPECT_EQ(run("region"), "*^ F\n A F\n Good F\n");
}

TEST_F(RegionCliTest, DefIsOnlyTheWordDefNotAWordThatStartsWithIt) {
  EXPECT_EQ(run("region definitely"), "Err - ??");
}

// ---- everything else ---------------------------------------------------------------------------------------

TEST_F(RegionCliTest, UnknownAndIncompleteRegionCommandsAnswerErrQuestionMarks) {
  buildTree();
  for (const char* c : {"region foo", "region put", "region remove", "region allowf", "region denyf",
                        "region get", "region  put X", "region load2", "region Save"}) {
    EXPECT_EQ(run(c), "Err - ??") << c;
  }
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(RegionCliTest, SubcommandsMatchWholeWordsUnlikeTheTopLevelCommands) {
  EXPECT_EQ(run("region saved"), "Err - ??");
  EXPECT_EQ(run("region loader"), "Err - ??");
  EXPECT_EQ(run("region homes"), "Err - ??");
  EXPECT_TRUE(takeCalls().empty());
}
