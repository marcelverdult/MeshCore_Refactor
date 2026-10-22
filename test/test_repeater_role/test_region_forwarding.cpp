// Whether the repeater forwards a flood depends on the Region the packet resolves to:
// allowPacketForward() drops a flood that arrived on no Region. These tests let the
// real resolution in onRecvPacket() run and then ask the real gate, so they cover the
// repeater's end of ledger issue 10 (a scoped flood with transport code 0 and the
// wildcard's flood denial), which the room server suite in test_server_base cannot,
// because only the repeater turns a missing Region into a dropped packet.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include <Arduino.h>
#include <target.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>

#include "MyMesh.h"

namespace {

constexpr uint32_t START_MILLIS = 1000;

class ForwardProbe : public MyMesh {
public:
  using MyMesh::MyMesh;

  RegionMap& regions() { return region_map; }
  RegionEntry* seenRegion() const { return recv_pkt_region; }
  mesh::DispatcherAction recv(mesh::Packet* pkt) { return onRecvPacket(pkt); }
  bool forwards(const mesh::Packet* pkt) { return allowPacketForward(pkt); }
};

class RepeaterRegionForwardingTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;
  std::unique_ptr<ForwardProbe> node;

  void SetUp() override {
    g_mock_millis = START_MILLIS;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);

    node.reset(new ForwardProbe(board, radio_driver, ms, rng, rtc_clock, tables));
    node->begin(&fs);
  }

  // A flood with an empty path and transport code 0: nothing but the route type for
  // the Region lookup to read, and nothing for the hop limits or the loop detection
  // to object to.
  static mesh::Packet floodOfRouteType(uint8_t route_type) {
    mesh::Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header = route_type;
    return pkt;
  }

  // What the repeater does with the packet: resolve its Region, then decide.
  bool forwardsAfterReceiving(mesh::Packet& pkt) {
    node->recv(&pkt);
    return node->forwards(&pkt);
  }
};

TEST_F(RepeaterRegionForwardingTest, ForwardsAScopedFloodWithCodeZeroWhileTheWildcardAllowsFlood) {
  // The control case: with flood allowed on the wildcard nothing else in the gate
  // stops this packet, so the two cases below fail for the Region alone.
  node->regions().getWildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = floodOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  EXPECT_TRUE(forwardsAfterReceiving(pkt));
  EXPECT_NE(node->seenRegion(), nullptr);
}

TEST_F(RepeaterRegionForwardingTest, DropsAnUnscopedFloodWhenTheWildcardDeniesFlood) {
  node->regions().getWildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet pkt = floodOfRouteType(ROUTE_TYPE_FLOOD);
  EXPECT_FALSE(forwardsAfterReceiving(pkt));
  EXPECT_EQ(node->seenRegion(), nullptr);
}

TEST_F(RepeaterRegionForwardingTest, DropsAScopedFloodWithCodeZeroWhenTheWildcardDeniesFlood) {
  // `region denyf *` is documented to drop packets without a Region transport code.
  // A scoped flood whose code is 0 carries none, so it must be dropped like the
  // un-scoped flood above.
  node->regions().getWildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet pkt = floodOfRouteType(ROUTE_TYPE_TRANSPORT_FLOOD);
  EXPECT_FALSE(forwardsAfterReceiving(pkt));
  EXPECT_EQ(node->seenRegion(), nullptr);
}

}  // namespace
