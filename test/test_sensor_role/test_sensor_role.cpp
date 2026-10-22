// Characterization tests for the sensor role, which until now had no host
// environment of its own: the repeater and the room server both define MyMesh, so
// each role needs a separate binary.
//
// The sensor is the role that drifted away from the other two (see the ledger,
// finding F2). These tests record what it does today, before the alignment changes
// any of it, so the change can be shown to land exactly where it was meant to.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <Arduino.h>
#include <target.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>

#include "SensorMesh.h"

namespace {

constexpr uint32_t T0 = 1000;

// SensorMesh is abstract on purpose: the application supplies what to read and how
// to answer a series query. This is the smallest concrete role that satisfies that,
// mirroring the shape of examples/simple_sensor/main.cpp without its battery logic,
// so the tests exercise the role and not an application's choices.
class TestSensor : public SensorMesh {
public:
  TestSensor(mesh::MainBoard& board, mesh::Radio& radio, mesh::MillisecondClock& ms,
             mesh::RNG& rng, mesh::RTCClock& rtc, mesh::MeshTables& tables)
    : SensorMesh(board, radio, ms, rng, rtc, tables) { }

  int reads = 0;

  // The role's own decisions, opened up so a test can read them directly. Nothing
  // else in the firmware reports any of these.
  uint8_t extraAcks() const { return getExtraAckTransmitCount(); }
  bool forwards(const mesh::Packet* pkt) { return allowPacketForward(pkt); }
  uint32_t floodRetransmitDelay(const mesh::Packet* pkt) { return getRetransmitDelay(pkt); }
  uint32_t airtimeFor(int len) { return _radio->getEstAirtimeFor(len); }
  NodePrefs* prefs() { return getNodePrefs(); }
  bool upgradeFrom(uint8_t version) { return upgradePrefs(version); }

protected:
  void onSensorDataRead() override { reads++; }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago,
                      MinMaxAvg dest[], int max_num) override {
    return 0;
  }
};

class SensorRoleTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;
  std::unique_ptr<TestSensor> mesh;

  void SetUp() override {
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    LittleFS.reset();
    rng.begin(1);

    mesh.reset(new TestSensor(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);
  }
};

TEST_F(SensorRoleTest, TheSensorStartsUp) {
  // The harness itself, so a failure here is the environment and not the behaviour.
  EXPECT_NE(mesh->getNodePrefs(), nullptr);
}


// A flood packet carrying a chosen number of path hashes, which is what the hop
// limits are tested against.
static mesh::Packet floodPacketWithHops(uint8_t route_type, uint8_t hops) {
  mesh::Packet pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.header = route_type;
  pkt.path_len = hops;   // one byte per hash at the default path hash size
  return pkt;
}

TEST_F(SensorRoleTest, MultiAcksTakesEffect) {
  // Was: the role reported no count whatever the setting said, so the branch in
  // SensorMesh::sendAckTo() was dead and 'set multi.acks on' did nothing. The
  // override is gone, so the core reports the stored setting as it does for the
  // other two roles.
  mesh->prefs()->multi_acks = 1;
  EXPECT_EQ(mesh->extraAcks(), 1);

  mesh->prefs()->multi_acks = 0;
  EXPECT_EQ(mesh->extraAcks(), 0) << "and off still means off";
}

TEST_F(SensorRoleTest, TheUnscopedHopLimitIsApplied) {
  // Was: the sensor looked at flood_max only, so it forwarded this while the other
  // two roles stopped it.
  mesh->prefs()->disable_fwd = 0;
  mesh->prefs()->flood_max = 5;
  mesh->prefs()->flood_max_unscoped = 1;
  mesh->prefs()->flood_max_advert = 8;

  mesh::Packet over = floodPacketWithHops(ROUTE_TYPE_FLOOD, 2);
  EXPECT_FALSE(mesh->forwards(&over)) << "two hops is past the un-scoped limit of one";

  mesh::Packet under = floodPacketWithHops(ROUTE_TYPE_FLOOD, 0);
  EXPECT_TRUE(mesh->forwards(&under)) << "and a fresh one still goes on";
}

