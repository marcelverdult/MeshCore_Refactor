#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <helpers/FemRxGain.h>
#include <helpers/KeyValueStore.h>

// The RX gain (LNA) setting of the boards whose LoRa front end module can switch
// its LNA: Heltec V4, V4 R8, T096 and Tracker V2. Their board classes carry the
// same attachDynamicPrefs() and handleCommand() (the board class name aside,
// see tools/refactoring/variant_duplication.py), which read the `fem_rxgain` key
// from the store the role attaches, answer `get/set radio.fem.rxgain` and write
// the key back. These tests pin what the CLI replies and what lands in the store,
// for a verbatim copy of those bodies and for the shared FemRxGain the boards
// forward to now.

namespace {

// The board's FEM control as far as the handlers see it (the variants'
// LoRaFEMControl), recording what the board does with it.
struct FakeFEMControl {
  bool can_control = true;
  bool lna_enabled = false;
  int rx_mode_calls = 0;

  bool isLnaCanControl() const { return can_control; }
  bool isLNAEnabled() const { return lna_enabled; }
  void setLNAEnable(bool enabled) { lna_enabled = enabled; }
  void setRxModeEnable() { rx_mode_calls++; }
};

// A key/value store that records every access. getByKey() copies the value the
// way DynamicConfigSerializer does (strncpy, then a terminator at max_len).
class RecordingStore : public KeyValueStore {
public:
  std::map<std::string, std::string> values;
  std::vector<std::pair<std::string, size_t>> reads;
  std::vector<std::pair<std::string, std::string>> writes;
  bool accept_writes = true;

  bool setByKey(const char* key, const char* value) override {
    writes.emplace_back(key, value);
    if (!accept_writes) return false;
    values[key] = value;
    return true;
  }
  bool getByKey(const char* key, char* value, size_t max_len) override {
    reads.emplace_back(key, max_len);
    auto it = values.find(key);
    if (it == values.end()) return false;
    strncpy(value, it->second.c_str(), max_len);
    value[max_len] = 0;
    return true;
  }
};

// What the four boards have around the handlers: the public FEM control and the
// two private helpers, whose bodies are the boards' own (HeltecV4Board.cpp), plus
// a switch that makes a switch of the LNA fail although it can be controlled,
// which no real board does but which reaches the handlers' error branch.
class FakeBoardBase {
public:
  FakeFEMControl loRaFEMControl;
  bool fail_set = false;
  std::vector<bool> set_calls;

protected:
  bool setLoRaFemLnaEnabled(bool enable) {
    set_calls.push_back(enable);
    if (!loRaFEMControl.isLnaCanControl()) {
      return false;
    }
    if (fail_set) {
      return false;
    }

    loRaFEMControl.setLNAEnable(enable);
    loRaFEMControl.setRxModeEnable();
    return true;
  }

  bool isLoRaFemLnaEnabled() const {
    return loRaFEMControl.isLNAEnabled();
  }
};

// The handlers as the four boards have them.
class VariantCopyBoard : public FakeBoardBase {
  KeyValueStore* _prefs = NULL;

public:
  void attachDynamicPrefs(KeyValueStore* prefs);
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply);
};

// Copied verbatim from variants/heltec_v4/HeltecV4Board.cpp, class name replaced.
void VariantCopyBoard::attachDynamicPrefs(KeyValueStore* prefs) {
  _prefs = prefs;

  char radio_fem_rxgain[8] = { 0 };
  _prefs->getByKey("fem_rxgain", radio_fem_rxgain, 7);  // get initial values

  setLoRaFemLnaEnabled(strcmp(radio_fem_rxgain, "1") == 0);
}

