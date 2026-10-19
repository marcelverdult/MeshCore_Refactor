// Tests for the tuning hooks of mesh::Dispatcher and mesh::Mesh: the values the
// core uses for duty cycle, RX delay, retransmit delays, extra ACKs, interference
// threshold, CAD and AGC reset when no role overrides them, without and with radio
// settings attached through useRadioPrefs(). They run
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

// With settings attached, the core returns what the role overrides of the repeater,
// room server and companion returned in v1.18.0 (all three were identical), e.g.
// examples/simple_repeater/MyMesh.h:131-158 and MyMesh.cpp:542-554.
namespace {

RadioPrefsFields makePrefs(float af, float rx_base, float tx_f, float direct_f,
                           uint8_t thr, uint8_t agc, uint8_t cad, uint8_t acks) {
  RadioPrefsFields p;
  p.airtime_factor = af;
  p.rx_delay_base = rx_base;
  p.tx_delay_factor = tx_f;
  p.direct_tx_delay_factor = direct_f;
  p.interference_threshold = thr;
  p.agc_reset_interval = agc;
  p.cad_enabled = cad;
  p.multi_acks = acks;
  return p;
}

// v1.18.0 role override: calcRxDelay()
int roleRxDelay(const RadioPrefsFields& prefs, float score, uint32_t air_time) {
  if (prefs.rx_delay_base <= 0.0f) return 0;
  return (int)((pow(prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

// v1.18.0 role override: getRetransmitDelay() / getDirectRetransmitDelay()
uint32_t roleRetransmitDelay(mesh::Radio& radio, mesh::RNG& rng, const mesh::Packet& pkt, float factor) {
  uint32_t t = (radio.getEstAirtimeFor(pkt.getPathByteLen() + pkt.payload_len + 2) * factor);
  return rng.nextInt(0, 5*t + 1);
}

const RadioPrefsFields kPrefsSets[] = {
  makePrefs(1.0f, 0.0f, 0.5f, 0.2f, 0, 0, 0, 0),     // typical repeater defaults
  makePrefs(2.0f, 3.25f, 0.75f, 0.3f, 14, 5, 1, 1),
  makePrefs(9.0f, 20.0f, 2.0f, 2.0f, 255, 255, 1, 2),
  makePrefs(0.0f, -1.0f, 0.0f, 0.0f, 1, 1, 0, 0),
};

}  // namespace

TEST(CoreTuningWithPrefs, DutyCycleInterferenceCadAgcAndAcks) {
  for (const RadioPrefsFields& prefs : kPrefsSets) {
    Harness h;
    h.mesh.useRadioPrefs(&prefs);
    EXPECT_FLOAT_EQ(prefs.airtime_factor, h.mesh.getAirtimeBudgetFactor());
    EXPECT_EQ((int)prefs.interference_threshold, h.mesh.getInterferenceThreshold());
    EXPECT_EQ((bool)prefs.cad_enabled, h.mesh.getCADEnabled());
    EXPECT_EQ(((int)prefs.agc_reset_interval) * 4000, h.mesh.getAGCResetInterval());
    EXPECT_EQ(prefs.multi_acks, h.mesh.getExtraAckTransmitCount());
  }
}

TEST(CoreTuningWithPrefs, RxDelayMatchesTheRoleFormula) {
  for (const RadioPrefsFields& prefs : kPrefsSets) {
    Harness h;
    h.mesh.useRadioPrefs(&prefs);
    for (float score : {0.0f, 0.3f, 0.85f, 1.0f}) {
      for (uint32_t air : {0u, 100u, 1234u, 60000u}) {
        EXPECT_EQ(roleRxDelay(prefs, score, air), h.mesh.calcRxDelay(score, air))
            << "rx_delay_base " << prefs.rx_delay_base << " score " << score << " air " << air;
      }
    }
  }
}

TEST(CoreTuningWithPrefs, RetransmitDelaysMatchTheRoleFormula) {
  for (mesh::Packet pkt : makePackets()) {
    for (const RadioPrefsFields& prefs : kPrefsSets) {
      Harness h;
      h.mesh.useRadioPrefs(&prefs);
      SequenceRNG expected_rng(kRandom);
      for (size_t i = 0; i < kRandom.size(); i++) {
        EXPECT_EQ(roleRetransmitDelay(h.radio, expected_rng, pkt, prefs.tx_delay_factor), h.mesh.getRetransmitDelay(&pkt));
        EXPECT_EQ(roleRetransmitDelay(h.radio, expected_rng, pkt, prefs.direct_tx_delay_factor), h.mesh.getDirectRetransmitDelay(&pkt));
      }
    }
  }
}

// The roles read their prefs on every call, so a CLI change applied immediately.
// The core keeps that: it holds a pointer, not a copy.
TEST(CoreTuningWithPrefs, ChangesApplyImmediately) {
  RadioPrefsFields prefs = kPrefsSets[0];
  Harness h;
  h.mesh.useRadioPrefs(&prefs);
  EXPECT_FALSE(h.mesh.getCADEnabled());
  prefs.cad_enabled = 1;
  prefs.airtime_factor = 4.0f;
  prefs.multi_acks = 1;
  EXPECT_TRUE(h.mesh.getCADEnabled());
  EXPECT_FLOAT_EQ(4.0f, h.mesh.getAirtimeBudgetFactor());
  EXPECT_EQ(1, h.mesh.getExtraAckTransmitCount());
}

// Detaching the settings returns to the built-in defaults.
TEST(CoreTuningWithPrefs, DetachingRestoresTheDefaults) {
  RadioPrefsFields prefs = kPrefsSets[1];
  Harness h;
  h.mesh.useRadioPrefs(&prefs);
  h.mesh.useRadioPrefs(NULL);
  EXPECT_FLOAT_EQ(1.0f, h.mesh.getAirtimeBudgetFactor());
  EXPECT_EQ(6079, h.mesh.calcRxDelay(0.0f, 1000));
  EXPECT_FALSE(h.mesh.getCADEnabled());
  EXPECT_EQ(0, h.mesh.getExtraAckTransmitCount());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
