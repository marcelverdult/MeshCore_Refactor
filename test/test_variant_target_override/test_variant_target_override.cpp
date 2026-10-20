#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>
#include "target.h"

// A variant that keeps its own radio_new_identity() and radio_init(), as the
// ESP-NOW variants and the variants outside cut C do and as every variant in a
// downstream fork still does. Its definitions must take precedence over the
// shared ones in src/helpers/radiolib/RadioLibTarget.cpp, which are built into
// this binary as well; if a shared one were not weak, this suite would not link.

mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
mesh::LocalIdentity::LocalIdentity(mesh::RNG* rng) { rng->random(pub_key, SEED_SIZE); }

TestRadio radio;
TestBoard board;
WRAPPER_CLASS radio_driver(radio, board);
TestRTCClock rtc_clock;

namespace {

// Fills the seed with one marker byte, which no radio noise produces here.
class MarkerRNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override { memset(dest, 0xEE, sz); }
};

}  // namespace

mesh::LocalIdentity radio_new_identity() {
  MarkerRNG rng;
  return mesh::LocalIdentity(&rng);
}

bool radio_init() {
  target_calls.push_back("variant radio_init()");
  return false;
}

namespace {

TEST(VariantTargetOverride, VariantRadioInitTakesPrecedence) {
  target_calls.clear();
  EXPECT_FALSE(radio_init());
  std::vector<std::string> expected = {"variant radio_init()"};
  EXPECT_EQ(expected, target_calls);
  EXPECT_EQ(0, radio.std_init_calls);
}

TEST(VariantTargetOverride, VariantDefinitionTakesPrecedence) {
  mesh::LocalIdentity id = radio_new_identity();
  uint8_t marker[SEED_SIZE];
  memset(marker, 0xEE, sizeof(marker));
  EXPECT_EQ(0, memcmp(marker, id.pub_key, SEED_SIZE));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
