// Characterization tests for the server underpinnings the three roles carry in
// triplicate (see docs/refactoring/0004-shared-server-base.md). They run the real
// role implementation on the host against the mocks in test/mocks, so the recorded
// behaviour is the firmware's, not a copy of its formulas.
//
// This file pins the temporary radio parameters, which step 4c moves into the
// shared base: what `set radio ...` schedules, when the pending values reach the
// radio, that they reach it once, and when the node reverts to its own settings.
// It also pins the Region a packet is taken to have arrived on, which step 4a
// already moved into ServerMesh::onRecvPacket().
// The room server stands in for the three roles here; the repeater and the sensor
// need an environment of their own, because both of them also define `MyMesh`.

#include <gtest/gtest.h>

#include <Arduino.h>
#include <target.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>

#include "MyMesh.h"

namespace {

// The delay the role gives the CLI to answer before the pending values are applied.
constexpr uint32_t APPLY_DELAY_MS = 2000;

// The clock the role starts from in these tests.
constexpr uint32_t T0 = 1000;

// Dispatcher::millisHasNowPassed() is `(long)(now - timestamp) > 0`, so a timer set
// for T fires at T + 1, not at T. That one millisecond is pinned deliberately: it is
// the observable edge of every scheduled action in the roles, and the signed
// difference is what makes the comparison survive the millis() rollover.

class ServerBaseTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;

  // Built the way the role's own main.cpp builds it.
  std::unique_ptr<MyMesh> mesh;

  float orig_freq = 0, orig_bw = 0;
  uint8_t orig_sf = 0, orig_cr = 0;

  void SetUp() override {
    g_mock_millis = T0;   // not 0, so a scheduled time is distinguishable from "off"
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);

    mesh.reset(new MyMesh(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);

    // begin() pushes the node's own settings to the radio; remember them as the
    // values a revert has to restore, without naming the defaults here.
    orig_freq = radio_driver.freq;
    orig_bw = radio_driver.bw;
    orig_sf = radio_driver.sf;
    orig_cr = radio_driver.cr;
    radio_driver.set_params_calls = 0;
  }
};

TEST_F(ServerBaseTest, BeginPushesTheNodesOwnSettingsToTheRadio) {
  EXPECT_GT(orig_freq, 0.0f);
  EXPECT_GT(orig_bw, 0.0f);
  EXPECT_GT(orig_sf, 0);
}

TEST_F(ServerBaseTest, TempParamsAreNotAppliedBeforeTheReplyDelay) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 3);

  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, 0);   // still inside the delay
  EXPECT_FLOAT_EQ(radio_driver.freq, orig_freq);

  // The scheduled moment itself is still too early.
  g_mock_millis = T0 + APPLY_DELAY_MS;
  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, 0);
  EXPECT_FLOAT_EQ(radio_driver.freq, orig_freq);
}

TEST_F(ServerBaseTest, TempParamsReachTheRadioOneMillisecondAfterTheDelay) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 3);

  g_mock_millis = T0 + APPLY_DELAY_MS + 1;
  mesh->loop();

  EXPECT_EQ(radio_driver.set_params_calls, 1);
  EXPECT_FLOAT_EQ(radio_driver.freq, 867.5f);
  EXPECT_FLOAT_EQ(radio_driver.bw, 125.0f);
  EXPECT_EQ(radio_driver.sf, 11);
  EXPECT_EQ(radio_driver.cr, 6);
}

TEST_F(ServerBaseTest, TempParamsAreAppliedOnlyOnce) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 3);

  g_mock_millis = T0 + APPLY_DELAY_MS + 1;
  mesh->loop();
  ASSERT_EQ(radio_driver.set_params_calls, 1);

  mesh->loop();
  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, 1);   // the timer is cleared when it fires
}

