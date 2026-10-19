#include <gtest/gtest.h>

#include <helpers/CommonRadioPrefs.h>

// "set af" and "set dutycycle" both write airtime_factor and both guard their
// range with float comparisons only. No ordered comparison is true for a NaN,
// so a check of the form "reject if below the minimum or above the maximum"
// cannot reject one. These cases pin that both commands refuse a value that is
// not a finite number, with any compiler and any optimisation flags, which is
// why the suite also runs with -Ofast (the flag nRF52 firmware is built with).

namespace {

class TestRadioPrefs : public CommonRadioPrefs {
protected:
  void structure() override { }

public:
  float freq = 869.525f, bw = 250.0f, airtime = 1.0f, rx_delay = 0, flood_tx = 0.5f, direct_tx = 0.3f;
  uint8_t sf = 11, cr = 5, int_thresh = 0, rx_gain = 0, tx_power = 20, hash_mode = 0, multi_acks = 0;
  uint8_t fem_rx = 0, fem_tx = 0;
  uint16_t agc_int = 0;
  bool cad = false;

  float getFreq() const override { return freq; }
  void setFreq(float f) override { freq = f; }
  float getBandwidth() const override { return bw; }
  void setBandwidth(float v) override { bw = v; }
  uint8_t getSpreadFactor() const override { return sf; }
  void setSpreadFactor(uint8_t v) override { sf = v; }
  uint8_t getCodingRate() const override { return cr; }
  void setCodingRate(uint8_t v) override { cr = v; }
  float getAirtimeFactor() const override { return airtime; }
  void setAirtimeFactor(float v) override { airtime = v; }
  bool isCadEnabled() const override { return cad; }
  void setCadEnabled(bool v) override { cad = v; }
  uint8_t getIntThresh() const override { return int_thresh; }
  void setIntThresh(uint8_t v) override { int_thresh = v; }
  uint8_t getRxGain() const override { return rx_gain; }
  void setRxGain(uint8_t v) override { rx_gain = v; }
  uint8_t getTxPower() const override { return tx_power; }
  void setTxPower(uint8_t v) override { tx_power = v; }
  float getRxDelay() const override { return rx_delay; }
  void setRxDelay(float v) override { rx_delay = v; }
  uint16_t getAgcResetInt() const override { return agc_int; }
  void setAgcResetInt(uint16_t v) override { agc_int = v; }
  uint8_t getHashMode() const override { return hash_mode; }
  void setHashMode(uint8_t v) override { hash_mode = v; }
  uint8_t getMultiAcks() const override { return multi_acks; }
  void setMultiAcks(uint8_t v) override { multi_acks = v; }
  float getFloodTxDelay() const override { return flood_tx; }
  void setFloodTxDelay(float v) override { flood_tx = v; }
  float getDirectTxDelay() const override { return direct_tx; }
  void setDirectTxDelay(float v) override { direct_tx = v; }
  uint8_t getFEMRxGain() const override { return fem_rx; }
  void setFEMRxGain(uint8_t v) override { fem_rx = v; }
  uint8_t getFEMTxGain() const override { return fem_tx; }
  void setFEMTxGain(uint8_t v) override { fem_tx = v; }
};

const char* AF_ERROR = "ERROR: af must be 0-9";
const char* DC_ERROR = "ERROR: dutycycle must be 1-100";

class RadioPrefsAirtime : public ::testing::Test {
protected:
  TestRadioPrefs p;
  char reply[160];

  const char* run(const char* command) {
    reply[0] = 0;
    EXPECT_TRUE(p.handleCommand(command, 0, reply)) << command;
    return reply;
  }
};

// Values that are not a number at all. Each one left airtime_factor as NaN
// before the bit check was added.
TEST_F(RadioPrefsAirtime, SetAfRejectsEveryNotANumberSpelling) {
  const char* cases[] = {"set af nan", "set af -nan", "set af NAN", "set af nan(0x1)"};
  for (const char* c : cases) {
    p.airtime = 1.0f;
    EXPECT_STREQ(AF_ERROR, run(c)) << c;
    EXPECT_FLOAT_EQ(1.0f, p.airtime) << c;
  }
}

TEST_F(RadioPrefsAirtime, SetAfRejectsInfinity) {
  for (const char* c : {"set af inf", "set af -inf", "set af 1e40"}) {
    p.airtime = 1.0f;
    EXPECT_STREQ(AF_ERROR, run(c)) << c;
    EXPECT_FLOAT_EQ(1.0f, p.airtime) << c;
  }
}

TEST_F(RadioPrefsAirtime, SetAfStillRejectsGarbageAndOutOfRange) {
  for (const char* c : {"set af abc", "set af ", "set af 10", "set af -1", "set af 9.01"}) {
    p.airtime = 1.0f;
    EXPECT_STREQ(AF_ERROR, run(c)) << c;
    EXPECT_FLOAT_EQ(1.0f, p.airtime) << c;
  }
}

TEST_F(RadioPrefsAirtime, SetAfStillAcceptsTheRange) {
  EXPECT_STREQ("OK", run("set af 0"));
  EXPECT_FLOAT_EQ(0.0f, p.airtime);
  EXPECT_STREQ("OK", run("set af 3.5"));
  EXPECT_FLOAT_EQ(3.5f, p.airtime);
  EXPECT_STREQ("OK", run("set af 9"));
  EXPECT_FLOAT_EQ(9.0f, p.airtime);
}

TEST_F(RadioPrefsAirtime, GetAfIsUnchanged) {
  p.airtime = 2.0f;
  EXPECT_STREQ("> 2.0", run("get af"));
}

// "set dutycycle" writes the same field through a division, so it needs the
// same guard.
TEST_F(RadioPrefsAirtime, SetDutycycleRejectsNotANumberAndInfinity) {
  for (const char* c : {"set dutycycle nan", "set dutycycle -nan", "set dutycycle inf", "set dutycycle 1e40"}) {
    p.airtime = 1.0f;
    EXPECT_STREQ(DC_ERROR, run(c)) << c;
    EXPECT_FLOAT_EQ(1.0f, p.airtime) << c;
  }
}

TEST_F(RadioPrefsAirtime, SetDutycycleStillRejectsOutOfRange) {
  for (const char* c : {"set dutycycle 0", "set dutycycle 101", "set dutycycle abc", "set dutycycle -5"}) {
    p.airtime = 1.0f;
    EXPECT_STREQ(DC_ERROR, run(c)) << c;
    EXPECT_FLOAT_EQ(1.0f, p.airtime) << c;
  }
}

// 1 percent is the strictest duty cycle the command offers and stores 99, a
// value "set af" itself would refuse. That is intended and must keep working.
TEST_F(RadioPrefsAirtime, SetDutycycleStillAcceptsTheRange) {
  EXPECT_STREQ("OK - 1.0%", run("set dutycycle 1"));
  EXPECT_FLOAT_EQ(99.0f, p.airtime);
  EXPECT_STREQ("OK - 50.0%", run("set dutycycle 50"));
  EXPECT_FLOAT_EQ(1.0f, p.airtime);
  EXPECT_STREQ("OK - 100.0%", run("set dutycycle 100"));
  EXPECT_FLOAT_EQ(0.0f, p.airtime);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
