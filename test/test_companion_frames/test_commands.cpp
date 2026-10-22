// What each companion command does with a well formed frame, and where its minimum
// length is. The frame sweep (test_frame_sweep.cpp) pins the replies at every
// length but only with filler bytes; these tests send the frames an app sends and
// check the meaning: the fields of a reply, and the state a setter changed, read
// back through the protocol where the protocol can read it.
//
// Expectations describe the firmware as it is (the current upstream behaviour plus
// the length guards of docs/fixes/companion-cmd-frame-min-len.md and
// docs/fixes/companion-channel-txt-min-len.md), including what looks odd; such
// places say so.

#include "CompanionHarness.h"

using namespace companion;

namespace {

// Command codes, as in examples/companion_radio/MyMesh.cpp. They are wire values,
// so the tests spell them out instead of borrowing the firmware's names.
enum : uint8_t {
  APP_START = 1, SEND_TXT_MSG = 2, SEND_CHANNEL_TXT_MSG = 3, GET_CONTACTS = 4,
  GET_DEVICE_TIME = 5, SET_DEVICE_TIME = 6, SEND_SELF_ADVERT = 7, SET_ADVERT_NAME = 8,
  ADD_UPDATE_CONTACT = 9, SYNC_NEXT_MESSAGE = 10, SET_RADIO_PARAMS = 11,
  SET_RADIO_TX_POWER = 12, RESET_PATH = 13, SET_ADVERT_LATLON = 14, REMOVE_CONTACT = 15,
  SHARE_CONTACT = 16, EXPORT_CONTACT = 17, IMPORT_CONTACT = 18, REBOOT = 19,
  GET_BATT_AND_STORAGE = 20, SET_TUNING_PARAMS = 21, DEVICE_QUERY = 22,
  EXPORT_PRIVATE_KEY = 23, IMPORT_PRIVATE_KEY = 24, SEND_RAW_DATA = 25, SEND_LOGIN = 26,
  SEND_STATUS_REQ = 27, HAS_CONNECTION = 28, LOGOUT = 29, GET_CONTACT_BY_KEY = 30,
  GET_CHANNEL = 31, SET_CHANNEL = 32, SIGN_START = 33, SIGN_DATA = 34, SIGN_FINISH = 35,
  SEND_TRACE_PATH = 36, SET_DEVICE_PIN = 37, SET_OTHER_PARAMS = 38,
  SEND_TELEMETRY_REQ = 39, GET_CUSTOM_VARS = 40, SET_CUSTOM_VAR = 41,
  GET_ADVERT_PATH = 42, GET_TUNING_PARAMS = 43, SEND_BINARY_REQ = 50,
  FACTORY_RESET = 51, SEND_PATH_DISCOVERY_REQ = 52, SET_FLOOD_SCOPE_KEY = 54,
  SEND_CONTROL_DATA = 55, GET_STATS = 56, SEND_ANON_REQ = 57, SET_AUTOADD_CONFIG = 58,
  GET_AUTOADD_CONFIG = 59, GET_ALLOWED_REPEAT_FREQ = 60, SET_PATH_HASH_MODE = 61,
  SEND_CHANNEL_DATA = 62, SET_DEFAULT_FLOOD_SCOPE = 63, GET_DEFAULT_FLOOD_SCOPE = 64,
  SEND_RAW_PACKET = 65, RUN_CLI_COMMAND = 66,
};

enum : uint8_t {
  RESP_OK = 0, RESP_ERR = 1, RESP_CONTACTS_START = 2, RESP_CONTACT = 3,
  RESP_END_OF_CONTACTS = 4, RESP_SELF_INFO = 5, RESP_SENT = 6, RESP_CURR_TIME = 9,
  RESP_NO_MORE_MESSAGES = 10, RESP_EXPORT_CONTACT = 11, RESP_BATT_AND_STORAGE = 12,
  RESP_DEVICE_INFO = 13, RESP_PRIVATE_KEY = 14, RESP_CHANNEL_INFO = 18,
  RESP_SIGN_START = 19, RESP_SIGNATURE = 20, RESP_CUSTOM_VARS = 21,
  RESP_TUNING_PARAMS = 23, RESP_STATS = 24, RESP_AUTOADD_CONFIG = 25,
  RESP_ALLOWED_REPEAT_FREQ = 26, RESP_DEFAULT_FLOOD_SCOPE = 28, RESP_CLI_REPLY = 29,
  PUSH_TELEMETRY_RESPONSE = 0x8B,
};

enum : uint8_t {
  ERR_UNSUPPORTED_CMD = 1, ERR_NOT_FOUND = 2, ERR_TABLE_FULL = 3, ERR_BAD_STATE = 4,
  ERR_FILE_IO_ERROR = 5, ERR_ILLEGAL_ARG = 6,
};

const Bytes OK = {RESP_OK};
Bytes err(uint8_t code) { return {RESP_ERR, code}; }

// The node's own key, from radio_new_identity() in test_companion_frames.cpp.
Bytes selfPubKey() {
  Bytes k;
  for (int i = 0; i < PUB_KEY_SIZE; i++) k.push_back((uint8_t)(0xA0 + i));
  return k;
}

// A peer that the tests add as a contact. Its first byte is neither 00 nor FF.
Bytes peerKey(uint8_t tag = 0x31) {
  Bytes k;
  for (int i = 0; i < PUB_KEY_SIZE; i++) k.push_back((uint8_t)(tag + i));
  return k;
}

// CMD_ADD_UPDATE_CONTACT in the layout writeContactRespFrame() sends back:
// key, type, flags, out_path_len, out_path[64], name[32], last_advert, then the
// optional lat, lon and lastmod.
Bytes contactFrame(const Bytes& key, const char* name, uint8_t out_path_len = 0xFF,
                   bool with_optional = true) {
  Bytes f = {ADD_UPDATE_CONTACT};
  put(f, key.data(), key.size());
  putU8(f, 1);              // ADV_TYPE_CHAT
  putU8(f, 0);              // flags
  putU8(f, out_path_len);   // 0xFF: no path known, so messages flood
  putFill(f, MAX_PATH_SIZE, 0);
  putField(f, name, 32);
  putU32(f, RTC0 - 100);    // last_advert_timestamp
  if (with_optional) {
    putI32(f, 52500000);    // lat
    putI32(f, 13400000);    // lon
    putU32(f, RTC0 - 50);   // lastmod
  }
  return f;
}

Bytes withKey(uint8_t code, const Bytes& key) {
  Bytes f = {code};
  put(f, key.data(), key.size());
  return f;
}

Bytes prefix(const Bytes& f, size_t n) { return Bytes(f.begin(), f.begin() + n); }

class CommandTest : public ::testing::Test {
protected:
  Harness h;

