#include <gtest/gtest.h>

#include <helpers/MillisDeadline.h>

// The firmware schedules periodic work by storing 'millis() + interval' and asking
// once per loop pass whether that deadline has passed. These tests drive exactly that
// loop, including over the two points where a 32-bit millisecond counter is awkward:
// where it passes 2^31 and where it wraps back to zero.
//
// Every value here is uint32_t on purpose. The firmware targets have a 32-bit long,
// the test host a 64-bit one, so a test written with long would measure the host
// rather than the target.

namespace {

const uint32_t SIGN_FLIP = 0x80000000u;   // 24.8 days
const uint32_t INTERVAL = 1000;
const uint32_t SPAN = 3000000;            // 3000 s of loop time, 1 ms per pass

// Reference form of the same question, as Dispatcher::millisHasNowPassed spells it.
bool signedDifferenceIdiom(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) > 0;
}

// One periodic block, driven pass by pass. Records what a caller actually cares about:
// how often it ran, the longest wait between two runs, and the longest run of
// consecutive passes that all fired (which must stay 1 for a periodic block).
struct PeriodicBlock {
  uint32_t deadline;
  uint32_t fires = 0;
  uint32_t longest_gap = 0;
  uint32_t longest_burst = 0;
  uint32_t gap = 0;
  uint32_t burst = 0;

  explicit PeriodicBlock(uint32_t start) : deadline(start) {}

  void pass(uint32_t now) {
    gap++;
    if (mesh::millisHasPassed(now, deadline)) {
      if (gap > longest_gap) longest_gap = gap;
      gap = 0;
      fires++;
      burst++;
      if (burst > longest_burst) longest_burst = burst;
      deadline = now + INTERVAL;
    } else {
      burst = 0;
    }
  }
};

PeriodicBlock runFrom(uint32_t start) {
  PeriodicBlock block(start);
  for (uint32_t i = 0; i < SPAN; i++) block.pass(start + i);
  return block;
}

}  // namespace

TEST(MillisDeadline, FiresOnlyStrictlyAfterTheDeadline) {
  EXPECT_FALSE(mesh::millisHasPassed(999, 1000));
  EXPECT_FALSE(mesh::millisHasPassed(1000, 1000));   // strict, as the shipped loops were
  EXPECT_TRUE(mesh::millisHasPassed(1001, 1000));
}

TEST(MillisDeadline, MatchesTheSignedDifferenceIdiom) {
  const uint32_t deadlines[] = {0, 1, 1000, SIGN_FLIP - 1, SIGN_FLIP, SIGN_FLIP + 1, 0xFFFFFFFFu};
  const uint32_t offsets[] = {0, 1, 2, 1000, 0x7FFFFFFEu, 0x7FFFFFFFu, SIGN_FLIP, SIGN_FLIP + 1,
                              0xFFFFFFFEu, 0xFFFFFFFFu};
  for (uint32_t deadline : deadlines) {
    for (uint32_t offset : offsets) {
      const uint32_t now = deadline + offset;
      EXPECT_EQ(signedDifferenceIdiom(now, deadline), mesh::millisHasPassed(now, deadline))
          << "now=" << now << " deadline=" << deadline;
    }
  }
}

TEST(MillisDeadline, TreatsADeadlineThatWrappedAsStillAhead) {
  // The last scheduling before the counter wraps: millis() is 256 short of 2^32 and
  // the stored deadline has already wrapped to 500, so it lies 756 ms in the future.
  const uint32_t now = 0xFFFFFF00u;
  const uint32_t wrapped_deadline = 500;

  EXPECT_FALSE(mesh::millisHasPassed(now, wrapped_deadline));
  EXPECT_EQ(-756, (int32_t)(now - wrapped_deadline));   // 756 ms still to wait
  EXPECT_TRUE(mesh::millisHasPassed(now + 757, wrapped_deadline));
}

TEST(MillisDeadline, PeriodIsUnchangedByTheSignFlipAndTheWrap) {
  const PeriodicBlock quiet = runFrom(0x10000000u);          // nowhere near a boundary
  const PeriodicBlock sign = runFrom(SIGN_FLIP - SPAN / 2);  // across 2^31
  const PeriodicBlock wrap = runFrom(0u - SPAN / 2);         // across the counter wrap

  EXPECT_EQ(INTERVAL + 1, quiet.longest_gap);   // strict compare, so one pass later
  EXPECT_EQ(1u, quiet.longest_burst);           // a periodic block never fires twice in a row

  EXPECT_EQ(quiet.fires, sign.fires);
  EXPECT_EQ(quiet.longest_gap, sign.longest_gap);
  EXPECT_EQ(quiet.longest_burst, sign.longest_burst);

  EXPECT_EQ(quiet.fires, wrap.fires);
  EXPECT_EQ(quiet.longest_gap, wrap.longest_gap);
  EXPECT_EQ(quiet.longest_burst, wrap.longest_burst);
}

TEST(MillisDeadline, LooksAheadHalfTheCounterAtMost) {
  // Documented horizon: a deadline up to 2^31 - 1 ahead reads as future, beyond that
  // the difference can no longer tell future from past. Every interval the firmware
  // schedules is far inside that horizon.
  const uint32_t now = 0;
  EXPECT_TRUE(mesh::millisHasPassed(now, now - 0x7FFFFFFFu));   // 2^31 - 1 in the past
  EXPECT_FALSE(mesh::millisHasPassed(now, now - SIGN_FLIP));    // exactly 2^31, reads as future
  EXPECT_FALSE(mesh::millisHasPassed(now, now + 1));
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
