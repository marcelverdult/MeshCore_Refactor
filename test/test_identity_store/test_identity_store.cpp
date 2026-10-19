#include <gtest/gtest.h>

#include <helpers/IdentityStore.h>

// IdentityStore is the only place that persists a node's keypair. Every role
// generates a new identity when the load at boot comes up empty and then saves
// it, so a save that reports success without having written anything costs the
// node its identity at the next boot.
//
// The mock filesystem reproduces both ways a medium refuses data: a write that
// comes up short, which is what littlefs does on nRF52 and STM32, and a write
// that reports success while only the close hands the bytes over, which is what
// the buffered ESP32 file does.

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

// The second failure mode, and the one that matters on ESP32: every write
// reports success because the bytes are still in a buffer, and the medium only
// sees them when the file is closed. File::close() is void, so nothing in the
// write path can report this. Only reading the file back afterwards can.

TEST_F(IdentityStoreSave, ReportsFailureWhenTheCommitLosesEverything) {
  fs.commit_budget = 0;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
  EXPECT_TRUE(fs.contents("/_main.id")->empty());
}

TEST_F(IdentityStoreSave, ReportsFailureWhenTheCommitKeepsOnlyThePublicKey) {
  fs.commit_budget = PUB_KEY_SIZE;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
  EXPECT_EQ(fs.contents("/_main.id")->size(), (size_t) PUB_KEY_SIZE);
}

// A worn cell that reads back a different byte keeps the file length intact, so
// only comparing the content catches it. This index is inside the private key,
// where a comparison of the public key alone would look away.
TEST_F(IdentityStoreSave, ReportsFailureWhenAByteOfThePrivateKeyComesBackWrong) {
  fs.corrupt_on_commit = PUB_KEY_SIZE + 20;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
  EXPECT_EQ(fs.contents("/_main.id")->size(), KEYPAIR_BYTES);  // the length is right
}

TEST_F(IdentityStoreSave, ReportsFailureWhenAByteOfThePublicKeyComesBackWrong) {
  fs.corrupt_on_commit = 3;
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id));
}

TEST_F(IdentityStoreSave, ReportsSuccessWhenTheCommitKeepsEverything) {
  fs.commit_budget = KEYPAIR_BYTES;
  IdentityStore store(fs, "");

  EXPECT_TRUE(store.save("_main", id));
}

TEST_F(IdentityStoreSaveWithName, ReportsFailureWhenTheCommitLosesTheName) {
  fs.commit_budget = KEYPAIR_BYTES;   // the keypair survives, the name does not
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id, "room"));
}

TEST_F(IdentityStoreSaveWithName, ReportsFailureWhenAByteOfTheNameComesBackWrong) {
  fs.corrupt_on_commit = KEYPAIR_BYTES + 2;   // inside "room"
  IdentityStore store(fs, "");

  EXPECT_FALSE(store.save("_main", id, "room"));
  EXPECT_EQ(fs.contents("/_main.id")->size(), KEYPAIR_BYTES + DISPLAY_NAME_BYTES);
}

TEST_F(IdentityStoreSaveWithName, ReportsSuccessWhenTheCommitKeepsEverything) {
  fs.commit_budget = KEYPAIR_BYTES + DISPLAY_NAME_BYTES;
  IdentityStore store(fs, "");

  EXPECT_TRUE(store.save("_main", id, "room"));
}

// The three cases above pick single indices. These two nail down that every
// byte of the comparison counts, which is what keeps a shortened comparison
// from passing unnoticed.
TEST_F(IdentityStoreSave, ReportsFailureForEveryByteOfTheKeypairComingBackWrong) {
  for (size_t i = 0; i < KEYPAIR_BYTES; i++) {
    Adafruit_LittleFS one_fs;
    one_fs.corrupt_on_commit = i;
    IdentityStore store(one_fs, "");

    EXPECT_FALSE(store.save("_main", id)) << "byte " << i << " went unnoticed";
  }
}

TEST_F(IdentityStoreSaveWithName, ReportsFailureForEveryByteOfTheNameComingBackWrong) {
  // Byte 31 of the name field is padding that load() forces to zero, so a
  // change there cannot be seen. It also does not matter: the name itself is
  // shorter and stays intact.
  for (size_t i = 0; i + 1 < DISPLAY_NAME_BYTES; i++) {
    Adafruit_LittleFS one_fs;
    one_fs.corrupt_on_commit = KEYPAIR_BYTES + i;
    IdentityStore store(one_fs, "");

    EXPECT_FALSE(store.save("_main", id, "room")) << "name byte " << i << " went unnoticed";
  }
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