TEST_F(ServerBaseTest, TheNodeRevertsToItsOwnSettingsAfterTheTimeout) {
  const uint32_t timeout_mins = 3;
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, (int)timeout_mins);

  g_mock_millis = T0 + APPLY_DELAY_MS + 1;
  mesh->loop();
  ASSERT_FLOAT_EQ(radio_driver.freq, 867.5f);

  // The revert is scheduled from the time of the command, not from the apply, and
  // it is the same strictly-greater comparison.
  const uint32_t revert_at = T0 + APPLY_DELAY_MS + timeout_mins * 60 * 1000;
  g_mock_millis = revert_at;
  mesh->loop();
  EXPECT_FLOAT_EQ(radio_driver.freq, 867.5f);   // the scheduled moment is still early

  g_mock_millis = revert_at + 1;
  mesh->loop();
  EXPECT_FLOAT_EQ(radio_driver.freq, orig_freq);
  EXPECT_FLOAT_EQ(radio_driver.bw, orig_bw);
  EXPECT_EQ(radio_driver.sf, orig_sf);
  EXPECT_EQ(radio_driver.cr, orig_cr);
}

TEST_F(ServerBaseTest, RevertHappensOnlyOnce) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 1);

  g_mock_millis = T0 + APPLY_DELAY_MS + 60 * 1000 + 1;
  mesh->loop();
  ASSERT_FLOAT_EQ(radio_driver.freq, orig_freq);   // applied and reverted in one loop
  const int after_revert = radio_driver.set_params_calls;

  mesh->loop();
  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, after_revert);
}


// A role with the shared base's state opened up, so a test can see which Region a
// packet was taken to have arrived on. That field drives the scope of every flooded
// reply, and nothing else in the firmware reports it.
class RegionProbe : public MyMesh {
public:
  using MyMesh::MyMesh;

  RegionEntry* seenRegion() const { return recv_pkt_region; }
  RegionMap& regions() { return region_map; }
  mesh::DispatcherAction recv(mesh::Packet* pkt) { return onRecvPacket(pkt); }
};

class RegionResolutionTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;
  std::unique_ptr<RegionProbe> mesh;

  void SetUp() override {
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);

    mesh.reset(new RegionProbe(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);
  }

  // A packet carrying nothing but a route type, which is all the resolution reads.
  static mesh::Packet packetOfRouteType(uint8_t route_type) {
    mesh::Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header = route_type;
    return pkt;
  }
};

TEST_F(RegionResolutionTest, ADirectPacketHasNoRegion) {
  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_DIRECT);
  mesh->recv(&pkt);
  EXPECT_EQ(mesh->seenRegion(), nullptr);
}

TEST_F(RegionResolutionTest, AnUnscopedFloodPacketArrivesOnTheWildcardRegion) {
  mesh->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_FLOOD);
  mesh->recv(&pkt);

  ASSERT_NE(mesh->seenRegion(), nullptr);
  EXPECT_TRUE(mesh->seenRegion()->isWildcard());
  EXPECT_EQ(mesh->seenRegion(), &mesh->regions().getWildcard());
}

TEST_F(RegionResolutionTest, AnUnscopedFloodPacketHasNoRegionWhenTheWildcardDeniesFlood) {
  mesh->regions().getWildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_FLOOD);
  mesh->recv(&pkt);

  EXPECT_EQ(mesh->seenRegion(), nullptr);
}

TEST_F(RegionResolutionTest, AScopedFloodPacketWithTransportCodeZeroArrivesOnTheWildcard) {
  // RegionMap::findMatch() returns the wildcard for transport code 0 before it looks
  // at any Region, so a scoped flood with no code is handled like an un-scoped one.
  mesh->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  mesh->recv(&pkt);

  ASSERT_NE(mesh->seenRegion(), nullptr);
  EXPECT_TRUE(mesh->seenRegion()->isWildcard());
}

