#include <gtest/gtest.h>
#include <string>
#include <stdexcept>
#include "helpers/nrf52/I2cBusClear.h"

// Open-drain bus model: a line is low if the master or a slave pulls it low.
// The interface only offers low/release, so "drive high" cannot be expressed.
struct FakeBus {
  // slave holds SDA low until it has seen this many SCL rising edges
  int slave_sda_clocks_needed = 0;
  // slave holds SCL low (clock stretching) for this many delayUs() calls after each release
  int stretch_calls = 0;
  bool scl_stuck_low = false;       // SCL shorted/held forever
  bool sda_stuck_low = false;       // SDA held forever (not a mid-byte slave)
  bool relow_after_stop = false;    // slave pulls SDA low again once a STOP was seen
  int sda_low_calls = 0;            // how often the master drove SDA low
  int stops = 0;

  bool master_sda_low = false, master_scl_low = false;
  int clocks_seen = 0;
  int stretch_left = 0;
  uint32_t elapsed_us = 0;
  std::string log;                  // sequence of S/P and edges, for assertions

  bool sda() const {
    bool slave_low = sda_stuck_low || clocks_seen < slave_sda_clocks_needed ||
                     (relow_after_stop && stops > 0);
    return !(master_sda_low || slave_low);
  }
  bool scl() const { return !(master_scl_low || scl_stuck_low || stretch_left > 0); }

  void sdaLow() { sda_low_calls++; bool was = sda(), c = scl(); master_sda_low = true; if (was && c) log += 'S'; }
  void sdaRelease() { bool was = sda(), c = scl(), drove = master_sda_low; master_sda_low = false; if (!was && sda() && c) log += 'P'; if (c && drove) stops++; }
  bool sdaRead() { return sda(); }
  void sclLow() { master_scl_low = true; }
  void sclRelease() {
    bool was = master_scl_low;
    master_scl_low = false;
    if (was) { clocks_seen++; stretch_left = stretch_calls; log += 'c'; }
  }
  bool sclRead() { return scl(); }
  void delayUs(uint32_t us) {
    elapsed_us += us;
    // a correct clear is bounded; fail instead of hanging the test run
    if (elapsed_us > 100000) throw std::runtime_error("bus clear did not terminate"); if (stretch_left > 0) stretch_left--;
  }
};

TEST(I2cBusClear, HealthyBusDoesNothing) {
  FakeBus b;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_TRUE(r.ok);
  EXPECT_FALSE(r.sda_low_at_start);
  EXPECT_EQ(0, r.pulses);
  EXPECT_EQ("", b.log);             // no clocks, no START, no STOP
}

TEST(I2cBusClear, OneClockReleasesAndEndsWithStop) {
  FakeBus b;
  b.slave_sda_clocks_needed = 1;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_TRUE(r.ok);
  EXPECT_TRUE(r.sda_low_at_start);
  EXPECT_EQ(1, r.pulses);
  EXPECT_EQ("cSP", b.log);          // one clock, then START-shape low, release = STOP
}

TEST(I2cBusClear, StopsClockingAsSoonAsSdaIsFree) {
  FakeBus b;
  b.slave_sda_clocks_needed = 5;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(5, r.pulses);
}

TEST(I2cBusClear, NineClocksSuffice) {
  FakeBus b;
  b.slave_sda_clocks_needed = 9;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(9, r.pulses);
}

TEST(I2cBusClear, SdaStuckForeverGivesUpAfterNineClocksWithoutStop) {
  FakeBus b;
  b.sda_stuck_low = true;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.sda_low_at_start);
  EXPECT_EQ(9, r.pulses);
  EXPECT_EQ(std::string(9, 'c'), b.log);   // no STOP attempted
  EXPECT_EQ(0, b.sda_low_calls);           // SDA never driven by the master
}

TEST(I2cBusClear, ReportsNotOkWhenSdaFallsAgainAfterStop) {
  FakeBus b;
  b.slave_sda_clocks_needed = 2;
  b.relow_after_stop = true;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_FALSE(r.ok);
}

TEST(I2cBusClear, SclHeldLowTouchesNothing) {
  FakeBus b;
  b.scl_stuck_low = true;
  b.slave_sda_clocks_needed = 1;
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(0, r.pulses);
  EXPECT_EQ("", b.log);
  EXPECT_LE(b.elapsed_us, 5000u);          // bounded wait, no endless loop
}

TEST(I2cBusClear, ClockStretchingSlaveIsWaitedFor) {
  FakeBus b;
  b.slave_sda_clocks_needed = 3;
  b.stretch_calls = 20;                    // 200 us of stretching per clock
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(3, r.pulses);
}

TEST(I2cBusClear, StretchBeyondLimitGivesUp) {
  FakeBus b;
  b.slave_sda_clocks_needed = 3;
  b.stretch_calls = 1000;                  // far beyond the 1 ms limit
  I2cBusClearResult r = i2cBusClear(b);
  EXPECT_FALSE(r.ok);
  EXPECT_LE(b.elapsed_us, 5000u);
}

TEST(I2cBusClear, ClockRateStaysAtOrBelow100kHz) {
  FakeBus b;
  b.slave_sda_clocks_needed = 9;
  i2cBusClear(b);
  // 9 clocks, two half periods each, plus settle and STOP: well under 1 ms beyond the settle time
  EXPECT_GE(b.elapsed_us, 9u * 2 * I2C_BUS_CLEAR_HALF_PERIOD_US);
  EXPECT_LT(b.elapsed_us, I2C_BUS_CLEAR_SETTLE_US + 1000u);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
