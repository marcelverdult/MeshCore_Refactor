#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>

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

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
