#include <gtest/gtest.h>
#include "helpers/RoutingPolicy.h"

using namespace mesh;

static Packet makeFlood(uint8_t route_type, uint8_t payload_type, uint8_t hops) {
    Packet p;
    p.header = route_type | (payload_type << PH_TYPE_SHIFT);
    p.setPathHashSizeAndCount(1, hops);
    p.payload_len = 1;
    if (route_type == ROUTE_TYPE_TRANSPORT_FLOOD) {
        p.transport_codes[0] = 1;  // has to be non-zero
    }
    return p;
}

TEST(FloodHopLimit, UnscopedFloodIsDroppedAtFirstHopWhenMaxUnscopedIsZero) {
    auto pkt = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&pkt, 64, 0, 8));
}

TEST(FloodHopLimit, ScopedFloodIsForwardedWhenMaxUnscopedIsZero) {
    for (uint8_t hops = 0; hops < 4; hops++) {
        auto pkt = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, hops);
        EXPECT_FALSE(isFloodHopLimitExceeded(&pkt, 64, 0, 8)) << "hops=" << (int)hops;
    }
}

TEST(FloodHopLimit, UnscopedFloodSurvivesUpToMaxUnscopedHops) {
    // matches the reported workaround: raising flood.max.unscoped to the expected hop count
    auto ok = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 2);
    EXPECT_FALSE(isFloodHopLimitExceeded(&ok, 64, 3, 8));

    auto too_far = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 3);
    EXPECT_TRUE(isFloodHopLimitExceeded(&too_far, 64, 3, 8));
}

TEST(FloodHopLimit, ScopedFloodStillHonoursFloodMaxAndAdvertMax) {
    auto beyond_max = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, 5);
    EXPECT_TRUE(isFloodHopLimitExceeded(&beyond_max, 5, 64, 8));

    auto advert = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 8);
    EXPECT_TRUE(isFloodHopLimitExceeded(&advert, 64, 64, 8));
}

// flood.max.unscoped=0 hits adverts too, well before flood_max_advert applies: a node
// still advertising un-scoped is invisible past its immediate neighbours
TEST(FloodHopLimit, UnscopedAdvertIsAlsoDroppedAtHopZero) {
    auto advert = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_ADVERT, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&advert, 64, 0, 8));

    auto scoped = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 0);
    EXPECT_FALSE(isFloodHopLimitExceeded(&scoped, 64, 0, 8));
}

TEST(ReplyRoute, FloodRequestGetsAPathReturn) {
    EXPECT_EQ(REPLY_ROUTE_PATH_RETURN,
              chooseReplyRoute(true, false, false));
    EXPECT_EQ(REPLY_ROUTE_PATH_RETURN,
              chooseReplyRoute(true, false, true));
}

TEST(ReplyRoute, DirectRequestWithSuppliedPathRepliesDirect) {
    EXPECT_EQ(REPLY_ROUTE_DIRECT_SUPPLIED, chooseReplyRoute(false, true, false));
}

// the reported bug: a DIRECT login (app already has a path) was answered by flooding, even
// with an out_path stored. Under flood.max.unscoped=0 that reply never arrives.
TEST(ReplyRoute, DirectRequestWithKnownOutPathRepliesDirect) {
    EXPECT_EQ(REPLY_ROUTE_DIRECT_OUT_PATH,
              chooseReplyRoute(false, false, true));
}

TEST(ReplyRoute, SuppliedPathWinsOverStoredOutPath) {
    EXPECT_EQ(REPLY_ROUTE_DIRECT_SUPPLIED, chooseReplyRoute(false, true, true));
}

TEST(ReplyRoute, DirectRequestWithNoReturnPathFallsBackToFlood) {
    EXPECT_EQ(REPLY_ROUTE_FLOOD, chooseReplyRoute(false, false, false));
}

TEST(ReplyScope, MirrorsTheRequestScopeWhenKnown) {
    EXPECT_EQ(REPLY_SCOPE_REQUEST, chooseReplyScope(true,
                                                    false,
                                                    false));
    EXPECT_EQ(REPLY_SCOPE_REQUEST, chooseReplyScope(true, false, true));
}

// un-scoped is itself a known scope, so mirror it. Replying scoped would change a path that
// works today, and repeaters not holding our default Region would drop it anyway.
TEST(ReplyScope, RepliesUnscopedToAnUnscopedFloodEvenWhenADefaultScopeExists) {
    EXPECT_EQ(REPLY_SCOPE_NONE, chooseReplyScope(false,
                                                 true,
                                                 true));
}