TEST_F(RegionResolutionTest, TransportCodeZeroRespectsTheWildcardsFloodDenial) {
  // Both flood route types agree here. The un-scoped branch in onRecvPacket() checks
  // REGION_DENY_FLOOD itself, and findMatch() now applies the wildcard's flags to a
  // scoped flood carrying code 0 as well (ledger issue 10), so denying flood on the
  // wildcard stops both. Before that fix this test pinned the disagreement.
  mesh->regions().getWildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet unscoped = packetOfRouteType(ROUTE_TYPE_FLOOD);
  mesh->recv(&unscoped);
  EXPECT_EQ(mesh->seenRegion(), nullptr) << "un-scoped flood respects the denial";

  mesh::Packet scoped = packetOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  mesh->recv(&scoped);
  EXPECT_EQ(mesh->seenRegion(), nullptr) << "scoped flood with code 0 respects the denial too";
}

TEST_F(RegionResolutionTest, AScopedFloodPacketWithAnUnknownCodeHasNoRegion) {
  // A non-zero code goes through the Region lookup, and nothing but the wildcard is
  // loaded, so no Region claims it. This is the case that tells the lookup apart
  // from simply handing back the wildcard.
  mesh->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  pkt.transport_codes[0] = 0x1234;
  mesh->recv(&pkt);

  EXPECT_EQ(mesh->seenRegion(), nullptr);
}

TEST_F(RegionResolutionTest, ANewRegionDeniesFloodUntilItIsAllowed) {
  // RegionMap::putRegion() sets REGION_DENY_FLOOD on a new Region ("DENY by
  // default"), so a freshly added Region is invisible to the flood lookup until the
  // flag is cleared. Pinned because the tests below depend on it, and because the
  // default itself is what the lookup's mask acts on.
  RegionEntry* region = mesh->regions().putRegion("#probe", 0);
  ASSERT_NE(region, nullptr);
  EXPECT_TRUE(region->flags & REGION_DENY_FLOOD);
}

TEST_F(RegionResolutionTest, AScopedFloodPacketArrivesOnTheRegionWhoseCodeItCarries) {
  mesh->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;
  RegionEntry* region = mesh->regions().putRegion("#probe", 0);
  ASSERT_NE(region, nullptr);
  region->flags &= ~REGION_DENY_FLOOD;   // allow flood, so the lookup considers it

  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  TransportKey keys[4];
  ASSERT_GT(mesh->regions().getTransportKeysFor(*region, keys, 4), 0);
  pkt.transport_codes[0] = keys[0].calcTransportCode(&pkt);

  mesh->recv(&pkt);

  ASSERT_NE(mesh->seenRegion(), nullptr);
  EXPECT_EQ(mesh->seenRegion()->id, region->id);
}

TEST_F(RegionResolutionTest, ARegionThatDeniesFloodIsSkippedEvenForItsOwnCode) {
  // The lookup is passed REGION_DENY_FLOOD as its mask, so a Region carrying that
  // flag is passed over and the packet ends up with no Region at all. This is the
  // case that tells the mask apart from passing 0.
  mesh->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;
  RegionEntry* region = mesh->regions().putRegion("#probe", 0);
  ASSERT_NE(region, nullptr);
  region->flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = packetOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  TransportKey keys[4];
  ASSERT_GT(mesh->regions().getTransportKeysFor(*region, keys, 4), 0);
  pkt.transport_codes[0] = keys[0].calcTransportCode(&pkt);

  // Same packet, same code, only the Region's flag differs.
  region->flags |= REGION_DENY_FLOOD;
  mesh->recv(&pkt);

  EXPECT_EQ(mesh->seenRegion(), nullptr);
}

TEST_F(RegionResolutionTest, TheRegionIsClearedBetweenPackets) {
  mesh->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet flood = packetOfRouteType(ROUTE_TYPE_FLOOD);
  mesh->recv(&flood);
  ASSERT_NE(mesh->seenRegion(), nullptr);

  // A direct packet must not leave the previous packet's Region standing, or a
  // reply would be scoped by whatever arrived before it.
  mesh::Packet direct = packetOfRouteType(ROUTE_TYPE_DIRECT);
  mesh->recv(&direct);
  EXPECT_EQ(mesh->seenRegion(), nullptr);
}