  void SetUp() override { h.boot(); }

  void addPeer(const Bytes& key = peerKey(), uint8_t out_path_len = 0xFF) {
    ASSERT_EQ(h.reply(contactFrame(key, "peer", out_path_len)), OK);
  }

  // The SELF_INFO reply that APP_START returns, which carries most of the prefs.
  Bytes selfInfo() {
    Bytes f = {APP_START, 3};
    putFill(f, 6, 0);
    putStr(f, "test");
    return h.reply(f);
  }
};

// ---- connection and identity -------------------------------------------------

TEST_F(CommandTest, AppStartReportsTheNodeAndItsRadio) {
  Bytes r = selfInfo();
  ASSERT_GE(r.size(), 58u);
  EXPECT_EQ(r[0], RESP_SELF_INFO);
  EXPECT_EQ(r[1], 1);                                    // ADV_TYPE_CHAT
  EXPECT_EQ(r[2], LORA_TX_POWER);
  EXPECT_EQ(r[3], MAX_LORA_TX_POWER);
  EXPECT_EQ(Bytes(r.begin() + 4, r.begin() + 36), selfPubKey());
  EXPECT_EQ(u32At(r, 36), 0u);                           // lat
  EXPECT_EQ(u32At(r, 40), 0u);                           // lon
  EXPECT_EQ(u32At(r, 48), (uint32_t)(LORA_FREQ * 1000));
  EXPECT_EQ(u32At(r, 52), (uint32_t)(LORA_BW * 1000));
  EXPECT_EQ(r[56], LORA_SF);
  EXPECT_EQ(r[57], LORA_CR);
  // The default name is the hex of the first four key bytes.
  EXPECT_EQ(std::string(r.begin() + 58, r.end()), "A0A1A2A3");
}

TEST_F(CommandTest, AppStartNeedsTheReservedBytes) {
  Bytes f = {APP_START, 3};
  putFill(f, 6, 0);
  EXPECT_EQ(h.reply(prefix(f, 7)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f)[0], RESP_SELF_INFO);              // 8 bytes, no app name
}

TEST_F(CommandTest, DeviceQueryReportsTheFirmware) {
  EXPECT_EQ(h.reply({DEVICE_QUERY}), err(ERR_UNSUPPORTED_CMD));
  Bytes r = h.reply({DEVICE_QUERY, 3});
  ASSERT_EQ(r.size(), 82u);
  EXPECT_EQ(r[0], RESP_DEVICE_INFO);
  EXPECT_EQ(r[1], FIRMWARE_VER_CODE);
  EXPECT_EQ(r[2], MAX_CONTACTS / 2);
  EXPECT_EQ(r[3], MAX_GROUP_CHANNELS);
  EXPECT_EQ(u32At(r, 4), 0u);                            // ble pin
  EXPECT_EQ(std::string((const char*)&r[8]), FIRMWARE_BUILD_DATE);
  EXPECT_EQ(std::string((const char*)&r[20]), "Mock");
  EXPECT_EQ(std::string((const char*)&r[60]), FIRMWARE_VERSION);
  EXPECT_EQ(r[80], 0);                                   // client repeat off
  EXPECT_EQ(r[81], 0);                                   // path hash mode
}

TEST_F(CommandTest, PrivateKeyExportAndImport) {
  Bytes r = h.reply({EXPORT_PRIVATE_KEY});
  ASSERT_EQ(r.size(), 65u);
  EXPECT_EQ(r[0], RESP_PRIVATE_KEY);
  for (int i = 0; i < 64; i++) ASSERT_EQ(r[1 + i], (uint8_t)(0x40 + i));

  Bytes f = {IMPORT_PRIVATE_KEY};
  putFill(f, 64, 0x11);
  EXPECT_EQ(h.reply(prefix(f, 64)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
  Bytes after = h.reply({EXPORT_PRIVATE_KEY});
  EXPECT_EQ(after[1], 0x11);
  Bytes info = selfInfo();
  EXPECT_NE(Bytes(info.begin() + 4, info.begin() + 36), selfPubKey());
}

TEST_F(CommandTest, SetAdvertNameIsStoredAndTruncated) {
  EXPECT_EQ(h.reply({SET_ADVERT_NAME}), err(ERR_UNSUPPORTED_CMD));
  Bytes f = {SET_ADVERT_NAME};
  putStr(f, "a-name-that-is-longer-than-thirty-one-bytes");
  EXPECT_EQ(h.reply(f), OK);
  Bytes r = selfInfo();
  EXPECT_EQ(std::string(r.begin() + 58, r.end()), "a-name-that-is-longer-than-thir");
}

TEST_F(CommandTest, SetAdvertLatLon) {
  Bytes f = {SET_ADVERT_LATLON};
  putI32(f, 52500000);
  putI32(f, -13400000);
  EXPECT_EQ(h.reply(prefix(f, 8)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
  Bytes r = selfInfo();
  EXPECT_EQ((int32_t)u32At(r, 36), 52500000);
  EXPECT_EQ((int32_t)u32At(r, 40), -13400000);

  Bytes bad = {SET_ADVERT_LATLON};
  putI32(bad, 91000000);
  putI32(bad, 0);
  EXPECT_EQ(h.reply(bad), err(ERR_ILLEGAL_ARG));
}

TEST_F(CommandTest, DevicePin) {
  Bytes f = {SET_DEVICE_PIN};
  putU32(f, 123456);
  EXPECT_EQ(h.reply(prefix(f, 4)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_EQ(u32At(h.reply({DEVICE_QUERY, 3}), 4), 123456u);

  Bytes bad = {SET_DEVICE_PIN};
  putU32(bad, 12345);
  EXPECT_EQ(h.reply(bad), err(ERR_ILLEGAL_ARG));
}

// ---- time ------------------------------------------------------------------

TEST_F(CommandTest, DeviceTime) {
  Bytes r = h.reply({GET_DEVICE_TIME});
  ASSERT_EQ(r.size(), 5u);
  EXPECT_EQ(r[0], RESP_CURR_TIME);
  EXPECT_EQ(u32At(r, 1), RTC0);

  Bytes f = {SET_DEVICE_TIME};
  putU32(f, RTC0 + 60);
  EXPECT_EQ(h.reply(prefix(f, 4)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_EQ(rtc_clock.now, RTC0 + 60);

  Bytes back = {SET_DEVICE_TIME};
  putU32(back, RTC0);
  EXPECT_EQ(h.reply(back), err(ERR_ILLEGAL_ARG));        // the clock never goes back
  EXPECT_EQ(rtc_clock.now, RTC0 + 60);
}

// ---- radio and tuning --------------------------------------------------------

Bytes radioParams(uint32_t freq, uint32_t bw, uint8_t sf, uint8_t cr, int repeat = -1) {
  Bytes f = {SET_RADIO_PARAMS};
  putU32(f, freq);
  putU32(f, bw);
  putU8(f, sf);
  putU8(f, cr);
  if (repeat >= 0) putU8(f, (uint8_t)repeat);
  return f;
}

TEST_F(CommandTest, SetRadioParams) {
  Bytes f = radioParams(869525, 250000, 11, 5);
  int at_boot = radio_driver.set_params_calls;               // begin() applies the stored ones
  EXPECT_EQ(h.reply(prefix(f, 10)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(radio_driver.set_params_calls, at_boot);
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_FLOAT_EQ(radio_driver.freq, 869.525f);
  EXPECT_FLOAT_EQ(radio_driver.bw, 250.0f);
  EXPECT_EQ(radio_driver.sf, 11);
  EXPECT_EQ(radio_driver.cr, 5);
  Bytes r = selfInfo();
  EXPECT_EQ(u32At(r, 48), 869525u);
  EXPECT_EQ(r[56], 11);

  EXPECT_EQ(h.reply(radioParams(869525, 250000, 13, 5)), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply(radioParams(100000, 250000, 11, 5)), err(ERR_ILLEGAL_ARG));
  // Client repeat is only allowed on the listed frequencies.
  EXPECT_EQ(h.reply(radioParams(869525, 250000, 11, 5, 1)), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply(radioParams(869495, 250000, 11, 5, 1)), OK);
  EXPECT_EQ(h.reply({DEVICE_QUERY, 3})[80], 1);
}

TEST_F(CommandTest, SetTxPower) {
  EXPECT_EQ(h.reply({SET_RADIO_TX_POWER}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SET_RADIO_TX_POWER, 10}), OK);
  EXPECT_EQ(radio_driver.tx_power, 10);
  EXPECT_EQ(selfInfo()[2], 10);
  EXPECT_EQ(h.reply({SET_RADIO_TX_POWER, MAX_LORA_TX_POWER + 1}), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply({SET_RADIO_TX_POWER, (uint8_t)-10}), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply({SET_RADIO_TX_POWER, (uint8_t)-9}), OK);
}

TEST_F(CommandTest, TuningParamsRoundTrip) {
  Bytes r = h.reply({GET_TUNING_PARAMS});
  ASSERT_EQ(r.size(), 9u);
  EXPECT_EQ(r[0], RESP_TUNING_PARAMS);
  EXPECT_EQ(u32At(r, 1), 0u);
  EXPECT_EQ(u32At(r, 5), 1000u);

  Bytes f = {SET_TUNING_PARAMS};
  putU32(f, 2500);
  putU32(f, 3000);
  EXPECT_EQ(h.reply(prefix(f, 8)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
  r = h.reply({GET_TUNING_PARAMS});
  EXPECT_EQ(u32At(r, 1), 2500u);
  EXPECT_EQ(u32At(r, 5), 3000u);
}

TEST_F(CommandTest, SetOtherParamsHasOptionalTail) {
  EXPECT_EQ(h.reply({SET_OTHER_PARAMS}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SET_OTHER_PARAMS, 1}), OK);
  Bytes r = selfInfo();
  EXPECT_EQ(r[47], 1);                                   // manual_add_contacts
  EXPECT_EQ(r[46], 0);                                   // telemetry modes untouched

  EXPECT_EQ(h.reply({SET_OTHER_PARAMS, 0, 0x26, 1, 1}), OK);
  r = selfInfo();
  EXPECT_EQ(r[44], 1);                                   // multi_acks
  EXPECT_EQ(r[45], 1);                                   // advert_loc_policy
  EXPECT_EQ(r[46], 0x26);                                // env 2, loc 1, base 2
  EXPECT_EQ(r[47], 0);
}

TEST_F(CommandTest, AutoAddConfig) {
  EXPECT_EQ(h.reply({SET_AUTOADD_CONFIG}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SET_AUTOADD_CONFIG, 0x1E, 100}), OK);
  EXPECT_EQ(h.reply({GET_AUTOADD_CONFIG}), Bytes({RESP_AUTOADD_CONFIG, 0x1E, 64}));
  EXPECT_EQ(h.reply({SET_AUTOADD_CONFIG, 0x02}), OK);  // max hops stays
  EXPECT_EQ(h.reply({GET_AUTOADD_CONFIG}), Bytes({RESP_AUTOADD_CONFIG, 0x02, 64}));
}

TEST_F(CommandTest, PathHashMode) {
  EXPECT_EQ(h.reply({SET_PATH_HASH_MODE, 0}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SET_PATH_HASH_MODE, 1, 2}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SET_PATH_HASH_MODE, 0, 3}), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply({SET_PATH_HASH_MODE, 0, 2}), OK);
  EXPECT_EQ(h.reply({DEVICE_QUERY, 3})[81], 2);
}

TEST_F(CommandTest, AllowedRepeatFrequencies) {
  Bytes r = h.reply({GET_ALLOWED_REPEAT_FREQ});
  ASSERT_EQ(r.size(), 25u);
  EXPECT_EQ(r[0], RESP_ALLOWED_REPEAT_FREQ);
  EXPECT_EQ(u32At(r, 1), 433000u);
  EXPECT_EQ(u32At(r, 9), 869495u);
  EXPECT_EQ(u32At(r, 17), 918000u);
}

TEST_F(CommandTest, Stats) {
  EXPECT_EQ(h.reply({GET_STATS}), err(ERR_UNSUPPORTED_CMD));
  Bytes core = h.reply({GET_STATS, 0});
  ASSERT_EQ(core.size(), 11u);
  EXPECT_EQ(core[0], RESP_STATS);
  EXPECT_EQ(core[1], 0);
  EXPECT_EQ(u16At(core, 2), 4100);                       // battery
  EXPECT_EQ(u32At(core, 4), T0 / 1000);                  // uptime
  Bytes radio = h.reply({GET_STATS, 1});
  ASSERT_EQ(radio.size(), 14u);
  EXPECT_EQ((int8_t)radio[4], -90);                      // last rssi
  EXPECT_EQ((int8_t)radio[5], 20);                       // last snr * 4
  EXPECT_EQ(h.reply({GET_STATS, 2}).size(), 30u);
  EXPECT_EQ(h.reply({GET_STATS, 3}), err(ERR_ILLEGAL_ARG));
}

TEST_F(CommandTest, BatteryAndStorage) {
  Bytes r = h.reply({GET_BATT_AND_STORAGE});
  ASSERT_EQ(r.size(), 11u);
  EXPECT_EQ(r[0], RESP_BATT_AND_STORAGE);
  EXPECT_EQ(u16At(r, 1), 4100);
  EXPECT_EQ(u32At(r, 7), 512u);                          // total kB of the mock
}

TEST_F(CommandTest, CustomVars) {
  EXPECT_EQ(h.reply({GET_CUSTOM_VARS}), Bytes({RESP_CUSTOM_VARS}));
  Bytes f = {SET_CUSTOM_VAR};
  putStr(f, "a:1");
  EXPECT_EQ(h.reply(prefix(f, 3)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);                             // the mock accepts any name
  Bytes nocolon = {SET_CUSTOM_VAR};
  putStr(nocolon, "abc");
  EXPECT_EQ(h.reply(nocolon), err(ERR_ILLEGAL_ARG));
}

// ---- contacts --------------------------------------------------------------

TEST_F(CommandTest, AddContactNeedsEveryMandatoryField) {
  Bytes f = contactFrame(peerKey(), "peer", 0xFF, false);
  ASSERT_EQ(f.size(), 136u);
  EXPECT_EQ(h.reply(prefix(f, 135)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
}

TEST_F(CommandTest, ContactRoundTrip) {
  addPeer();
  Bytes r = h.reply(withKey(GET_CONTACT_BY_KEY, peerKey()));
  ASSERT_EQ(r.size(), 148u);
  EXPECT_EQ(r[0], RESP_CONTACT);
  // The reply is the add frame with its code changed, which is the layout the app
  // relies on when it edits a contact and sends it back.
  Bytes sent = contactFrame(peerKey(), "peer");
  sent[0] = RESP_CONTACT;
  EXPECT_EQ(r, sent);

  EXPECT_EQ(h.reply(prefix(withKey(GET_CONTACT_BY_KEY, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(withKey(GET_CONTACT_BY_KEY, peerKey(0x50))), err(ERR_NOT_FOUND));
}

TEST_F(CommandTest, AddContactWithoutLastmodUsesTheClock) {
  Bytes f = contactFrame(peerKey(), "peer");
  f.resize(f.size() - 4);
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_EQ(u32At(h.reply(withKey(GET_CONTACT_BY_KEY, peerKey())), 144), RTC0);
}

TEST_F(CommandTest, ContactListing) {
  addPeer(peerKey(0x31));
  addPeer(peerKey(0x51));
  Bytes start = h.reply({GET_CONTACTS});
  EXPECT_EQ(start, Bytes({RESP_CONTACTS_START, 2, 0, 0, 0}));
  // The iterator runs in the following loops, one contact each, then the end
  // marker with the newest lastmod, so the app can ask for "since" next time.
  for (int i = 0; i < 4; i++) h.mesh().loop();
  ASSERT_EQ(h.serial.written.size(), 4u);
  EXPECT_EQ(h.serial.written[0], start);
  EXPECT_EQ(h.serial.written[1][0], RESP_CONTACT);
  EXPECT_EQ(h.serial.written[2][0], RESP_CONTACT);
  Bytes end = {RESP_END_OF_CONTACTS};
  putU32(end, RTC0 - 50);
  EXPECT_EQ(h.serial.written[3], end);
  // A second request while one runs is refused.
  h.reply({GET_CONTACTS});
  EXPECT_EQ(h.reply({GET_CONTACTS}), err(ERR_BAD_STATE));
}

TEST_F(CommandTest, ContactListingSince) {
  addPeer();
  Bytes f = {GET_CONTACTS};
  putU32(f, RTC0);                                       // newer than the contact
  EXPECT_EQ(h.reply(f), Bytes({RESP_CONTACTS_START, 1, 0, 0, 0}));
  h.serial.written.clear();
  for (int i = 0; i < 3; i++) h.mesh().loop();
  ASSERT_EQ(h.serial.written.size(), 1u);
  EXPECT_EQ(h.serial.written[0][0], RESP_END_OF_CONTACTS);
}

TEST_F(CommandTest, ResetPathAndRemove) {
  addPeer(peerKey(), 0);
  EXPECT_EQ(h.reply(prefix(withKey(RESET_PATH, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(withKey(RESET_PATH, peerKey())), OK);
  EXPECT_EQ(h.reply(withKey(GET_CONTACT_BY_KEY, peerKey()))[35], 0xFF);
  EXPECT_EQ(h.reply(withKey(RESET_PATH, peerKey(0x50))), err(ERR_NOT_FOUND));

  EXPECT_EQ(h.reply(prefix(withKey(REMOVE_CONTACT, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(withKey(REMOVE_CONTACT, peerKey())), OK);
  EXPECT_EQ(h.reply(withKey(REMOVE_CONTACT, peerKey())), err(ERR_NOT_FOUND));
  EXPECT_EQ(h.reply(withKey(GET_CONTACT_BY_KEY, peerKey())), err(ERR_NOT_FOUND));
}

// The contact table starts with eight reserved slots for anonymous requests, all
// zeros until one is used, and the lookup searches them too. A key of zeros
// therefore names a reserved slot, and REMOVE_CONTACT removes it. The reported
// count, num_contacts minus the eight, then wraps. Recorded as it is; the slots
// come back on the next boot (resetContacts()). Also seen on a board, see
// docs/refactoring/0010-companion-command-table.md.
TEST_F(CommandTest, AZeroKeyNamesAReservedSlot) {
  Bytes zero(PUB_KEY_SIZE, 0);
  EXPECT_EQ(h.reply(withKey(GET_CONTACT_BY_KEY, zero))[0], RESP_CONTACT);
  EXPECT_EQ(h.reply(withKey(REMOVE_CONTACT, zero)), OK);
  Bytes start = h.reply({GET_CONTACTS});
  EXPECT_EQ(start, Bytes({RESP_CONTACTS_START, 0xFF, 0xFF, 0xFF, 0xFF}));
  for (int i = 0; i < 7; i++) EXPECT_EQ(h.reply(withKey(REMOVE_CONTACT, zero)), OK) << i;
  EXPECT_EQ(h.reply(withKey(REMOVE_CONTACT, zero)), err(ERR_NOT_FOUND));
}

TEST_F(CommandTest, ShareContact) {
  addPeer();
  EXPECT_EQ(h.reply(prefix(withKey(SHARE_CONTACT, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  // Sharing resends the contact's stored advert; one added by hand has none, which
  // the firmware reports as "unable to send".
  EXPECT_EQ(h.reply(withKey(SHARE_CONTACT, peerKey())), err(ERR_TABLE_FULL));
  EXPECT_EQ(h.mesh().outboundTotal(), 0);
  EXPECT_EQ(h.reply(withKey(SHARE_CONTACT, peerKey(0x50))), err(ERR_NOT_FOUND));
}

TEST_F(CommandTest, ExportSelfThenSendItAsRawPacket) {
  Bytes r = h.reply({EXPORT_CONTACT});
  ASSERT_GT(r.size(), 1u + 2 + PUB_KEY_SIZE);
  EXPECT_EQ(r[0], RESP_EXPORT_CONTACT);
  EXPECT_EQ(r[1] & 0x03, 1);                             // ROUTE_TYPE_FLOOD
  EXPECT_EQ(h.mesh().outboundTotal(), 0);                // exported, not sent

  // The advert is a whole packet, so the raw packet command takes it as is.
  Bytes raw = {SEND_RAW_PACKET, 1};
  put(raw, &r[1], r.size() - 1);
  EXPECT_EQ(h.reply(prefix(raw, 3)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(raw), OK);
  EXPECT_GT(h.mesh().outboundTotal(), 0);
  EXPECT_EQ(h.reply({SEND_RAW_PACKET, 1, 0xFF, 0xFF}), err(ERR_ILLEGAL_ARG));

  EXPECT_EQ(h.reply(withKey(EXPORT_CONTACT, peerKey())), err(ERR_NOT_FOUND));
}

TEST_F(CommandTest, ExportAndImportAContact) {
  addPeer();
  Bytes r = h.reply(withKey(EXPORT_CONTACT, peerKey()));
  // A contact that was added by hand has no advert stored to export.
  EXPECT_EQ(r, err(ERR_NOT_FOUND));

  // Importing needs more than 98 bytes; a packet that is not an advert is refused.
  Bytes f = {IMPORT_CONTACT};
  putFill(f, 97, 0);
  EXPECT_EQ(h.reply(f), err(ERR_UNSUPPORTED_CMD));
  putU8(f, 0);
  EXPECT_EQ(h.reply(f), err(ERR_ILLEGAL_ARG));
}

TEST_F(CommandTest, AdvertPath) {
  Bytes f = {GET_ADVERT_PATH, 0};
  put(f, peerKey().data(), PUB_KEY_SIZE);
  EXPECT_EQ(h.reply(prefix(f, 33)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), err(ERR_NOT_FOUND));
}

// ---- messages and requests -------------------------------------------------

Bytes textMsg(uint8_t txt_type, const Bytes& key, const char* text) {
  Bytes f = {SEND_TXT_MSG, txt_type, 0};
  putU32(f, RTC0);
  put(f, key.data(), 6);
  putStr(f, text);
  return f;
}

TEST_F(CommandTest, SendTextMessage) {
  addPeer();
  Bytes f = textMsg(0, peerKey(), "hi");
  EXPECT_EQ(h.reply(prefix(f, 13)), err(ERR_UNSUPPORTED_CMD));
  Bytes r = h.reply(f);
  ASSERT_EQ(r.size(), 10u);
  EXPECT_EQ(r[0], RESP_SENT);
  EXPECT_EQ(r[1], 1);                                    // flooded, no path known
  EXPECT_NE(u32At(r, 2), 0u);                            // the ack to expect
  EXPECT_GT(h.mesh().outboundTotal(), 0);

  EXPECT_EQ(h.reply(textMsg(0, peerKey(0x50), "hi")), err(ERR_NOT_FOUND));
  EXPECT_EQ(h.reply(textMsg(2, peerKey(), "hi")), err(ERR_UNSUPPORTED_CMD));
  // A CLI command expects no ack.
  EXPECT_EQ(u32At(h.reply(textMsg(1, peerKey(), "ver")), 2), 0u);
}

Bytes channelMsg(uint8_t txt_type, uint8_t idx, const char* text) {
  Bytes f = {SEND_CHANNEL_TXT_MSG, txt_type, idx};
  putU32(f, RTC0);
  putStr(f, text);
  return f;
}

TEST_F(CommandTest, SendChannelText) {
  EXPECT_EQ(h.reply(prefix(channelMsg(0, 0, ""), 6)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(channelMsg(0, 0, "")), OK);
  EXPECT_EQ(h.reply(channelMsg(0, 0, "hello")), OK);
  EXPECT_GT(h.mesh().outboundTotal(), 0);
  EXPECT_EQ(h.reply(channelMsg(1, 0, "hello")), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(channelMsg(0, MAX_GROUP_CHANNELS, "hello")), err(ERR_NOT_FOUND));
}

Bytes channelData(uint8_t idx, uint8_t path_len, uint16_t data_type, size_t payload) {
  Bytes f = {SEND_CHANNEL_DATA, idx, path_len};
  if (path_len != 0xFF) putFill(f, path_len, 0x22);
  putU16(f, data_type);
  putFill(f, payload, 0x33);
  return f;
}

TEST_F(CommandTest, SendChannelData) {
  // This command answers a short frame with ILLEGAL_ARG, not UNSUPPORTED_CMD.
  EXPECT_EQ(h.reply({SEND_CHANNEL_DATA, 0, 0xFF}), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply({SEND_CHANNEL_DATA, 0, 0xFF, 0x34}), err(ERR_ILLEGAL_ARG));
  EXPECT_EQ(h.reply(channelData(0, 0xFF, 0x1234, 0)), OK);
  EXPECT_EQ(h.reply(channelData(0, 2, 0x1234, 3)), OK);
  EXPECT_EQ(h.reply(channelData(0, 0xFF, 0, 3)), err(ERR_ILLEGAL_ARG));   // reserved type
  EXPECT_EQ(h.reply(channelData(MAX_GROUP_CHANNELS, 0xFF, 0x1234, 3)), err(ERR_NOT_FOUND));
  EXPECT_EQ(h.reply({SEND_CHANNEL_DATA, 0, 0x7F, 0, 0}), err(ERR_ILLEGAL_ARG)); // bad path len
}

TEST_F(CommandTest, SendRawData) {
  Bytes f = {SEND_RAW_DATA, 1, 0x44, 1, 2, 3, 4};
  EXPECT_EQ(h.reply(prefix(f, 5)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(prefix(f, 6)), err(ERR_ILLEGAL_ARG));   // payload below 4
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_EQ(h.reply({SEND_RAW_DATA, 0xFF, 1, 2, 3, 4}), err(ERR_UNSUPPORTED_CMD));  // flood
}

// The four requests that answer RESP_SENT with a tag, against a known peer, and
// NOT_FOUND for an unknown one.
TEST_F(CommandTest, RequestsToAPeer) {
  addPeer();
  Bytes login = withKey(SEND_LOGIN, peerKey());
  putStr(login, "pw");
  EXPECT_EQ(h.reply(prefix(login, 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(login)[0], RESP_SENT);
  EXPECT_EQ(h.reply(withKey(SEND_LOGIN, peerKey(0x50))), err(ERR_NOT_FOUND));

  EXPECT_EQ(h.reply(prefix(withKey(SEND_STATUS_REQ, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(withKey(SEND_STATUS_REQ, peerKey()))[0], RESP_SENT);
  EXPECT_EQ(h.reply(withKey(SEND_STATUS_REQ, peerKey(0x50))), err(ERR_NOT_FOUND));

  Bytes bin = withKey(SEND_BINARY_REQ, peerKey());
  EXPECT_EQ(h.reply(bin), err(ERR_UNSUPPORTED_CMD));         // needs at least one data byte
  putU8(bin, 0x07);
  EXPECT_EQ(h.reply(bin)[0], RESP_SENT);

  Bytes disc = {SEND_PATH_DISCOVERY_REQ, 0};
  put(disc, peerKey().data(), PUB_KEY_SIZE);
  EXPECT_EQ(h.reply(prefix(disc, 33)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(disc)[0], RESP_SENT);
  disc[1] = 1;
  EXPECT_EQ(h.reply(disc), err(ERR_UNSUPPORTED_CMD));
}

TEST_F(CommandTest, TelemetryHasTwoForms) {
  addPeer();
  Bytes self = {SEND_TELEMETRY_REQ, 0, 0, 0};
  Bytes r = h.reply(self);
  ASSERT_GE(r.size(), 8u);
  EXPECT_EQ(r[0], PUSH_TELEMETRY_RESPONSE);
  EXPECT_EQ(Bytes(r.begin() + 2, r.begin() + 8), prefix(selfPubKey(), 6));

  Bytes peer = {SEND_TELEMETRY_REQ, 0, 0, 0};
  put(peer, peerKey().data(), PUB_KEY_SIZE);
  EXPECT_EQ(h.reply(peer)[0], RESP_SENT);
  // Between the two forms the frame matches neither.
  EXPECT_EQ(h.reply(prefix(peer, 5)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(prefix(peer, 35)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(prefix(peer, 3)), err(ERR_UNSUPPORTED_CMD));
}

TEST_F(CommandTest, AnonRequestAddsATransientContact) {
  Bytes f = withKey(SEND_ANON_REQ, peerKey(0x50));
  EXPECT_EQ(h.reply(f), err(ERR_UNSUPPORTED_CMD));           // needs at least one data byte
  putU8(f, 0x01);
  Bytes r = h.reply(f);
  ASSERT_EQ(r.size(), 10u);
  EXPECT_EQ(r[0], RESP_SENT);
  EXPECT_EQ(r[1], 0);                                        // zero hop direct
  EXPECT_EQ(h.reply(withKey(GET_CONTACT_BY_KEY, peerKey(0x50)))[33], 0);  // ADV_TYPE_NONE
}

TEST_F(CommandTest, ConnectionsAndLogout) {
  EXPECT_EQ(h.reply(prefix(withKey(HAS_CONNECTION, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(withKey(HAS_CONNECTION, peerKey())), err(ERR_NOT_FOUND));
  EXPECT_EQ(h.reply(prefix(withKey(LOGOUT, peerKey()), 32)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(withKey(LOGOUT, peerKey())), OK);
}

TEST_F(CommandTest, TracePath) {
  Bytes f = {SEND_TRACE_PATH};
  putU32(f, 0x11223344);
  putU32(f, 0);
  putU8(f, 0);                                               // one byte hashes
  EXPECT_EQ(h.reply(f), err(ERR_UNSUPPORTED_CMD));           // no path
  putU8(f, 0x7A);
  Bytes r = h.reply(f);
  ASSERT_EQ(r.size(), 10u);
  EXPECT_EQ(r[0], RESP_SENT);
  EXPECT_EQ(u32At(r, 2), 0x11223344u);
  f[9] = 1;                                                  // two byte hashes: 1 is not a multiple
  EXPECT_EQ(h.reply(f), err(ERR_ILLEGAL_ARG));
}

TEST_F(CommandTest, ControlData) {
  EXPECT_EQ(h.reply({SEND_CONTROL_DATA}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SEND_CONTROL_DATA, 0x10}), err(ERR_UNSUPPORTED_CMD));   // high bit needed
  EXPECT_EQ(h.reply({SEND_CONTROL_DATA, 0x80, 1, 2}), OK);
  EXPECT_GT(h.mesh().outboundTotal(), 0);
}

TEST_F(CommandTest, SelfAdvert) {
  EXPECT_EQ(h.reply({SEND_SELF_ADVERT}), OK);
  EXPECT_EQ(h.reply({SEND_SELF_ADVERT, 1}), OK);
  EXPECT_EQ(h.mesh().outboundTotal(), 2);
}

TEST_F(CommandTest, SyncWithNothingQueued) {
  EXPECT_EQ(h.reply({SYNC_NEXT_MESSAGE}), Bytes({RESP_NO_MORE_MESSAGES}));
}

// ---- channels, scopes, signing -------------------------------------------------

Bytes setChannel(uint8_t idx, const char* name, uint8_t secret_byte) {
  Bytes f = {SET_CHANNEL, idx};
  putField(f, name, 32);
  putFill(f, 16, secret_byte);
  return f;
}

TEST_F(CommandTest, Channels) {
  EXPECT_EQ(h.reply({GET_CHANNEL}), err(ERR_UNSUPPORTED_CMD));
  Bytes pub = h.reply({GET_CHANNEL, 0});
  ASSERT_EQ(pub.size(), 50u);
  EXPECT_EQ(pub[0], RESP_CHANNEL_INFO);
  EXPECT_EQ(std::string((const char*)&pub[2]), "Public");

  Bytes f = setChannel(1, "#test", 0x5C);
  EXPECT_EQ(h.reply(prefix(f, 49)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply(f), OK);
  Bytes back = h.reply({GET_CHANNEL, 1});
  f[0] = RESP_CHANNEL_INFO;
  EXPECT_EQ(back, f);
  EXPECT_EQ(h.reply(setChannel(MAX_GROUP_CHANNELS, "x", 1)), err(ERR_NOT_FOUND));
  // A 256 bit secret is not supported, and says so.
  Bytes wide = setChannel(1, "#wide", 1);
  putFill(wide, 16, 1);
  EXPECT_EQ(h.reply(wide), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({GET_CHANNEL, MAX_GROUP_CHANNELS}), err(ERR_NOT_FOUND));
}

TEST_F(CommandTest, FloodScopeKey) {
  EXPECT_EQ(h.reply({SET_FLOOD_SCOPE_KEY}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SET_FLOOD_SCOPE_KEY, 0}), OK);
  Bytes f = {SET_FLOOD_SCOPE_KEY, 0};
  putFill(f, 16, 0x42);
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_EQ(h.reply({SET_FLOOD_SCOPE_KEY, 1}), OK);
  EXPECT_EQ(h.reply({SET_FLOOD_SCOPE_KEY, 2}), err(ERR_UNSUPPORTED_CMD));
}

TEST_F(CommandTest, DefaultFloodScope) {
  EXPECT_EQ(h.reply({GET_DEFAULT_FLOOD_SCOPE}), Bytes({RESP_DEFAULT_FLOOD_SCOPE}));
  Bytes f = {SET_DEFAULT_FLOOD_SCOPE};
  putField(f, "hansemesh", 31);
  putFill(f, 16, 0x24);
  EXPECT_EQ(h.reply(f), OK);
  Bytes r = h.reply({GET_DEFAULT_FLOOD_SCOPE});
  Bytes expected = f;
  expected[0] = RESP_DEFAULT_FLOOD_SCOPE;
  EXPECT_EQ(r, expected);

  // A frame shorter than name and key clears the scope, whatever it holds.
  EXPECT_EQ(h.reply(prefix(f, 47)), OK);
  EXPECT_EQ(h.reply({GET_DEFAULT_FLOOD_SCOPE}), Bytes({RESP_DEFAULT_FLOOD_SCOPE}));
  Bytes empty = {SET_DEFAULT_FLOOD_SCOPE};
  putFill(empty, 47, 0);
  EXPECT_EQ(h.reply(empty), err(ERR_ILLEGAL_ARG));
}

TEST_F(CommandTest, Signing) {
  EXPECT_EQ(h.reply({SIGN_FINISH}), err(ERR_BAD_STATE));
  EXPECT_EQ(h.reply({SIGN_DATA, 1}), err(ERR_BAD_STATE));
  Bytes start = h.reply({SIGN_START});
  ASSERT_EQ(start.size(), 6u);
  EXPECT_EQ(start[0], RESP_SIGN_START);
  EXPECT_EQ(u32At(start, 2), 8u * 1024);
  EXPECT_EQ(h.reply({SIGN_DATA}), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(h.reply({SIGN_DATA, 1, 2, 3}), OK);
  Bytes sig = h.reply({SIGN_FINISH});
  ASSERT_EQ(sig.size(), 1u + SIGNATURE_SIZE);
  EXPECT_EQ(sig[0], RESP_SIGNATURE);
  EXPECT_EQ(h.reply({SIGN_FINISH}), err(ERR_BAD_STATE));
}

// ---- CLI, reboot, reset ----------------------------------------------------

TEST_F(CommandTest, RunCliCommand) {
  EXPECT_EQ(h.reply({RUN_CLI_COMMAND, 'x'}), err(ERR_UNSUPPORTED_CMD));
  Bytes f = {RUN_CLI_COMMAND};
  putStr(f, "get name");
  Bytes r = h.reply(f);
  EXPECT_EQ(r[0], RESP_CLI_REPLY);
  EXPECT_EQ(std::string(r.begin() + 1, r.end()), "> A0A1A2A3");
}

TEST_F(CommandTest, RebootNeedsTheWord) {
  Bytes f = {REBOOT};
  putStr(f, "reboot");
  EXPECT_EQ(h.reply(prefix(f, 6)), err(ERR_UNSUPPORTED_CMD));
  Bytes other = {REBOOT};
  putStr(other, "rebooX");
  EXPECT_EQ(h.reply(other), err(ERR_UNSUPPORTED_CMD));
  EXPECT_EQ(board.reboot_calls, 0);
  EXPECT_TRUE(h.send(f).empty());                            // no reply, the board restarts
  EXPECT_EQ(board.reboot_calls, 1);
}

TEST_F(CommandTest, FactoryResetNeedsTheWord) {
  Bytes f = {FACTORY_RESET};
  putStr(f, "reset");
  EXPECT_EQ(h.reply(prefix(f, 5)), err(ERR_UNSUPPORTED_CMD));
  EXPECT_FALSE(h.fs.files.empty());
  EXPECT_EQ(h.reply(f), OK);
  EXPECT_TRUE(h.fs.files.empty());
  EXPECT_EQ(h.serial.disable_calls, 1);
  EXPECT_EQ(board.reboot_calls, 1);
}

// ---- codes without a command --------------------------------------------------

TEST_F(CommandTest, UnknownCodes) {
  for (int code : {0, 44, 45, 46, 47, 48, 49, 53, 67, 0x7F, 0x80, 0xFF}) {
    Bytes f = {(uint8_t)code};
    putFill(f, 40, 0);
    EXPECT_EQ(h.reply(prefix(f, 1)), err(ERR_UNSUPPORTED_CMD)) << code;
    EXPECT_EQ(h.reply(f), err(ERR_UNSUPPORTED_CMD)) << code;
  }
}

}  // namespace