// second half of the bug: a DIRECT request carries no transport codes, so recv_pkt_region is
// always NULL. Un-scoped is dropped under flood.max.unscoped=0, and floods the mesh otherwise.
TEST(ReplyScope, FallsBackToDefaultScopeWhenRequestScopeUnknown) {
    EXPECT_EQ(REPLY_SCOPE_DEFAULT, chooseReplyScope(false,
                                                    false,
                                                    true));
}

TEST(ReplyScope, SendsUnscopedOnlyWhenNoScopeIsAvailableAtAll) {
    EXPECT_EQ(REPLY_SCOPE_NONE, chooseReplyScope(false, false, false));
    EXPECT_EQ(REPLY_SCOPE_NONE, chooseReplyScope(false, true, false));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(FloodMaxTypes, EmptyListChangesNothing) {
    auto unscoped = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&unscoped, 64, 0, 8, ""));
    EXPECT_TRUE(isFloodHopLimitExceeded(&unscoped, 64, 0, 8, NULL));

    auto scoped = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, 3);
    EXPECT_FALSE(isFloodHopLimitExceeded(&scoped, 64, 0, 8, ""));
    EXPECT_FALSE(isFloodHopLimitExceeded(&scoped, 64, 0, 8, NULL));

    auto advert = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 8);
    EXPECT_TRUE(isFloodHopLimitExceeded(&advert, 64, 64, 8, ""));

    auto capped = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_RESPONSE, 5);
    EXPECT_TRUE(isFloodHopLimitExceeded(&capped, 5, 64, 8, ""));
}

// the three argument form keeps compiling and behaving: that is what the sensor node and any
// caller not passing a list gets
TEST(FloodMaxTypes, ThreeArgumentCallStillWorks) {
    auto pkt = makeFlood(ROUTE_TYPE_FLOOD, PAYLOAD_TYPE_RESPONSE, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&pkt, 64, 0, 8));
}

// the FUOTA offer flood travels as GRP_DATA (0x06), scoped, so this is the knob for it
TEST(FloodMaxTypes, LimitsGroupDataWithoutTouchingOtherTypes) {
    auto grp_data_near = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_GRP_DATA, 3);
    EXPECT_FALSE(isFloodHopLimitExceeded(&grp_data_near, 64, 64, 8, "6:4"));

    auto grp_data_far = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_GRP_DATA, 4);
    EXPECT_TRUE(isFloodHopLimitExceeded(&grp_data_far, 64, 64, 8, "6:4"));

    auto txt_far = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, 20);
    EXPECT_FALSE(isFloodHopLimitExceeded(&txt_far, 64, 64, 8, "6:4"));
}

TEST(FloodMaxTypes, SeveralEntriesEachApplyToTheirOwnType) {
    const char* list = "2:10,6:4,9:3";
    auto txt = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, 10);
    EXPECT_TRUE(isFloodHopLimitExceeded(&txt, 64, 64, 8, list));
    auto trace = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TRACE, 3);
    EXPECT_TRUE(isFloodHopLimitExceeded(&trace, 64, 64, 8, list));
    auto ack = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ACK, 60);
    EXPECT_FALSE(isFloodHopLimitExceeded(&ack, 64, 64, 8, list));
}

// zero means: do not pass it on at all, as flood.max.unscoped=0 already does for un-scoped floods
TEST(FloodMaxTypes, ZeroStopsTheTypeAtTheFirstHop) {
    auto pkt = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TRACE, 0);
    EXPECT_TRUE(isFloodHopLimitExceeded(&pkt, 64, 64, 8, "9:0"));
}

// every limit that applies is tested, so the strictest wins: the same rule flood.max and
// flood.max.unscoped have always followed
TEST(FloodMaxTypes, StrictestLimitWins) {
    // list is stricter than flood.max
    auto a = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, 2);
    EXPECT_TRUE(isFloodHopLimitExceeded(&a, 64, 64, 8, "2:2"));
    // flood.max is stricter than the list
    auto b = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_TXT_MSG, 2);
    EXPECT_TRUE(isFloodHopLimitExceeded(&b, 2, 64, 8, "2:30"));
    // an advert entry joins flood.max.advert on the same terms: 3 wins over 8
    auto adv = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 3);
    EXPECT_TRUE(isFloodHopLimitExceeded(&adv, 64, 64, 8, "4:3"));
    // and 8 wins over 30
    auto adv2 = makeFlood(ROUTE_TYPE_TRANSPORT_FLOOD, PAYLOAD_TYPE_ADVERT, 8);
    EXPECT_TRUE(isFloodHopLimitExceeded(&adv2, 64, 64, 8, "4:30"));
}