// The advertisement schedule, which step 4b moves into the shared base. Driven
// through the role's own public surface, so these tests read the same before and
// after the move. They pin the interval arithmetic and which send path each kind of
// advertisement takes, not the millisecond the transmitter happens to pick: the
// duty cycle budget in Dispatcher decides that, and it is not what moved.
class AdvertScheduleTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;
  std::unique_ptr<MyMesh> mesh;

  void SetUp() override {
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);

    mesh.reset(new MyMesh(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);

    // Start from "no advertisements scheduled", whatever begin() left behind.
    mesh->getNodePrefs()->advert_interval = 0;
    mesh->getNodePrefs()->flood_advert_interval = 0;
    mesh->updateAdvertTimer();
    mesh->updateFloodAdvertTimer();
    radio_driver.send_calls = 0;
  }

  // Run the node forward, and report whether it transmitted anything.
  bool runFor(uint32_t millis_span, uint32_t step = 250) {
    const int before = radio_driver.send_calls;
    for (uint32_t t = 0; t < millis_span; t += step) {
      g_mock_millis += step;
      mesh->loop();
    }
    return radio_driver.send_calls > before;
  }
};

TEST_F(AdvertScheduleTest, NothingIsAdvertisedWhenBothIntervalsAreZero) {
  EXPECT_FALSE(runFor(3 * 60 * 60 * 1000));   // three hours of quiet
}

TEST_F(AdvertScheduleTest, TheLocalIntervalCountsTwoMinutesPerUnit) {
  mesh->getNodePrefs()->advert_interval = 1;   // the field is minutes / 2
  mesh->updateAdvertTimer();

  EXPECT_FALSE(runFor(2 * 60 * 1000 - 1000)) << "nothing before the two minutes";
  EXPECT_TRUE(runFor(2000)) << "advertised once the two minutes passed";
}

TEST_F(AdvertScheduleTest, AZeroLocalIntervalStopsAScheduledAdvert) {
  mesh->getNodePrefs()->advert_interval = 1;
  mesh->updateAdvertTimer();

  // Turning it off has to clear the pending timer, not just stop rescheduling.
  mesh->getNodePrefs()->advert_interval = 0;
  mesh->updateAdvertTimer();

  EXPECT_FALSE(runFor(10 * 60 * 1000));
}

TEST_F(AdvertScheduleTest, TheFloodIntervalCountsOneHourPerUnit) {
  mesh->getNodePrefs()->flood_advert_interval = 1;   // the field is hours
  mesh->updateFloodAdvertTimer();

  EXPECT_FALSE(runFor(60 * 60 * 1000 - 60 * 1000)) << "nothing before the hour";
  EXPECT_TRUE(runFor(2 * 60 * 1000)) << "advertised once the hour passed";
}

TEST_F(AdvertScheduleTest, AZeroFloodIntervalStopsAScheduledAdvert) {
  mesh->getNodePrefs()->flood_advert_interval = 1;
  mesh->updateFloodAdvertTimer();

  mesh->getNodePrefs()->flood_advert_interval = 0;
  mesh->updateFloodAdvertTimer();

  EXPECT_FALSE(runFor(2 * 60 * 60 * 1000));
}

TEST_F(AdvertScheduleTest, AFloodAdvertReschedulesTheLocalOneToo) {
  // The role schedules both when a flood advert fires, so the two do not overlap.
  // With the local interval left at zero only the flood advert can fire, and it has
  // to keep firing on its own schedule afterwards.
  mesh->getNodePrefs()->flood_advert_interval = 1;
  mesh->updateFloodAdvertTimer();

  ASSERT_TRUE(runFor(60 * 60 * 1000 + 60 * 1000)) << "first flood advert";
  EXPECT_FALSE(runFor(30 * 60 * 1000)) << "not again within the next half hour";
  EXPECT_TRUE(runFor(35 * 60 * 1000)) << "again after the next hour";
}

