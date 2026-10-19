// Characterization tests for the tuning hooks of mesh::Dispatcher and mesh::Mesh:
// the values the core uses for duty cycle, RX delay, retransmit delays, extra ACKs,
// interference threshold, CAD and AGC reset when no role overrides them. They run
// the real Dispatcher.cpp and Mesh.cpp against fakes for the radio, clock, RNG,
// packet manager and tables.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include <Mesh.h>

namespace {

// Airtime grows with the length, so a wrong length in a formula changes the result.
class FakeRadio : public mesh::Radio {
public:
  int recvRaw(uint8_t* bytes, int sz) override { return 0; }
  uint32_t getEstAirtimeFor(int len_bytes) override { return 11 + 37 * (uint32_t)len_bytes; }
  float packetScore(float snr, int packet_len) override { return 0; }
  bool startSendRaw(const uint8_t* bytes, int len) override { return false; }
  bool isSendComplete() override { return true; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }
};

class FakeClock : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return 0; }
};

class FakeRTC : public mesh::RTCClock {
public:
  uint32_t getCurrentTime() override { return 0; }
  void setCurrentTime(uint32_t time) override { }
};

// Returns a fixed sequence of 32 bit values; two instances with the same sequence
// let a test compute the expected nextInt() result independently of the core.
class SequenceRNG : public mesh::RNG {
  std::vector<uint32_t> _values;
  size_t _next = 0;
public:
  explicit SequenceRNG(std::vector<uint32_t> values) : _values(values) { }
  void random(uint8_t* dest, size_t sz) override {
    uint32_t v = _values[_next++ % _values.size()];
    memset(dest, 0, sz);
    memcpy(dest, &v, sz < sizeof(v) ? sz : sizeof(v));
  }
};

class NullPacketManager : public mesh::PacketManager {
public:
  mesh::Packet* allocNew() override { return nullptr; }
  void free(mesh::Packet* packet) override { }
  void queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) override { }
  mesh::Packet* getNextOutbound(uint32_t now) override { return nullptr; }
  int getOutboundCount(uint32_t now) const override { return 0; }
  int getOutboundTotal() const override { return 0; }
  int getFreeCount() const override { return 0; }
  mesh::Packet* getOutboundByIdx(int i) override { return nullptr; }
  mesh::Packet* removeOutboundByIdx(int i) override { return nullptr; }
  void queueInbound(mesh::Packet* packet, uint32_t scheduled_for) override { }
  mesh::Packet* getNextInbound(uint32_t now) override { return nullptr; }
};

class NullTables : public mesh::MeshTables {
public:
  bool wasSeen(const mesh::Packet* packet) override { return false; }
  void markSeen(const mesh::Packet* packet) override { }
  void clear(const mesh::Packet* packet) override { }
};

// A Mesh with nothing overridden, so the core's own hook implementations run.
class CoreMesh : public mesh::Mesh {
public:
  CoreMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
           mesh::PacketManager& mgr, mesh::MeshTables& tables)
    : mesh::Mesh(radio, ms, rng, rtc, mgr, tables) { }

  using mesh::Mesh::getAirtimeBudgetFactor;
  using mesh::Mesh::calcRxDelay;
  using mesh::Mesh::getInterferenceThreshold;
  using mesh::Mesh::getCADEnabled;
  using mesh::Mesh::getAGCResetInterval;
  using mesh::Mesh::getRetransmitDelay;
  using mesh::Mesh::getDirectRetransmitDelay;
  using mesh::Mesh::getExtraAckTransmitCount;
};

const std::vector<uint32_t> kRandom = { 0, 1, 7, 12345, 0x7FFFFFFF, 0xFFFFFFFF, 999983 };

// Packets with a 3 x 2 byte path and a 40 byte payload: one plain flood packet and
// one with transport codes (4 more bytes on air), so formulas that differ only in
// how they count the length give different results.
std::vector<mesh::Packet> makePackets() {
  std::vector<mesh::Packet> pkts;
  for (uint8_t route : {ROUTE_TYPE_FLOOD, ROUTE_TYPE_TRANSPORT_FLOOD}) {
    mesh::Packet pkt;
    pkt.header = route | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    pkt.setPathHashSizeAndCount(2, 3);
    pkt.payload_len = 40;
    pkts.push_back(pkt);
  }
  return pkts;
}

struct Harness {
  FakeRadio radio;
  FakeClock clock;
  FakeRTC rtc;
  SequenceRNG rng{kRandom};
  NullPacketManager mgr;
  NullTables tables;
  CoreMesh mesh{radio, clock, rng, rtc, mgr, tables};
};

}  // namespace

// Without any settings attached, the core keeps its built-in defaults
// (src/Dispatcher.cpp and src/Mesh.cpp as of v1.18.0).
TEST(CoreTuningDefaults, DutyCycleInterferenceCadAgcAndAcks) {
  Harness h;
  EXPECT_FLOAT_EQ(1.0f, h.mesh.getAirtimeBudgetFactor());
  EXPECT_EQ(0, h.mesh.getInterferenceThreshold());
  EXPECT_FALSE(h.mesh.getCADEnabled());
  EXPECT_EQ(0, h.mesh.getAGCResetInterval());
  EXPECT_EQ(0, h.mesh.getExtraAckTransmitCount());
}

TEST(CoreTuningDefaults, RxDelayUsesBaseTen) {
  Harness h;
  // Dispatcher::calcRxDelay(): (pow(10, 0.85f - score) - 1.0) * air_time
  for (float score : {0.0f, 0.3f, 0.85f, 1.0f}) {
    for (uint32_t air : {0u, 100u, 1234u}) {
      int expected = (int) ((pow(10, 0.85f - score) - 1.0) * air);
      EXPECT_EQ(expected, h.mesh.calcRxDelay(score, air)) << "score " << score << " air " << air;
    }
  }
  // pinned values
  EXPECT_EQ(6079, h.mesh.calcRxDelay(0.0f, 1000));
  EXPECT_EQ(0, h.mesh.calcRxDelay(0.85f, 1000));
}

TEST(CoreTuningDefaults, RetransmitDelaysUseRawLength) {
  for (mesh::Packet pkt : makePackets()) {
    Harness h;
    SequenceRNG expected_rng(kRandom);
    for (size_t i = 0; i < kRandom.size(); i++) {
      // Mesh::getRetransmitDelay(): t = (airtime(raw length) * 52 / 50) / 2, nextInt(0, 5) * t
      uint32_t t = (h.radio.getEstAirtimeFor(pkt.getRawLength()) * 52 / 50) / 2;
      uint32_t expected = expected_rng.nextInt(0, 5) * t;
      EXPECT_EQ(expected, h.mesh.getRetransmitDelay(&pkt)) << "draw " << i;
    }
    EXPECT_EQ(0u, h.mesh.getDirectRetransmitDelay(&pkt));
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