TEST(FloodMaxTypes, LookupReportsNoEntryWhereTheListIsSilent) {
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType(NULL, PAYLOAD_TYPE_TRACE));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("", PAYLOAD_TYPE_TRACE));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:4", PAYLOAD_TYPE_TRACE));
    EXPECT_EQ(4, floodMaxForType("6:4", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(0, floodMaxForType("6:0", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(64, floodMaxForType("15:64", 15));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:4", 16));   // beyond the 4 bit type field
}

// a damaged list keeps what was read before the damage and drops the rest; it never applies a
// value it did not read, and it never runs off the end of the buffer
TEST(FloodMaxTypes, MalformedListStopsAtTheDamage) {
    EXPECT_EQ(4, floodMaxForType("6:4,xx", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:4,9", PAYLOAD_TYPE_TRACE));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType(":", PAYLOAD_TYPE_REQ));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType(",,,", PAYLOAD_TYPE_REQ));
    // out of range entries are skipped, not clamped
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("6:65", PAYLOAD_TYPE_GRP_DATA));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, floodMaxForType("99:4", PAYLOAD_TYPE_GRP_DATA));
    // and a skipped entry does not stop the ones behind it
    EXPECT_EQ(3, floodMaxForType("6:65,9:3", PAYLOAD_TYPE_TRACE));
}

TEST(FloodMaxTypes, SetInsertsChangesAndRemovesSortedByType) {
    char list[FLOOD_MAX_TYPES_LEN] = "";
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_TRACE, 3));
    EXPECT_STREQ("9:3", list);
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_GRP_DATA, 4));
    EXPECT_STREQ("6:4,9:3", list);                       // sorted by type, not by order of setting
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_GRP_DATA, 10));
    EXPECT_STREQ("6:10,9:3", list);                      // changed, not duplicated
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_GRP_DATA, FLOOD_MAX_TYPE_NONE));
    EXPECT_STREQ("9:3", list);                           // removed
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_TRACE, FLOOD_MAX_TYPE_NONE));
    EXPECT_STREQ("", list);                              // back to empty, ie. back to today's behaviour
}

TEST(FloodMaxTypes, SetRejectsOutOfRangeAndLeavesTheListAlone) {
    char list[FLOOD_MAX_TYPES_LEN] = "6:4";
    EXPECT_FALSE(setFloodMaxForType(list, sizeof(list), 16, 4));
    EXPECT_STREQ("6:4", list);
    EXPECT_FALSE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_TRACE, 65));
    EXPECT_STREQ("6:4", list);
    EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), PAYLOAD_TYPE_TRACE, 64));   // 64 is still allowed
    EXPECT_STREQ("6:4,9:64", list);
}

// all 16 slots filled is the worst case the buffer has to hold
TEST(FloodMaxTypes, BufferHoldsEverySlotAtItsLongest) {
    char list[FLOOD_MAX_TYPES_LEN] = "";
    for (uint8_t t = 0; t < FLOOD_MAX_TYPE_SLOTS; t++) {
        EXPECT_TRUE(setFloodMaxForType(list, sizeof(list), t, 64)) << "type=" << (int)t;
    }
    EXPECT_STREQ("0:64,1:64,2:64,3:64,4:64,5:64,6:64,7:64,8:64,9:64,10:64,11:64,12:64,13:64,14:64,15:64", list);
    EXPECT_EQ(85u, strlen(list));   // the worst case the buffer has to hold
    EXPECT_LT(strlen(list), sizeof(list));
    for (uint8_t t = 0; t < FLOOD_MAX_TYPE_SLOTS; t++) EXPECT_EQ(64, floodMaxForType(list, t));
}

// a buffer too small is refused, and what was there survives
TEST(FloodMaxTypes, SetRefusesWhenTheResultWouldNotFit) {
    char small[4] = "6:4";
    EXPECT_FALSE(setFloodMaxForType(small, sizeof(small), PAYLOAD_TYPE_TRACE, 3));
    EXPECT_STREQ("6:4", small);
}