TEST_F(AdvertScheduleTest, AnAdvertisementGoesOutOnTheRouteItWasAskedFor) {
  // The route type is in the first byte of the frame, so the two send paths are
  // told apart by what actually left the radio, not just by something leaving it.
  mesh->sendSelfAdvertisement(0, false);
  ASSERT_TRUE(runFor(10 * 1000)) << "zero hop advertisement";
  EXPECT_EQ(radio_driver.last_send[0] & PH_ROUTE_MASK, ROUTE_TYPE_DIRECT);

  radio_driver.send_calls = 0;
  mesh->sendSelfAdvertisement(0, true);
  ASSERT_TRUE(runFor(10 * 1000)) << "flooded advertisement";
  // No default scope is configured here, so the scoped send falls back to a plain
  // flood rather than a transport flood.
  EXPECT_EQ(radio_driver.last_send[0] & PH_ROUTE_MASK, ROUTE_TYPE_FLOOD);
}


// Identity, file system and statistics, which step 4d moves into the shared base.
// Driven through the role's own public surface, so these read the same before and
// after the move.
class HousekeepingTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;
  std::unique_ptr<MyMesh> mesh;

  void SetUp() override {
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    LittleFS.reset();
    rng.begin(1);

    mesh.reset(new MyMesh(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);
  }
};

TEST_F(HousekeepingTest, SettingTheTxPowerReachesTheRadio) {
  radio_driver.tx_power_calls = 0;

  mesh->setTxPower(-7);

  EXPECT_EQ(radio_driver.tx_power_calls, 1);
  EXPECT_EQ(radio_driver.tx_power, -7);
}

TEST_F(HousekeepingTest, ClearingTheStatsResetsTheRadioCounters) {
  radio_driver.packets_recv = 11;
  radio_driver.packets_sent = 7;
  radio_driver.packets_recv_errors = 3;

  mesh->clearStats();

  EXPECT_EQ(radio_driver.packets_recv, 0u);
  EXPECT_EQ(radio_driver.packets_sent, 0u);
  EXPECT_EQ(radio_driver.packets_recv_errors, 0u);
}

TEST_F(HousekeepingTest, FormattingTheFileSystemErasesIt) {
  // On this platform the role erases through the LittleFS global, which is the same
  // in-memory instance the test can look into.
  LittleFS.files["/leftover"] = { 1, 2, 3 };
  ASSERT_TRUE(LittleFS.exists("/leftover"));

  EXPECT_TRUE(mesh->formatFileSystem());

  EXPECT_FALSE(LittleFS.exists("/leftover"));
}

TEST_F(HousekeepingTest, TheStatsRepliesAreFilledIn) {
  char reply[256];

  memset(reply, 0, sizeof(reply));
  mesh->formatStatsReply(reply);
  EXPECT_GT(strlen(reply), 0u) << "core stats";

  memset(reply, 0, sizeof(reply));
  mesh->formatRadioStatsReply(reply);
  EXPECT_GT(strlen(reply), 0u) << "radio stats";

  memset(reply, 0, sizeof(reply));
  mesh->formatPacketStatsReply(reply);
  EXPECT_GT(strlen(reply), 0u) << "packet stats";
}

TEST_F(HousekeepingTest, ClearingTheStatsResetsTheCoreCountersToo) {
  // Make the core count a transmission of its own, so the reset has something to
  // clear beyond the radio's counters.
  mesh->sendSelfAdvertisement(0, true);
  for (int i = 0; i < 40; i++) { g_mock_millis += 250; mesh->loop(); }
  ASSERT_GT(radio_driver.send_calls, 0) << "something has to have been sent";

  char busy[256] = {0};
  mesh->formatPacketStatsReply(busy);

  mesh->clearStats();

  char cleared[256] = {0};
  mesh->formatPacketStatsReply(cleared);
  EXPECT_STRNE(busy, cleared) << "the core's own counters have to be reset as well";
}