TEST_F(SensorRoleTest, TheAdvertHopLimitIsApplied) {
  mesh->prefs()->disable_fwd = 0;
  mesh->prefs()->flood_max = 64;
  mesh->prefs()->flood_max_unscoped = 64;
  mesh->prefs()->flood_max_advert = 2;

  mesh::Packet advert = floodPacketWithHops(ROUTE_TYPE_FLOOD, 2);
  advert.header |= (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  EXPECT_FALSE(mesh->forwards(&advert)) << "adverts have their own, lower ceiling";

  mesh::Packet plain = floodPacketWithHops(ROUTE_TYPE_FLOOD, 2);
  EXPECT_TRUE(mesh->forwards(&plain)) << "a non advert at the same hop count goes on";
}

TEST_F(SensorRoleTest, AZeroHopLimitIsWrittenOutAsAZero) {
  // The first half of the upgrade hazard: if a zero were written as an empty value,
  // the loader would leave the new default alone and there would be no hazard.
  mesh->prefs()->flood_max_unscoped = 0;
  mesh->prefs()->flood_max_advert = 0;
  mesh->savePrefs();

  ASSERT_TRUE(fs.exists("/prefs.json"));
  const auto& bytes = fs.files["/prefs.json"];
  const std::string json(bytes.begin(), bytes.end());

  EXPECT_NE(json.find("f_max_uns:0,"), std::string::npos)
      << "a zero is written out as a zero, not left empty: " << json;
}

TEST_F(SensorRoleTest, ASensorUpgradedOntoZeroHopLimitsKeepsForwarding) {
  // The hazard this change had to answer. A sensor built before it set no default for
  // the two limits, so it stored zero for both, and with forwarding switched on by an
  // operator that is the state an updated node boots into. Zero means "never
  // forward", because the limits are compared with >=. The file carries no prefs
  // version, so the first boot lifts both zeros to the role defaults (see
  // BootSequenceChecks.h for the upgrade itself).
  const char* older = "{repeat:{disable:0,f_max:64,f_max_uns:0,f_max_adv:0}}";
  const std::string text(older);
  fs.files["/prefs.json"] = std::vector<uint8_t>(text.begin(), text.end());

  TestSensor updated(board, radio_driver, ms, rng, rtc_clock, tables);
  updated.begin(&fs);

  EXPECT_EQ(updated.prefs()->disable_fwd, 0) << "the operator had forwarding on";

  mesh::Packet fresh_flood = floodPacketWithHops(ROUTE_TYPE_FLOOD, 0);
  EXPECT_TRUE(updated.forwards(&fresh_flood))
      << "an upgraded node goes on forwarding un-scoped floods";

  mesh::Packet advert = floodPacketWithHops(ROUTE_TYPE_FLOOD, 0);
  advert.header |= (PAYLOAD_TYPE_ADVERT << PH_TYPE_SHIFT);
  EXPECT_TRUE(updated.forwards(&advert)) << "and adverts";
}

TEST_F(SensorRoleTest, TheHopLimitStepIsOnlyForFilesBeforeVersionOne) {
  // begin() only asks for an upgrade from an older version, but the step must hold
  // on its own: from version 1 on, a zero was chosen on a firmware that applies it.
  mesh->prefs()->flood_max_unscoped = 0;
  mesh->prefs()->flood_max_advert = 0;

  EXPECT_FALSE(mesh->upgradeFrom(1));
  EXPECT_EQ(mesh->prefs()->flood_max_unscoped, 0);
  EXPECT_EQ(mesh->prefs()->flood_max_advert, 0);

  EXPECT_TRUE(mesh->upgradeFrom(0)) << "and from version 0 it does lift them";
  EXPECT_EQ(mesh->prefs()->flood_max_unscoped, 64);
}

TEST_F(SensorRoleTest, AMissingKeyLeavesTheNewDefaultStanding) {
  // The other half of the upgrade picture: a key absent from the file overwrites
  // nothing, so a node whose file predates the key gets the new default.
  const char* without = "{repeat:{disable:0,f_max:64}}";
  const std::string text(without);
  fs.files["/prefs.json"] = std::vector<uint8_t>(text.begin(), text.end());

  TestSensor updated(board, radio_driver, ms, rng, rtc_clock, tables);
  updated.begin(&fs);

  EXPECT_EQ(updated.prefs()->flood_max_unscoped, 64);
  EXPECT_EQ(updated.prefs()->flood_max_advert, 8);
}

TEST_F(SensorRoleTest, FloodMaxItselfIsApplied) {
  mesh->prefs()->disable_fwd = 0;
  mesh->prefs()->flood_max = 2;

  mesh::Packet under = floodPacketWithHops(ROUTE_TYPE_FLOOD, 1);
  EXPECT_TRUE(mesh->forwards(&under));

  mesh::Packet at_limit = floodPacketWithHops(ROUTE_TYPE_FLOOD, 2);
  EXPECT_FALSE(mesh->forwards(&at_limit)) << "the limit is reached, not exceeded";
}

TEST_F(SensorRoleTest, ForwardingCanBeTurnedOffEntirely) {
  mesh->prefs()->disable_fwd = 1;
  mesh->prefs()->flood_max = 5;

  mesh::Packet pkt = floodPacketWithHops(ROUTE_TYPE_FLOOD, 0);
  EXPECT_FALSE(mesh->forwards(&pkt));
}

TEST_F(SensorRoleTest, TheBoostedGainPrefIsAppliedAtBoot) {
  // Was: the sensor pushed nothing to the radio in begin(), so a value set over the
  // CLI applied at once and was gone after a reboot. begin() has already run in
  // SetUp(), so the call must have happened by now.
  EXPECT_EQ(radio_driver.rx_boost_calls, 1);

  // The pref itself is only defaulted on SX1262 and SX1268 builds, which this host
  // environment is not, so it stays at zero here and the applied value is zero.
  EXPECT_EQ(mesh->prefs()->rx_boosted_gain, 0);
  EXPECT_FALSE(radio_driver.rx_boosted);
}

TEST_F(SensorRoleTest, AStoredBoostedGainSurvivesAReboot) {
  // The point of applying it in begin(): the value has to reach the radio again
  // after a restart, not only when the command is given.
  const char* stored = "{radio:{rxgain:1}}";
  const std::string text(stored);
  fs.files["/prefs.json"] = std::vector<uint8_t>(text.begin(), text.end());

  radio_driver.reset();
  TestSensor rebooted(board, radio_driver, ms, rng, rtc_clock, tables);
  rebooted.begin(&fs);

  EXPECT_EQ(rebooted.prefs()->rx_boosted_gain, 1);
  EXPECT_EQ(radio_driver.rx_boost_calls, 1);
  EXPECT_TRUE(radio_driver.rx_boosted) << "the stored setting reached the radio";
}

TEST_F(SensorRoleTest, TheRetransmitDelayDrawsFromSixDiscreteSlots) {
  // This one is deliberately kept, not aligned: the other roles draw uniformly from
  // a continuous range, and upstream still calls that change experimental. Pinned so
  // the role parameter cannot be lost by accident.
  mesh->prefs()->tx_delay_factor = 1.0f;

  mesh::Packet pkt = floodPacketWithHops(ROUTE_TYPE_FLOOD, 0);
  const uint32_t t = mesh->airtimeFor(pkt.getPathByteLen() + pkt.payload_len + 2);
  ASSERT_GT(t, 0u);

  for (int i = 0; i < 200; i++) {
    const uint32_t d = mesh->floodRetransmitDelay(&pkt);
    ASSERT_EQ(d % t, 0u) << "every delay is a whole multiple of the airtime";
    ASSERT_LE(d / t, 5u) << "and never more than five of them";
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