TEST(FloodMaxTypes, TableRoundTripsThroughTheList) {
    uint8_t table[FLOOD_MAX_TYPE_SLOTS];
    floodMaxTypesToTable("2:10,6:4,9:3", table);
    EXPECT_EQ(10, table[PAYLOAD_TYPE_TXT_MSG]);
    EXPECT_EQ(4, table[PAYLOAD_TYPE_GRP_DATA]);
    EXPECT_EQ(3, table[PAYLOAD_TYPE_TRACE]);
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, table[PAYLOAD_TYPE_ACK]);

    char out[FLOOD_MAX_TYPES_LEN];
    EXPECT_TRUE(floodMaxTypesFromTable(table, out, sizeof(out)));
    EXPECT_STREQ("2:10,6:4,9:3", out);
}

// ---- what "set flood.max.type ..." and "get flood.max.type" do with their strings ----

TEST(FloodMaxTypeCmd, NamesCoverEveryAssignedPayloadType) {
    EXPECT_EQ(PAYLOAD_TYPE_REQ, floodMaxTypeByName("req"));
    EXPECT_EQ(PAYLOAD_TYPE_RESPONSE, floodMaxTypeByName("response"));
    EXPECT_EQ(PAYLOAD_TYPE_TXT_MSG, floodMaxTypeByName("txt"));
    EXPECT_EQ(PAYLOAD_TYPE_ACK, floodMaxTypeByName("ack"));
    EXPECT_EQ(PAYLOAD_TYPE_ADVERT, floodMaxTypeByName("advert"));
    EXPECT_EQ(PAYLOAD_TYPE_GRP_TXT, floodMaxTypeByName("grptxt"));
    EXPECT_EQ(PAYLOAD_TYPE_GRP_DATA, floodMaxTypeByName("grpdata"));
    EXPECT_EQ(PAYLOAD_TYPE_ANON_REQ, floodMaxTypeByName("anonreq"));
    EXPECT_EQ(PAYLOAD_TYPE_PATH, floodMaxTypeByName("path"));
    EXPECT_EQ(PAYLOAD_TYPE_TRACE, floodMaxTypeByName("trace"));
    EXPECT_EQ(PAYLOAD_TYPE_MULTIPART, floodMaxTypeByName("multipart"));
    EXPECT_EQ(PAYLOAD_TYPE_CONTROL, floodMaxTypeByName("control"));
    EXPECT_EQ(14, floodMaxTypeByName("fuota")) << "0x0E, which upstream leaves unassigned and the FUOTA branch uses";
    EXPECT_EQ(PAYLOAD_TYPE_RAW_CUSTOM, floodMaxTypeByName("raw"));
    // every name maps back to the slot it came from
    for (uint8_t t = 0; t < FLOOD_MAX_TYPE_SLOTS; t++) {
        const char* name = floodMaxTypeName(t);
        if (name != NULL) EXPECT_EQ(t, floodMaxTypeByName(name)) << name;
    }
    // 0x0C and 0x0D are not assigned in the protocol, so they have no name
    EXPECT_EQ(NULL, floodMaxTypeName(12));
    EXPECT_EQ(NULL, floodMaxTypeName(13));
    EXPECT_EQ(NULL, floodMaxTypeName(16));
}

// a plain number always works, so a payload type added later needs no firmware change
TEST(FloodMaxTypeCmd, PlainNumbersWorkAndNonsenseIsRefused) {
    EXPECT_EQ(0, floodMaxTypeByName("0"));
    EXPECT_EQ(12, floodMaxTypeByName("12"));
    EXPECT_EQ(15, floodMaxTypeByName("15"));
    EXPECT_EQ(-1, floodMaxTypeByName("16"));
    EXPECT_EQ(-1, floodMaxTypeByName("999"));
    EXPECT_EQ(-1, floodMaxTypeByName("4x"));
    EXPECT_EQ(-1, floodMaxTypeByName("Advert"));   // names are case sensitive, as the rest of the CLI
    EXPECT_EQ(-1, floodMaxTypeByName(""));
}

TEST(FloodMaxTypeCmd, ReadsTypeAndHops) {
    uint8_t type = 99, hops = 99;
    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("grpdata 4", &type, &hops));
    EXPECT_EQ(PAYLOAD_TYPE_GRP_DATA, type);
    EXPECT_EQ(4, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("9 0", &type, &hops));
    EXPECT_EQ(PAYLOAD_TYPE_TRACE, type);
    EXPECT_EQ(0, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("trace 64", &type, &hops));
    EXPECT_EQ(64, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("trace off", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_TYPE_NONE, hops);

    EXPECT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("  grpdata   4", &type, &hops));   // extra spaces
    EXPECT_EQ(PAYLOAD_TYPE_GRP_DATA, type);
    EXPECT_EQ(4, hops);
}