bool VariantCopyBoard::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  if (strcmp(command, "get radio.fem.rxgain") == 0) {
    if (!loRaFEMControl.isLnaCanControl()) {
      strcpy(reply, "Error: unsupported");
    } else {
      sprintf(reply, "> %s", isLoRaFemLnaEnabled() ? "on" : "off");
    }
    return true;
  }
  if (memcmp(command, "set radio.fem.rxgain ", 21) == 0) {
    if (!loRaFEMControl.isLnaCanControl()) {
      strcpy(reply, "Error: unsupported");
    } else if (memcmp(&command[21], "on", 2) == 0) {
      if (setLoRaFemLnaEnabled(true)) {
        _prefs->setByKey("fem_rxgain", "1");
        strcpy(reply, "OK - LoRa FEM RX gain on");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else if (memcmp(&command[21], "off", 3) == 0) {
      if (setLoRaFemLnaEnabled(false)) {
        _prefs->setByKey("fem_rxgain", "0");
        strcpy(reply, "OK - LoRa FEM RX gain off");
      } else {
        strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
      }
    } else {
      strcpy(reply, "Error: state must be on or off");
    }
    return true;
  }

  return false; // not handled
}

// A board as the four are now: the handlers forward to the shared FemRxGain,
// which reaches the private helpers as a friend.
class SharedBoard : public FakeBoardBase {
  friend class FemRxGain<SharedBoard>;
  FemRxGain<SharedBoard> fem_rxgain;

public:
  void attachDynamicPrefs(KeyValueStore* prefs) { fem_rxgain.attachPrefs(*this, prefs); }
  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
    return fem_rxgain.handleCommand(*this, command, reply);
  }
};

template <class BOARD>
class FemRxGainTest : public ::testing::Test {
protected:
  BOARD board;
  RecordingStore store;
  char reply[160];

  // Runs a CLI command the way CommonCLI hands it over: in a zero filled buffer,
  // so the handlers' fixed length compares stay inside it.
  bool run(const char* command) {
    char buf[80] = { 0 };
    strcpy(buf, command);
    strcpy(reply, "untouched");
    return board.handleCommand(buf, 1234, reply);
  }

  void attach(const char* stored) {
    if (stored) store.values["fem_rxgain"] = stored;
    board.attachDynamicPrefs(&store);
    board.set_calls.clear();
    store.reads.clear();
  }
};

using Implementations = ::testing::Types<VariantCopyBoard, SharedBoard>;
TYPED_TEST_SUITE(FemRxGainTest, Implementations);

TYPED_TEST(FemRxGainTest, AttachReadsTheKeyOnceAndSwitchesTheLnaOn) {
  this->store.values["fem_rxgain"] = "1";
  this->board.attachDynamicPrefs(&this->store);

  ASSERT_EQ(this->store.reads.size(), 1u);
  EXPECT_EQ(this->store.reads[0].first, "fem_rxgain");
  EXPECT_EQ(this->store.reads[0].second, 7u);
  EXPECT_EQ(this->board.set_calls, std::vector<bool>{ true });
  EXPECT_TRUE(this->board.loRaFEMControl.lna_enabled);
  EXPECT_EQ(this->board.loRaFEMControl.rx_mode_calls, 1);
  EXPECT_TRUE(this->store.writes.empty());
}

TYPED_TEST(FemRxGainTest, AttachSwitchesTheLnaOffForAnyOtherValue) {
  for (const char* stored : { "0", "", "on", "10", " 1", "1 ", "true" }) {
    TypeParam board;
    board.loRaFEMControl.lna_enabled = true;
    RecordingStore store;
    store.values["fem_rxgain"] = stored;
    board.attachDynamicPrefs(&store);

    EXPECT_EQ(board.set_calls, std::vector<bool>{ false }) << "stored: '" << stored << "'";
    EXPECT_FALSE(board.loRaFEMControl.lna_enabled) << "stored: '" << stored << "'";
    EXPECT_TRUE(store.writes.empty());
  }
}

TYPED_TEST(FemRxGainTest, AttachSwitchesTheLnaOffWhenTheKeyIsMissing) {
  this->board.loRaFEMControl.lna_enabled = true;
  this->board.attachDynamicPrefs(&this->store);

  EXPECT_EQ(this->store.reads.size(), 1u);
  EXPECT_EQ(this->board.set_calls, std::vector<bool>{ false });
  EXPECT_FALSE(this->board.loRaFEMControl.lna_enabled);
}

TYPED_TEST(FemRxGainTest, AttachReadsOnlySevenCharacters) {
  // "1" followed by spaces is cut to seven characters and is still not "1".
  this->store.values["fem_rxgain"] = "1       ";
  this->board.attachDynamicPrefs(&this->store);
  EXPECT_EQ(this->board.set_calls, std::vector<bool>{ false });
}

