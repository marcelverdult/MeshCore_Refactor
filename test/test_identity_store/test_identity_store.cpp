#include <gtest/gtest.h>

#include <helpers/IdentityStore.h>

// IdentityStore is the only place that persists a node's keypair. Every role
// generates a new identity when the load at boot comes up empty and then saves
// it, so a save that reports success without having written anything costs the
// node its identity at the next boot.
//
// The mock filesystem lets a file open and then runs out of write budget, which
// is how a full or worn flash behaves.

namespace {

const size_t KEYPAIR_BYTES = PUB_KEY_SIZE + PRV_KEY_SIZE;  // 96
const size_t DISPLAY_NAME_BYTES = 32;

mesh::LocalIdentity makeIdentity() {
  mesh::LocalIdentity id;
  for (size_t i = 0; i < PUB_KEY_SIZE; i++) id.pub_key[i] = (uint8_t) (0xA0 + i);
  for (size_t i = 0; i < PRV_KEY_SIZE; i++) id.prv_key[i] = (uint8_t) (0x10 + i);
  return id;
}

class IdentityStoreSave : public ::testing::Test {
protected:
  Adafruit_LittleFS fs;
  mesh::LocalIdentity id = makeIdentity();
};

TEST_F(IdentityStoreSave, ReportsSuccessWhenTheKeypairFits) {
  fs.write_budget = KEYPAIR_BYTES;
  IdentityStore store(fs, "");

  EXPECT_TRUE(store.save("_main", id));
  ASSERT_NE(fs.contents("/_main.id"), nullptr);
  EXPECT_EQ(fs.contents("/_main.id")->size(), KEYPAIR_BYTES);
}

TEST_F(IdentityStoreSave, ReportsFailureWhenTheFileCannotBeOpened) {
  fs.fail_open = true;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
}

// The defect: the write fails, the identity is not on the medium, and the
// caller is told the save worked.
TEST_F(IdentityStoreSave, ReportsFailureWhenNoByteCanBeWritten) {
  fs.write_budget = 0;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
  EXPECT_TRUE(fs.contents("/_main.id")->empty());
}

TEST_F(IdentityStoreSave, ReportsFailureWhenThePrivateKeyIsCutOff) {
  fs.write_budget = PUB_KEY_SIZE + 1;   // public key fits, private key does not
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
  EXPECT_EQ(fs.contents("/_main.id")->size(), (size_t) PUB_KEY_SIZE + 1);
}

TEST_F(IdentityStoreSave, RoundTripsAnIdentityThroughTheFilesystem) {
  IdentityStore store(fs, "");
  ASSERT_TRUE(store.save("_main", id));

  mesh::LocalIdentity loaded;
  ASSERT_TRUE(store.load("_main", loaded));
  EXPECT_EQ(memcmp(loaded.pub_key, id.pub_key, PUB_KEY_SIZE), 0);
  EXPECT_EQ(memcmp(loaded.prv_key, id.prv_key, PRV_KEY_SIZE), 0);
}

class IdentityStoreSaveWithName : public IdentityStoreSave { };

TEST_F(IdentityStoreSaveWithName, ReportsSuccessWhenKeypairAndNameFit) {
  fs.write_budget = KEYPAIR_BYTES + DISPLAY_NAME_BYTES;
  IdentityStore store(fs, "");

  EXPECT_TRUE(store.save("_main", id, "room"));
  EXPECT_EQ(fs.contents("/_main.id")->size(), KEYPAIR_BYTES + DISPLAY_NAME_BYTES);
}

TEST_F(IdentityStoreSaveWithName, ReportsFailureWhenTheKeypairIsCutOff) {
  fs.write_budget = 0;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id, "room"));
}

// The display name is written after the keypair, so a medium that fills up in
// between leaves a file that loads as an identity but carries no name.
TEST_F(IdentityStoreSaveWithName, ReportsFailureWhenOnlyTheNameIsCutOff) {
  fs.write_budget = KEYPAIR_BYTES;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id, "room"));
  EXPECT_EQ(fs.contents("/_main.id")->size(), KEYPAIR_BYTES);
}

TEST_F(IdentityStoreSaveWithName, ReportsFailureWhenTheFileCannotBeOpened) {
  fs.fail_open = true;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id, "room"));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