TEST(FloodMaxTypeCmd, RefusesBadArgumentsWithoutTouchingTheOutputs) {
    uint8_t type = 7, hops = 7;
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_TYPE, parseFloodMaxTypeCmd("nosuchtype 4", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_TYPE, parseFloodMaxTypeCmd("", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_TYPE, parseFloodMaxTypeCmd("16 4", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace 65", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace -1", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace four", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace 4x", &type, &hops));
    EXPECT_EQ(FLOOD_MAX_CMD_BAD_HOPS, parseFloodMaxTypeCmd("trace offf", &type, &hops));
    EXPECT_EQ(7, type);
    EXPECT_EQ(7, hops);
}

// the answer "get flood.max.type" gives, in the 160 byte reply buffer the CLI uses
TEST(FloodMaxTypeCmd, PrintsTheListTheWayGetAnswersIt) {
    char reply[160];
    formatFloodMaxTypes("", reply, sizeof(reply));
    EXPECT_STREQ("> none", reply);
    formatFloodMaxTypes(NULL, reply, sizeof(reply));
    EXPECT_STREQ("> none", reply);

    formatFloodMaxTypes("6:4", reply, sizeof(reply));
    EXPECT_STREQ("> grpdata:4", reply);

    formatFloodMaxTypes("2:10,6:4,9:3", reply, sizeof(reply));
    EXPECT_STREQ("> txt:10 grpdata:4 trace:3", reply);

    formatFloodMaxTypes("12:5", reply, sizeof(reply));   // a type with no name prints its number
    EXPECT_STREQ("> 12:5", reply);
}

// set, then get, with the same strings the CLI passes around
TEST(FloodMaxTypeCmd, SetThenGetRoundTrips) {
    char list[FLOOD_MAX_TYPES_LEN] = "";
    char reply[160];
    uint8_t type, hops;

    ASSERT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("grpdata 4", &type, &hops));
    ASSERT_TRUE(setFloodMaxForType(list, sizeof(list), type, hops));
    formatFloodMaxTypes(list, reply, sizeof(reply));
    EXPECT_STREQ("> grpdata:4", reply);

    ASSERT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("trace 3", &type, &hops));
    ASSERT_TRUE(setFloodMaxForType(list, sizeof(list), type, hops));
    formatFloodMaxTypes(list, reply, sizeof(reply));
    EXPECT_STREQ("> grpdata:4 trace:3", reply);

    ASSERT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("grpdata off", &type, &hops));
    ASSERT_TRUE(setFloodMaxForType(list, sizeof(list), type, hops));
    formatFloodMaxTypes(list, reply, sizeof(reply));
    EXPECT_STREQ("> trace:3", reply);

    ASSERT_EQ(FLOOD_MAX_CMD_OK, parseFloodMaxTypeCmd("trace off", &type, &hops));
    ASSERT_TRUE(setFloodMaxForType(list, sizeof(list), type, hops));
    formatFloodMaxTypes(list, reply, sizeof(reply));
    EXPECT_STREQ("> none", reply);
}

// all 16 entries with their names are longer than the reply buffer: it must end in " ..." and never
// write past cap
TEST(FloodMaxTypeCmd, LongListIsCutCleanlyInsideTheReplyBuffer) {
    char list[FLOOD_MAX_TYPES_LEN] = "";
    for (uint8_t t = 0; t < FLOOD_MAX_TYPE_SLOTS; t++) {
        ASSERT_TRUE(setFloodMaxForType(list, sizeof(list), t, 64));
    }
    char reply[160];
    memset(reply, 0x7F, sizeof(reply));
    formatFloodMaxTypes(list, reply, sizeof(reply));
    EXPECT_LT(strlen(reply), sizeof(reply));
    EXPECT_STREQ("> req:64 response:64 txt:64 ack:64 advert:64 grptxt:64 grpdata:64 anonreq:64 path:64"
                 " trace:64 multipart:64 control:64 12:64 13:64 fuota:64 raw:64", reply);

    // and in a buffer far too small the answer still ends inside it, even if the " ..." marker
    // itself is the thing that gets cut
    char small[12];
    memset(small, 0x7F, sizeof(small));
    formatFloodMaxTypes(list, small, sizeof(small));
    EXPECT_LT(strlen(small), sizeof(small));
    EXPECT_STREQ("> req:64 ..", small);
}