TYPED_TEST(FemRxGainTest, AttachTriesTheLnaEvenWhenItCannotBeControlled) {
  this->board.loRaFEMControl.can_control = false;
  this->store.values["fem_rxgain"] = "1";
  this->board.attachDynamicPrefs(&this->store);

  EXPECT_EQ(this->board.set_calls, std::vector<bool>{ true });
  EXPECT_FALSE(this->board.loRaFEMControl.lna_enabled);
  EXPECT_EQ(this->board.loRaFEMControl.rx_mode_calls, 0);
  EXPECT_TRUE(this->store.writes.empty());
}

TYPED_TEST(FemRxGainTest, GetReportsTheLnaState) {
  this->attach("1");
  EXPECT_TRUE(this->run("get radio.fem.rxgain"));
  EXPECT_STREQ(this->reply, "> on");

  this->board.loRaFEMControl.lna_enabled = false;
  EXPECT_TRUE(this->run("get radio.fem.rxgain"));
  EXPECT_STREQ(this->reply, "> off");

  EXPECT_TRUE(this->board.set_calls.empty());
  EXPECT_TRUE(this->store.reads.empty());
  EXPECT_TRUE(this->store.writes.empty());
}

TYPED_TEST(FemRxGainTest, GetIsUnsupportedWhenTheLnaCannotBeControlled) {
  this->attach("1");
  this->board.loRaFEMControl.can_control = false;
  EXPECT_TRUE(this->run("get radio.fem.rxgain"));
  EXPECT_STREQ(this->reply, "Error: unsupported");
}

TYPED_TEST(FemRxGainTest, SetOnSwitchesTheLnaAndStoresOne) {
  this->attach("0");
  EXPECT_TRUE(this->run("set radio.fem.rxgain on"));

  EXPECT_STREQ(this->reply, "OK - LoRa FEM RX gain on");
  EXPECT_EQ(this->board.set_calls, std::vector<bool>{ true });
  EXPECT_TRUE(this->board.loRaFEMControl.lna_enabled);
  ASSERT_EQ(this->store.writes.size(), 1u);
  EXPECT_EQ(this->store.writes[0], std::make_pair(std::string("fem_rxgain"), std::string("1")));
  EXPECT_TRUE(this->store.reads.empty());
}

TYPED_TEST(FemRxGainTest, SetOffSwitchesTheLnaAndStoresZero) {
  this->attach("1");
  EXPECT_TRUE(this->run("set radio.fem.rxgain off"));

  EXPECT_STREQ(this->reply, "OK - LoRa FEM RX gain off");
  EXPECT_EQ(this->board.set_calls, std::vector<bool>{ false });
  EXPECT_FALSE(this->board.loRaFEMControl.lna_enabled);
  ASSERT_EQ(this->store.writes.size(), 1u);
  EXPECT_EQ(this->store.writes[0], std::make_pair(std::string("fem_rxgain"), std::string("0")));
}

TYPED_TEST(FemRxGainTest, SetMatchesOnlyTheStartOfTheState) {
  this->attach("0");
  EXPECT_TRUE(this->run("set radio.fem.rxgain onward"));
  EXPECT_STREQ(this->reply, "OK - LoRa FEM RX gain on");
  EXPECT_TRUE(this->run("set radio.fem.rxgain offline"));
  EXPECT_STREQ(this->reply, "OK - LoRa FEM RX gain off");
  EXPECT_EQ(this->board.set_calls, (std::vector<bool>{ true, false }));
  EXPECT_EQ(this->store.writes.size(), 2u);
}

TYPED_TEST(FemRxGainTest, SetRejectsAnyOtherState) {
  this->attach("0");
  for (const char* command : { "set radio.fem.rxgain 1", "set radio.fem.rxgain 0",
                               "set radio.fem.rxgain  on", "set radio.fem.rxgain of",
                               "set radio.fem.rxgain ON", "set radio.fem.rxgain o",
                               "set radio.fem.rxgain " }) {
    EXPECT_TRUE(this->run(command)) << command;
    EXPECT_STREQ(this->reply, "Error: state must be on or off") << command;
  }
  EXPECT_TRUE(this->board.set_calls.empty());
  EXPECT_TRUE(this->store.writes.empty());
}

