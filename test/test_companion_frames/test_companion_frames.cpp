// The companion's frame protocol on the host: the real MyMesh.cpp, driven through
// handleCmdFrame() the way the BLE, USB and WiFi interfaces drive it. The suites
// pin the protocol before handleCmdFrame() is restructured
// (docs/refactoring/0010-companion-command-table.md):
//
// - test_frame_sweep.cpp: every code at every length, against a recorded transcript
// - test_commands.cpp: what each command does with a valid frame, and where its
//   minimum length is
// - test_frame_bounds.cpp: that no command reads the bytes a previous frame left
//   above the end of the current one (step 10c)

#include <gtest/gtest.h>

#include <Identity.h>
#include <cstring>

// A variant's target.cpp creates the identity from the board's RNG. Here it is a
// fixed key pair, so the node's public key, and every reply that carries it, is the
// same on every run. The first byte is neither 00 nor FF, which begin() rejects.
mesh::LocalIdentity radio_new_identity() {
  uint8_t keys[PRV_KEY_SIZE + PUB_KEY_SIZE];
  for (int i = 0; i < PRV_KEY_SIZE; i++) keys[i] = (uint8_t)(0x40 + i);
  for (int i = 0; i < PUB_KEY_SIZE; i++) keys[PRV_KEY_SIZE + i] = (uint8_t)(0xA0 + i);
  mesh::LocalIdentity id;
  id.readFrom(keys, sizeof(keys));
  return id;
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
