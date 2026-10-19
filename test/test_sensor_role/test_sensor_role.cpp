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

TEST_F(SensorRoleTest, OnlyFloodMaxIsAppliedToday) {
  mesh->prefs()->disable_fwd = 0;
  mesh->prefs()->flood_max = 5;
  mesh->prefs()->flood_max_unscoped = 1;    // the other roles would stop at one hop
  mesh->prefs()->flood_max_advert = 1;

  mesh::Packet pkt = floodPacketWithHops(ROUTE_TYPE_FLOOD, 2);

  EXPECT_TRUE(mesh->forwards(&pkt))
      << "today the sensor forwards this, because it only looks at flood_max";
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
