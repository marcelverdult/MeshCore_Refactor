#include <gtest/gtest.h>

#include <helpers/radiolib/RadioLibWrappers.h>

// The packet score drives the flood rebroadcast delay (Dispatcher::calcRxDelay).
// It depends on the spreading factor, because the SNR a packet needs to be
// decodable rises as the spreading factor falls. These tests pin that the score
// a wrapper reports is the score for the spreading factor the radio reports,
// whether or not the wrapper overrides packetScore itself.

namespace {

class StubBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 4200; }
  const char* getManufacturerName() const override { return "stub"; }
  void reboot() override { }
  uint8_t getStartupReason() const override { return 0; }
};

// Stands in for the wrappers that report the spreading factor but leave
// packetScore to the base class (LR1110, LR1121, LR2021 do exactly this).
class ReportingWrapper : public RadioLibWrapper {
public:
  uint8_t sf = 10;

  ReportingWrapper(PhysicalLayer& r, mesh::MainBoard& b) : RadioLibWrapper(r, b) { }

  void setParams(float, float, uint8_t, uint8_t) override { }
  bool isReceivingPacket() override { return false; }
  float getCurrentRSSI() override { return -100.0f; }

  uint8_t getSpreadingFactor() const override { return sf; }

  // packetScoreInt is protected; expose it so a test can state the expected
  // value in terms of the formula under test rather than a copy of it.
  float scoreAt(float snr, int sf_, int len) { return packetScoreInt(snr, sf_, len); }
};

// Stands in for a wrapper that reports nothing, so the base class falls back
// to the compile-time LORA_SF.
class SilentWrapper : public RadioLibWrapper {
public:
  SilentWrapper(PhysicalLayer& r, mesh::MainBoard& b) : RadioLibWrapper(r, b) { }
  void setParams(float, float, uint8_t, uint8_t) override { }
  bool isReceivingPacket() override { return false; }
  float getCurrentRSSI() override { return -100.0f; }
};

struct RadioWrapperFixture : public ::testing::Test {
  PhysicalLayer radio;
  StubBoard board;
};

TEST_F(RadioWrapperFixture, ScoreFollowsTheReportedSpreadingFactor) {
  ReportingWrapper w(radio, board);
  for (uint8_t sf = 7; sf <= 12; sf++) {
    w.sf = sf;
    for (float snr : {0.0f, -5.0f, -10.0f, -15.0f, -18.0f}) {
      EXPECT_FLOAT_EQ(w.packetScore(snr, 64), w.scoreAt(snr, sf, 64))
        << "sf=" << (int)sf << " snr=" << snr;
    }
  }
}

// The old base class assumed sf=10 for every radio that did not override
// packetScore. These are the cases where that assumption is wrong: at sf=7 it
// scored too high (delay too short), at sf=12 too low (delay too long).
TEST_F(RadioWrapperFixture, DoesNotAssumeSpreadingFactorTen) {
  ReportingWrapper w(radio, board);

  w.sf = 7;
  EXPECT_NE(w.packetScore(-5.0f, 64), w.scoreAt(-5.0f, 10, 64));
  EXPECT_LT(w.packetScore(-5.0f, 64), w.scoreAt(-5.0f, 10, 64));

  w.sf = 12;
  EXPECT_NE(w.packetScore(-10.0f, 64), w.scoreAt(-10.0f, 10, 64));
  EXPECT_GT(w.packetScore(-10.0f, 64), w.scoreAt(-10.0f, 10, 64));
}

// At sf=10 old and new agree, which is the probe on the arithmetic above.
TEST_F(RadioWrapperFixture, AgreesWithTheOldAssumptionAtSpreadingFactorTen) {
  ReportingWrapper w(radio, board);
  w.sf = 10;
  for (float snr : {0.0f, -5.0f, -10.0f, -15.0f, -18.0f}) {
    EXPECT_FLOAT_EQ(w.packetScore(snr, 64), w.scoreAt(snr, 10, 64)) << "snr=" << snr;
  }
}

// A wrapper that reports no spreading factor gets the build's LORA_SF, not a
// hardcoded 10. The env builds with LORA_SF=9 so the two differ.
TEST_F(RadioWrapperFixture, FallsBackToTheBuildsSpreadingFactor) {
  SilentWrapper silent(radio, board);
  ReportingWrapper probe(radio, board);
  EXPECT_EQ(silent.getSpreadingFactor(), LORA_SF);
  EXPECT_FLOAT_EQ(silent.packetScore(-5.0f, 64), probe.scoreAt(-5.0f, LORA_SF, 64));
}

// The score also carries the length penalty, so a longer packet scores lower.
TEST_F(RadioWrapperFixture, LongerPacketsScoreLower) {
  ReportingWrapper w(radio, board);
  w.sf = 8;
  EXPECT_GT(w.packetScore(0.0f, 16), w.packetScore(0.0f, 240));
}

// Below the SNR threshold for the reported spreading factor there is no chance
// of success, so the score is zero.
TEST_F(RadioWrapperFixture, BelowThresholdScoresZero) {
  ReportingWrapper w(radio, board);
  w.sf = 7;
  EXPECT_FLOAT_EQ(w.packetScore(-8.0f, 64), 0.0f);
  w.sf = 12;
  EXPECT_FLOAT_EQ(w.packetScore(-21.0f, 64), 0.0f);
}

// Absolute values, worked out by hand from the formula, so a shifted SNR
// threshold, a dropped length penalty or a dropped clamp cannot hide behind a
// comparison of the score against itself.
//   score = clamp((snr - threshold[sf]) / 10 * (1 - len / 256), 0, 1)
TEST_F(RadioWrapperFixture, ScoreMatchesTheFormulaByHand) {
  ReportingWrapper w(radio, board);

  // sf=8 threshold -10 dB: (-5 + 10) / 10 * (1 - 64/256) = 0.5 * 0.75
  w.sf = 8;
  EXPECT_FLOAT_EQ(w.packetScore(-5.0f, 64), 0.375f);

  // sf=7 threshold -7.5 dB: (0 + 7.5) / 10 * (1 - 128/256) = 0.75 * 0.5
  w.sf = 7;
  EXPECT_FLOAT_EQ(w.packetScore(0.0f, 128), 0.375f);

  // sf=12 threshold -20 dB: (0 + 20) / 10 * 0.75 = 1.5, clamped to 1.0
  w.sf = 12;
  EXPECT_FLOAT_EQ(w.packetScore(0.0f, 64), 1.0f);

  // exactly at the threshold the score is zero, and half a dB above it the
  // formula already applies: (-14.5 + 15) / 10 * 0.75. Pinning a value this
  // close to the threshold is what keeps the threshold itself under test.
  w.sf = 10;
  EXPECT_FLOAT_EQ(w.packetScore(-15.0f, 64), 0.0f);
  EXPECT_FLOAT_EQ(w.packetScore(-14.5f, 64), 0.0375f);
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
