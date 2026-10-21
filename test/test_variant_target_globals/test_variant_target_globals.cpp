#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/ArduinoHelpers.h>
#include "target.h"

// The radio, wrapper and RTC globals of a variant's target.cpp, pinned on the
// host: which RadioLib Module constructor the radio is built with (pins and SPI
// bus), what radio_driver wraps, and which clock rtc_clock falls back to. See
// docs/refactoring/0011-variant-target.md, "Cut D".

// Stand-ins for src/Identity.cpp (Ed25519) and for the out-of-line members of
// AutoDiscoverRTCClock (the RTC chip libraries). The real getCurrentTime() and
// setCurrentTime() use the fallback clock when no RTC chip answered, which is
// the case these tests look at; begin() is recorded as the variant stand-in's
// RTC clock does.
mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
mesh::LocalIdentity::LocalIdentity(mesh::RNG* rng) { rng->random(pub_key, SEED_SIZE); }
void AutoDiscoverRTCClock::begin(TwoWire& wire) {
  target_calls.push_back(&wire == &Wire ? "rtc_clock.begin(Wire)" : "rtc_clock.begin(other)");
}
uint32_t AutoDiscoverRTCClock::getCurrentTime() { return _fallback->getCurrentTime(); }
void AutoDiscoverRTCClock::setCurrentTime(uint32_t time) { _fallback->setCurrentTime(time); }

TestBoard board;

// No target.h declares the fallback clock; the test needs it to see where
// rtc_clock falls back to.
extern VolatileRTCClock fallback_clock;

// The same block once more, as the reference the cases are checked against on
// every commit, whatever defines the globals above.
namespace copy {
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
}  // namespace copy

namespace {

struct Globals {
  const char* name;
  TestRadioWrapper* driver;
  AutoDiscoverRTCClock* rtc;
  VolatileRTCClock* fallback;
};

std::string name_of(const ::testing::TestParamInfo<Globals>& info) { return info.param.name; }

class RadioGlobals : public ::testing::TestWithParam<Globals> {
protected:
  TestRadio* radio() { return static_cast<TestRadio*>(GetParam().driver->wrapped_radio()); }
};

TEST_P(RadioGlobals, RadioIsBuiltFromAModule) {
  ASSERT_NE(nullptr, radio());
  EXPECT_NE(nullptr, radio()->module);
}

TEST_P(RadioGlobals, ModuleHasTheBoardPinsInSx126xOrder) {
  ASSERT_NE(nullptr, radio()->module);
  EXPECT_EQ((uint32_t)P_LORA_NSS, radio()->module->cs);
  EXPECT_EQ((uint32_t)P_LORA_DIO_1, radio()->module->irq);
  EXPECT_EQ((uint32_t)P_LORA_RESET, radio()->module->rst);
  EXPECT_EQ((uint32_t)P_LORA_BUSY, radio()->module->gpio);
}

TEST_P(RadioGlobals, ModuleIsOnTheDefaultSpiBusPassedExplicitly) {
  // Passed, not left to RadioLib: with the bus as argument RadioLib does not
  // start it itself, std_init() does.
  ASSERT_NE(nullptr, radio()->module);
  EXPECT_EQ(&SPI, radio()->module->spi);
}

TEST_P(RadioGlobals, DriverWrapsTheBoard) {
  EXPECT_EQ(&board, GetParam().driver->wrapped_board());
}

TEST_P(RadioGlobals, RtcClockFallsBackToTheFallbackClock) {
  GetParam().fallback->setCurrentTime(1234567);
  EXPECT_EQ(1234567u, GetParam().rtc->getCurrentTime());
  GetParam().rtc->setCurrentTime(7654321);
  EXPECT_EQ(7654321u, GetParam().fallback->getCurrentTime());
}

TEST_P(RadioGlobals, FallbackClockCountsMillisWhenTheRtcClockTicks) {
  // What VolatileRTCClock does, and AutoDiscoverRTCClock::tick() forwards to it.
  GetParam().fallback->setCurrentTime(1000);
  g_mock_millis += 4000;
  GetParam().rtc->tick();
  EXPECT_EQ(1004u, GetParam().fallback->getCurrentTime());
}

INSTANTIATE_TEST_SUITE_P(Copy, RadioGlobals,
  ::testing::Values(Globals{"Copy", &copy::radio_driver, &copy::rtc_clock, &copy::fallback_clock}), name_of);

// The globals the variant gets, whoever defines them.
INSTANTIATE_TEST_SUITE_P(Variant, RadioGlobals,
  ::testing::Values(Globals{"Variant", &radio_driver, &rtc_clock, &fallback_clock}), name_of);

TEST(RadioGlobalsBoot, RadioInitStartsTheRtcThenTheWrappedRadioOnSpi) {
  // Cut C's shared radio_init() on these globals: the boot path of the 31.
  target_calls.clear();
  TestRadio* r = static_cast<TestRadio*>(radio_driver.wrapped_radio());
  r->std_init_calls = 0;
  r->std_init_result = true;
  EXPECT_TRUE(radio_init());
  std::vector<std::string> expected = {"rtc_clock.begin(Wire)", "std_init(&SPI)"};
  EXPECT_EQ(expected, target_calls);
  EXPECT_EQ(1, r->std_init_calls);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
