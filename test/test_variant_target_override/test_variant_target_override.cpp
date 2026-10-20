#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>
#include "target.h"

// A variant that keeps its own radio_new_identity(), as the two ESP-NOW variants
// do and as every variant in a downstream fork still does. Its definition must
// take precedence over the shared one in src/helpers/radiolib/RadioLibTarget.cpp,
// which is built into this binary as well; if the shared one were not weak, this
// suite would not link.

mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
mesh::LocalIdentity::LocalIdentity(mesh::RNG* rng) { rng->random(pub_key, SEED_SIZE); }

PhysicalLayer radio;
TestBoard board;
WRAPPER_CLASS radio_driver(radio, board);

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

namespace {

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
