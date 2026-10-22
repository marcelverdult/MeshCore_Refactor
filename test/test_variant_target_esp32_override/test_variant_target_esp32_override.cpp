#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>
#include "target.h"

// An ESP32 variant that takes its globals from RadioLibTarget.cpp but keeps its
// own radio_init(), as a downstream fork's variant may. Its definition must take
// precedence over the shared default, which is built into this binary as well;
// if the default were not weak, this suite would not link.

mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
mesh::LocalIdentity::LocalIdentity(mesh::RNG* rng) { rng->random(pub_key, SEED_SIZE); }
void AutoDiscoverRTCClock::begin(TwoWire&) { target_calls.push_back("rtc_clock.begin(Wire)"); }
uint32_t AutoDiscoverRTCClock::getCurrentTime() { return _fallback->getCurrentTime(); }
void AutoDiscoverRTCClock::setCurrentTime(uint32_t time) { _fallback->setCurrentTime(time); }

TestBoard board;

bool radio_init() {
  target_calls.push_back("variant radio_init()");
  return false;
}

namespace {

TEST(VariantTargetEsp32Override, VariantRadioInitTakesPrecedence) {
  target_calls.clear();
  EXPECT_FALSE(radio_init());
  std::vector<std::string> expected = {"variant radio_init()"};
  EXPECT_EQ(expected, target_calls);
  EXPECT_EQ(0, static_cast<TestRadio*>(radio_driver.wrapped_radio())->std_init_calls);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