TYPED_TEST(FemRxGainTest, SetIsUnsupportedWhenTheLnaCannotBeControlled) {
  this->attach("0");
  this->board.loRaFEMControl.can_control = false;
  for (const char* command : { "set radio.fem.rxgain on", "set radio.fem.rxgain off",
                               "set radio.fem.rxgain x" }) {
    EXPECT_TRUE(this->run(command)) << command;
    EXPECT_STREQ(this->reply, "Error: unsupported") << command;
  }
  EXPECT_TRUE(this->board.set_calls.empty());
  EXPECT_TRUE(this->store.writes.empty());
}

TYPED_TEST(FemRxGainTest, SetReportsAFailedSwitchAndStoresNothing) {
  this->attach("0");
  this->board.fail_set = true;
  EXPECT_TRUE(this->run("set radio.fem.rxgain on"));
  EXPECT_STREQ(this->reply, "Error: failed to apply LoRa FEM RX gain");
  EXPECT_TRUE(this->run("set radio.fem.rxgain off"));
  EXPECT_STREQ(this->reply, "Error: failed to apply LoRa FEM RX gain");
  EXPECT_EQ(this->board.set_calls, (std::vector<bool>{ true, false }));
  EXPECT_TRUE(this->store.writes.empty());
}

TYPED_TEST(FemRxGainTest, SetIgnoresWhetherTheStoreTookTheValue) {
  this->attach("0");
  this->store.accept_writes = false;
  EXPECT_TRUE(this->run("set radio.fem.rxgain on"));
  EXPECT_STREQ(this->reply, "OK - LoRa FEM RX gain on");
  EXPECT_EQ(this->store.writes.size(), 1u);
}

TYPED_TEST(FemRxGainTest, SetWritesToTheStoreAttachedLast) {
  RecordingStore first;
  this->board.attachDynamicPrefs(&first);
  this->board.attachDynamicPrefs(&this->store);
  EXPECT_TRUE(this->run("set radio.fem.rxgain on"));
  EXPECT_TRUE(first.writes.empty());
  EXPECT_EQ(this->store.writes.size(), 1u);
}

TYPED_TEST(FemRxGainTest, OtherCommandsAreNotHandled) {
  this->attach("1");
  for (const char* command : { "get radio.fem.rxgain ", "get radio.fem.rxgain on",
                               "set radio.fem.rxgain", "set radio.fem.rxgainon",
                               "get radio.rxgain", "set radio.fem.txgain on",
                               "SET radio.fem.rxgain on", "", "ver" }) {
    EXPECT_FALSE(this->run(command)) << command;
    EXPECT_STREQ(this->reply, "untouched") << command;
  }
  EXPECT_TRUE(this->board.set_calls.empty());
  EXPECT_TRUE(this->store.reads.empty());
  EXPECT_TRUE(this->store.writes.empty());
}

// Without a store the boards' copies dereference NULL on `set` (the change
// meshcore-dev/MeshCore#3496 makes to them); the shared code stores nothing and
// replies as with a store. No role in this tree calls handleCommand() before it
// attaches the store, so this case is new, not changed.
TEST(FemRxGainWithoutStore, SetSwitchesTheLnaAndStoresNothing) {
  SharedBoard board;
  char reply[160];
  char on[80] = "set radio.fem.rxgain on";
  char off[80] = "set radio.fem.rxgain off";
  char get[80] = "get radio.fem.rxgain";

  EXPECT_TRUE(board.handleCommand(on, 0, reply));
  EXPECT_STREQ(reply, "OK - LoRa FEM RX gain on");
  EXPECT_TRUE(board.loRaFEMControl.lna_enabled);
  EXPECT_TRUE(board.handleCommand(get, 0, reply));
  EXPECT_STREQ(reply, "> on");
  EXPECT_TRUE(board.handleCommand(off, 0, reply));
  EXPECT_STREQ(reply, "OK - LoRa FEM RX gain off");
  EXPECT_FALSE(board.loRaFEMControl.lna_enabled);
  EXPECT_EQ(board.set_calls, (std::vector<bool>{ true, false }));
}

// The shared object holds nothing but the store pointer, so the boards keep
// their size and layout (the pointer replaces their own `_prefs`).
TEST(FemRxGainWithoutStore, IsTheSizeOfAPointer) {
  EXPECT_EQ(sizeof(FemRxGain<SharedBoard>), sizeof(KeyValueStore*));
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
