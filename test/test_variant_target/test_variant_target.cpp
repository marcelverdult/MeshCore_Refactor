#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>
#include "target.h"

// What a board variant's target.cpp provides to the roles, pinned on the host.
// 87 of the 89 variants create a new identity with the same two lines, and the
// identity is the node's address in the mesh, so the entropy it is made from
// matters: it has to come from the LoRa radio's noise, mixed with Arduino's
// random(), exactly as RadioNoiseListener does it.

// Stand-in for src/Identity.cpp, which needs the Ed25519 library. The real
// constructor draws SEED_SIZE bytes from the RNG and derives both keys from
// them. Keeping the seed itself as the public key lets a test see exactly what
// was drawn, which is the only input the real key derivation has.
mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
mesh::LocalIdentity::LocalIdentity(mesh::RNG* rng) { rng->random(pub_key, SEED_SIZE); }

// The globals a variant's target.cpp defines. The shared definitions must
// reach the radio through radio_driver, so a second radio that nothing wraps
// shows whether they pick the right one.
TestRadio radio;
TestRadio other_radio;
TestBoard board;
WRAPPER_CLASS radio_driver(radio, board);
TestRTCClock rtc_clock;

namespace {

// The body of radio_new_identity() as 87 variants' target.cpp carry it, verbatim
// apart from the parameter: there `radio` is the variant's global radio.
mesh::LocalIdentity variant_copy(PhysicalLayer& radio) {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

// The seed RadioNoiseListener draws: each byte is the radio's random byte XOR
// the low byte of Arduino's random(0, 256), in order.
void expected_seed(uint8_t* seed, uint8_t radio_byte, long random_seed) {
  randomSeed(random_seed);
  for (int i = 0; i < SEED_SIZE; i++) {
    seed[i] = radio_byte ^ (random(0, 256) & 0xFF);
  }
}

TEST(VariantTarget, VariantCopySeedsFromTheRadioAndArduinoRandom) {
  PhysicalLayer radio;
  radio.random_byte = 0xC3;
  randomSeed(7);
  mesh::LocalIdentity id = variant_copy(radio);

  uint8_t seed[SEED_SIZE];
  expected_seed(seed, 0xC3, 7);
  EXPECT_EQ(0, memcmp(seed, id.pub_key, SEED_SIZE));
}

TEST(VariantTarget, VariantCopyDependsOnTheRadio) {
  // Same random() sequence, two radios: the identities must differ, so the
  // radio's entropy really goes into the seed.
  PhysicalLayer a, b;
  a.random_byte = 0x11;
  b.random_byte = 0x22;
  randomSeed(3);
  mesh::LocalIdentity from_a = variant_copy(a);
  randomSeed(3);
  mesh::LocalIdentity from_b = variant_copy(b);
  EXPECT_NE(0, memcmp(from_a.pub_key, from_b.pub_key, SEED_SIZE));
}

TEST(VariantTarget, SharedDefaultMatchesTheVariantCopy) {
  // The definition in src/helpers/radiolib/RadioLibTarget.cpp, which the
  // variants no longer repeat, draws the same seed from the same radio.
  radio.random_byte = 0xA7;
  randomSeed(11);
  mesh::LocalIdentity shared = radio_new_identity();
  randomSeed(11);
  mesh::LocalIdentity copy = variant_copy(radio);
  EXPECT_EQ(0, memcmp(copy.pub_key, shared.pub_key, SEED_SIZE));
}

TEST(VariantTarget, SharedDefaultListensOnTheWrappedRadio) {
  radio.random_byte = 0x01;
  other_radio.random_byte = 0xFE;
  randomSeed(5);
  mesh::LocalIdentity shared = radio_new_identity();

  uint8_t seed[SEED_SIZE];
  expected_seed(seed, 0x01, 5);
  EXPECT_EQ(0, memcmp(seed, shared.pub_key, SEED_SIZE));
  expected_seed(seed, 0xFE, 5);
  EXPECT_NE(0, memcmp(seed, shared.pub_key, SEED_SIZE));
}

TEST(VariantTarget, ListenerOnTheDriverMatchesListenerOnTheRadio) {
  radio.random_byte = 0x3C;
  uint8_t via_radio[16], via_driver[16];
  randomSeed(9);
  RadioNoiseListener(radio).random(via_radio, sizeof(via_radio));
  randomSeed(9);
  RadioNoiseListener(radio_driver).random(via_driver, sizeof(via_driver));
  EXPECT_EQ(0, memcmp(via_radio, via_driver, sizeof(via_radio)));
}

// The body of radio_init() as 28 nRF52 variants' target.cpp carry it, verbatim.
// The 3 others in cut C (heltec_t096, heltec_t1, heltec_t114) pass nothing
// instead of &SPI when P_LORA_SCLK is not defined, which std_init() ignores
// then; see the note, "Cut C".
bool variant_copy_radio_init() {
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

// The cases below run against each radio_init() in the list: the variants'
// copy, and from the code commit of cut C on, the shared default as well.
class RadioInit : public ::testing::TestWithParam<bool (*)()> {
protected:
  void SetUp() override {
    target_calls.clear();
    radio.std_init_result = true;
    radio.std_init_calls = 0;
    other_radio.std_init_calls = 0;
  }
};

TEST_P(RadioInit, StartsTheRtcOnWireThenTheRadioOnSpi) {
  GetParam()();
  std::vector<std::string> expected = {"rtc_clock.begin(Wire)", "std_init(&SPI)"};
  EXPECT_EQ(expected, target_calls);
}

TEST_P(RadioInit, StartsTheRadioOnce) {
  GetParam()();
  EXPECT_EQ(1, radio.std_init_calls);
}

TEST_P(RadioInit, ReturnsTrueWhenTheRadioStarts) {
  radio.std_init_result = true;
  EXPECT_TRUE(GetParam()());
}

TEST_P(RadioInit, ReturnsFalseWhenTheRadioFails) {
  // The roles stop on false ("Radio init failed"), so a failure must not be
  // turned into success.
  radio.std_init_result = false;
  EXPECT_FALSE(GetParam()());
  std::vector<std::string> expected = {"rtc_clock.begin(Wire)", "std_init(&SPI)"};
  EXPECT_EQ(expected, target_calls);
}

TEST_P(RadioInit, LeavesOtherRadiosAlone) {
  GetParam()();
  EXPECT_EQ(0, other_radio.std_init_calls);
}

INSTANTIATE_TEST_SUITE_P(VariantCopy, RadioInit, ::testing::Values(&variant_copy_radio_init));

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
