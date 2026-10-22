#pragma once

// Which packets a server role passes on, the decision allowPacketForward() makes.
// Included by the suite of each role (repeater, room server, sensor) right after
// BootSequenceChecks.h, so it uses the role the suite named as `BootRole` and the
// fixture defined there, and the same expectations run against all three.
//
// The room server and the sensor check two things, forwarding switched off and the
// flood hop limits. The repeater checks the same two first and then two of its own,
// the Region a flood arrived on and loop detection. A suite defines
// FORWARD_ROLE_CHECKS_REGION_AND_LOOPS if its role does the latter, and
// FORWARD_ROLE_FORWARDS_BY_DEFAULT if a fresh node of the role forwards.

#include <cstring>

namespace boot_checks {

class ForwardProbe : public BootProbe {
public:
  using BootProbe::BootProbe;

  bool forwards(const mesh::Packet* pkt) { return allowPacketForward(pkt); }
  void arrivedOn(RegionEntry* r) { recv_pkt_region = r; }
};

class ForwardPolicyTest : public BootSequenceTest {
protected:
  std::unique_ptr<ForwardProbe> node;
  RegionEntry* region = nullptr;

  void SetUp() override {
    BootSequenceTest::SetUp();
    node.reset(new ForwardProbe(board, radio_driver, ms, rng, rtc_clock, tables));
    node->begin(&fs);
    // A node that forwards, with limits that stop nothing in the tests below and a
    // Region the packet arrived on, so each test changes the one thing it is about.
    node->prefs().disable_fwd = 0;
    node->prefs().flood_max = 64;
    node->prefs().flood_max_unscoped = 64;
    node->prefs().flood_max_advert = 64;
    node->prefs().loop_detect = LOOP_DETECT_OFF;
    region = node->regions().putRegion("#fwd", 0);
    ASSERT_NE(region, nullptr);
    node->arrivedOn(region);
  }

