#include <gtest/gtest.h>
#include <helpers/RegionMap.h>

// RegionMap::findMatch() decides which Region a scoped flood arrives on. The servers
// call it with REGION_DENY_FLOOD and forward the packet only when it returns a Region.
// An un-scoped flood is filtered by the roles themselves, on the wildcard's own flag.
// A scoped flood that carries transport code 0 has no scope at all and is meant to be
// handled like an un-scoped one, so the wildcard's flag has to apply to it as well.

namespace {

mesh::Packet scopedFlood(uint16_t code) {
  mesh::Packet pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header = ROUTE_TYPE_TRANSPORT_FLOOD;
  pkt.transport_codes[0] = code;
  return pkt;
}

class FindMatchTest : public ::testing::Test {
protected:
  TransportKeyStore store;
  RegionMap regions{store};

  RegionEntry& wildcard() { return regions.getWildcard(); }

  // A Region that allows flood, together with the code a sender scoping to it uses.
  RegionEntry* allowedRegion(const char* name, mesh::Packet& pkt) {
    RegionEntry* region = regions.putRegion(name, 0);
    region->flags &= ~REGION_DENY_FLOOD;   // putRegion() denies flood by default

    TransportKey keys[4];
    EXPECT_GT(regions.getTransportKeysFor(*region, keys, 4), 0);
    pkt.transport_codes[0] = keys[0].calcTransportCode(&pkt);
    return region;
  }
};

}  // namespace

TEST_F(FindMatchTest, CodeZeroArrivesOnTheWildcardWhenItAllowsFlood) {
  wildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), &wildcard());
}

TEST_F(FindMatchTest, CodeZeroHasNoRegionWhenTheWildcardDeniesFlood) {
  wildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(FindMatchTest, CodeZeroIsOnlyHeldBackByTheFlagTheMaskNames) {
  // The wildcard denies flood, but the caller asks about a different flag, and
  // asking about none at all matches everything. So the shortcut has to apply the
  // mask it is given, not a fixed flag.
  wildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_DIRECT), &wildcard());
  EXPECT_EQ(regions.findMatch(&pkt, 0), &wildcard());

  wildcard().flags = REGION_DENY_DIRECT;
  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_DIRECT), nullptr);
  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), &wildcard());
}

TEST_F(FindMatchTest, AnUnknownCodeHasNoRegionEvenWhenTheWildcardAllowsFlood) {
  wildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0x1234);
  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(FindMatchTest, ACodeThatBelongsToNoRegionHasNoRegionWhileRegionsExist) {
  // With a Region in the table the lookup really compares codes, so a code that is
  // not that Region's has to come back empty.
  wildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  allowedRegion("#probe", pkt);
  pkt.transport_codes[0]++;   // the Region's code is never 0xFFFF, so this stays non-zero

  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(FindMatchTest, TheCodeOfAnAllowedRegionFindsThatRegion) {
  wildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  RegionEntry* region = allowedRegion("#probe", pkt);
  ASSERT_NE(pkt.transport_codes[0], 0);

  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), region);
}

TEST_F(FindMatchTest, ARegionThatDeniesFloodIsSkippedForItsOwnCode) {
  wildcard().flags &= ~REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  RegionEntry* region = allowedRegion("#probe", pkt);
  region->flags |= REGION_DENY_FLOOD;

  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), nullptr);
}

TEST_F(FindMatchTest, ADeniedWildcardDoesNotHoldBackAScopeThatIsAllowed) {
  // Only a packet with no scope falls back on the wildcard. A packet scoped to a
  // Region that allows flood is that Region's business, whatever the wildcard says.
  wildcard().flags |= REGION_DENY_FLOOD;

  mesh::Packet pkt = scopedFlood(0);
  RegionEntry* region = allowedRegion("#probe", pkt);

  EXPECT_EQ(regions.findMatch(&pkt, REGION_DENY_FLOOD), region);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