TEST_F(HousekeepingTest, ThePacketStatsFollowTheRadioCounters) {
  char before[256] = {0};
  mesh->formatPacketStatsReply(before);

  radio_driver.packets_recv_errors = 42;
  char after[256] = {0};
  mesh->formatPacketStatsReply(after);

  EXPECT_STRNE(before, after) << "the reply has to reflect the counters it reads";
}


// The ACL peer lookup, which step 4e moved into the shared base. The ACL is
// protected on the base, so a subclass can populate it; before the move it was
// private in each role and could not be reached from a test at all.
class PeerLookupProbe : public MyMesh {
public:
  using MyMesh::MyMesh;

  ClientACL& clients() { return acl; }
  int search(const uint8_t* hash) { return searchPeersByHash(hash); }
  void secretFor(uint8_t* dest, int peer_idx) { getPeerSharedSecret(dest, peer_idx); }
};

class PeerLookupTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;
  std::unique_ptr<PeerLookupProbe> mesh;

  void SetUp() override {
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);

    mesh.reset(new PeerLookupProbe(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);
  }

  // The hash is the first byte of the public key (PATH_HASH_SIZE is 1), so the
  // first byte decides who matches and the rest only keeps the keys distinct.
  mesh::Identity identityStarting(uint8_t first, uint8_t tag) {
    uint8_t pub[PUB_KEY_SIZE];
    memset(pub, tag, sizeof(pub));
    pub[0] = first;
    return mesh::Identity(pub);
  }

  ClientInfo* addClient(uint8_t first, uint8_t tag) {
    ClientInfo* c = mesh->clients().putClient(identityStarting(first, tag), 0);
    if (c) memset(c->shared_secret, tag, PUB_KEY_SIZE);
    return c;
  }
};

TEST_F(PeerLookupTest, AnEmptyAclMatchesNobody) {
  const uint8_t hash[1] = { 0xAA };
  EXPECT_EQ(mesh->search(hash), 0);
}

TEST_F(PeerLookupTest, OnlyTheClientsWhoseHashMatchesAreReturned) {
  addClient(0xAA, 1);
  addClient(0xBB, 2);
  addClient(0xAA, 3);

  const uint8_t hash[1] = { 0xAA };
  EXPECT_EQ(mesh->search(hash), 2);

  const uint8_t other[1] = { 0xBB };
  EXPECT_EQ(mesh->search(other), 1);

  const uint8_t missing[1] = { 0xCC };
  EXPECT_EQ(mesh->search(missing), 0);
}

TEST_F(PeerLookupTest, TheSharedSecretBelongsToTheMatchedClient) {
  addClient(0xBB, 2);      // not a match, so the index must not be mistaken for it
  addClient(0xAA, 7);

  const uint8_t hash[1] = { 0xAA };
  ASSERT_EQ(mesh->search(hash), 1);

  uint8_t secret[PUB_KEY_SIZE];
  memset(secret, 0, sizeof(secret));
  mesh->secretFor(secret, 0);

  uint8_t expected[PUB_KEY_SIZE];
  memset(expected, 7, sizeof(expected));
  EXPECT_EQ(memcmp(secret, expected, PUB_KEY_SIZE), 0);
}

TEST_F(PeerLookupTest, MoreThanTwentyFourMatchesAreAllReported) {
  // The sensor used to stop at 24, because its index array was sized that way
  // while the other two roles sized theirs for the whole ACL. One array in the
  // base ends the truncation, which is a deliberate change of behaviour for the
  // sensor and is recorded in docs/refactoring/0004-shared-server-base.md.
  const int wanted = 30;   // more than 24, still inside MAX_CLIENTS
  for (int i = 0; i < wanted; i++) {
    ASSERT_NE(addClient(0xAA, (uint8_t)(i + 1)), nullptr) << "client " << i;
  }

  const uint8_t hash[1] = { 0xAA };
  EXPECT_EQ(mesh->search(hash), wanted);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