  static mesh::Packet packet(uint8_t route_type, uint8_t hops, uint8_t payload_type = 0) {
    mesh::Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header = route_type | (payload_type << PH_TYPE_SHIFT);
    pkt.path_len = hops;   // one byte per hash at the default path hash size
    return pkt;
  }
};

TEST_F(ForwardPolicyTest, AFreshNodeForwardsOnlyIfTheRoleDoesByDefault) {
  auto fresh = std::unique_ptr<ForwardProbe>(new ForwardProbe(board, radio_driver, ms, rng, rtc_clock, tables));
  fresh->begin(&fs);
  fresh->arrivedOn(region);
  mesh::Packet pkt = packet(ROUTE_TYPE_FLOOD, 0);
#ifdef FORWARD_ROLE_FORWARDS_BY_DEFAULT
  EXPECT_TRUE(fresh->forwards(&pkt));
#else
  EXPECT_FALSE(fresh->forwards(&pkt)) << "forwarding is off until an operator switches it on";
#endif
}

TEST_F(ForwardPolicyTest, ForwardingOffRefusesEveryRoute) {
  node->prefs().disable_fwd = 1;
  for (uint8_t route : {ROUTE_TYPE_TRANSPORT_FLOOD, ROUTE_TYPE_FLOOD, ROUTE_TYPE_DIRECT, ROUTE_TYPE_TRANSPORT_DIRECT}) {
    mesh::Packet pkt = packet(route, 0);
    EXPECT_FALSE(node->forwards(&pkt)) << "route type " << int(route);
  }
}

TEST_F(ForwardPolicyTest, AFreshFloodOfEitherKindIsForwarded) {
  mesh::Packet unscoped = packet(ROUTE_TYPE_FLOOD, 0);
  mesh::Packet scoped = packet(ROUTE_TYPE_TRANSPORT_FLOOD, 0);
  EXPECT_TRUE(node->forwards(&unscoped));
  EXPECT_TRUE(node->forwards(&scoped));
}

TEST_F(ForwardPolicyTest, FloodMaxStopsBothKindsOfFloodAtTheLimit) {
  node->prefs().flood_max = 3;
  mesh::Packet under = packet(ROUTE_TYPE_TRANSPORT_FLOOD, 2);
  mesh::Packet at_unscoped = packet(ROUTE_TYPE_FLOOD, 3);
  mesh::Packet at_scoped = packet(ROUTE_TYPE_TRANSPORT_FLOOD, 3);
  EXPECT_TRUE(node->forwards(&under));
  EXPECT_FALSE(node->forwards(&at_unscoped)) << "the limit is compared with >=";
  EXPECT_FALSE(node->forwards(&at_scoped));
}

TEST_F(ForwardPolicyTest, TheUnscopedLimitStopsOnlyUnscopedFloods) {
  node->prefs().flood_max_unscoped = 2;
  mesh::Packet unscoped = packet(ROUTE_TYPE_FLOOD, 2);
  mesh::Packet scoped = packet(ROUTE_TYPE_TRANSPORT_FLOOD, 2);
  EXPECT_FALSE(node->forwards(&unscoped));
  EXPECT_TRUE(node->forwards(&scoped));
}

TEST_F(ForwardPolicyTest, TheAdvertLimitStopsOnlyAdverts) {
  node->prefs().flood_max_advert = 2;
  mesh::Packet advert = packet(ROUTE_TYPE_TRANSPORT_FLOOD, 2, PAYLOAD_TYPE_ADVERT);
  mesh::Packet other = packet(ROUTE_TYPE_TRANSPORT_FLOOD, 2);
  EXPECT_FALSE(node->forwards(&advert));
  EXPECT_TRUE(node->forwards(&other));
}

TEST_F(ForwardPolicyTest, HopLimitsDoNotApplyToDirectRoutes) {
  node->prefs().flood_max = 0;
  node->prefs().flood_max_unscoped = 0;
  node->prefs().flood_max_advert = 0;
  mesh::Packet direct = packet(ROUTE_TYPE_DIRECT, 5, PAYLOAD_TYPE_ADVERT);
  mesh::Packet transport_direct = packet(ROUTE_TYPE_TRANSPORT_DIRECT, 5);
  EXPECT_TRUE(node->forwards(&direct));
  EXPECT_TRUE(node->forwards(&transport_direct));
}

#ifdef FORWARD_ROLE_CHECKS_REGION_AND_LOOPS

TEST_F(ForwardPolicyTest, AFloodWithoutARegionIsRefused) {
  node->arrivedOn(nullptr);
  mesh::Packet flood = packet(ROUTE_TYPE_FLOOD, 0);
  mesh::Packet direct = packet(ROUTE_TYPE_DIRECT, 0);
  EXPECT_FALSE(node->forwards(&flood));
  EXPECT_TRUE(node->forwards(&direct)) << "a direct route needs no Region";
}

TEST_F(ForwardPolicyTest, LoopDetectionCountsThisNodeInThePath) {
  // Minimal loop detection refuses a flood whose path already holds this node's
  // one byte hash four times.
  mesh::Packet pkt = packet(ROUTE_TYPE_FLOOD, 4);
  memset(pkt.path, node->identity().pub_key[0], 4);

  EXPECT_TRUE(node->forwards(&pkt)) << "off by default";
  node->prefs().loop_detect = LOOP_DETECT_MINIMAL;
  EXPECT_FALSE(node->forwards(&pkt));

  pkt.path[0] ^= 0xFF;   // three times only
  EXPECT_TRUE(node->forwards(&pkt));
}

#ifdef MESH_DEBUG

TEST_F(ForwardPolicyTest, TheSharedChecksComeBeforeTheRegionCheck) {
  // Only the debug log shows which check refused a packet. With forwarding off, the
  // Region check must not run at all.
  node->arrivedOn(nullptr);
  mesh::Packet flood = packet(ROUTE_TYPE_FLOOD, 0);
  const char* region_line = "allowPacketForward: unknown transport code";

  Serial.out.clear();
  EXPECT_FALSE(node->forwards(&flood));
  EXPECT_NE(Serial.out.find(region_line), std::string::npos) << "the control: forwarding on";

  node->prefs().disable_fwd = 1;
  Serial.out.clear();
  EXPECT_FALSE(node->forwards(&flood));
  EXPECT_EQ(Serial.out.find(region_line), std::string::npos) << Serial.out;
}

#endif  // MESH_DEBUG

#else

TEST_F(ForwardPolicyTest, ARegionIsNotNeededToForwardAFlood) {
  node->arrivedOn(nullptr);
  mesh::Packet flood = packet(ROUTE_TYPE_FLOOD, 0);
  EXPECT_TRUE(node->forwards(&flood)) << "only the repeater refuses a flood without one";
}

TEST_F(ForwardPolicyTest, LoopDetectionIsNotApplied) {
  node->prefs().loop_detect = LOOP_DETECT_STRICT;
  mesh::Packet pkt = packet(ROUTE_TYPE_FLOOD, 4);
  memset(pkt.path, node->identity().pub_key[0], 4);
  EXPECT_TRUE(node->forwards(&pkt));
}

#endif  // FORWARD_ROLE_CHECKS_REGION_AND_LOOPS

}  // namespace boot_checks
