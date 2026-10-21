// Where each command's frame ends, with frames an app sends.
//
// The handlers read their fields at offsets into cmd_frame. On a board the bytes
// above a frame's length are left over from the previous, longer frame, so a
// handler that reads past `len` acts on them. The frame sweep shows that for
// filler frames behind one tail; these tests do it for a catalog of real frames:
// for each command at its minimum and one byte below it, at both sides of every
// optional field, and at the places where a handler, or a function it calls, is
// known to read past the frame (docs/refactoring/0010-companion-command-table.md,
// step 10c). Each frame goes to a freshly booted node once as it is and once
// behind each of four tails, and everything the node did must be the same: the
// replies byte for byte, the packets queued for the air, the files stored, the
// calls to the board, radio, clock and sensors.
//
// A second test pins the answer one byte below each minimum, the case step 10d
// would change and step 10c must not.

#include "CompanionHarness.h"
#include <helpers/FrameReader.h>

#include <functional>
#include <sstream>

using namespace companion;

namespace {

enum : uint8_t {
  APP_START = 1, SEND_TXT_MSG = 2, SEND_CHANNEL_TXT_MSG = 3, GET_CONTACTS = 4,
  GET_DEVICE_TIME = 5, SET_DEVICE_TIME = 6, SEND_SELF_ADVERT = 7, SET_ADVERT_NAME = 8,
  ADD_UPDATE_CONTACT = 9, SET_RADIO_PARAMS = 11, SET_RADIO_TX_POWER = 12,
  RESET_PATH = 13, SET_ADVERT_LATLON = 14, REMOVE_CONTACT = 15, SHARE_CONTACT = 16,
  EXPORT_CONTACT = 17, IMPORT_CONTACT = 18, REBOOT = 19, SET_TUNING_PARAMS = 21,
  DEVICE_QUERY = 22, IMPORT_PRIVATE_KEY = 24, SEND_RAW_DATA = 25, SEND_LOGIN = 26,
  SEND_STATUS_REQ = 27, HAS_CONNECTION = 28, LOGOUT = 29, GET_CONTACT_BY_KEY = 30,
  GET_CHANNEL = 31, SET_CHANNEL = 32, SIGN_START = 33, SIGN_DATA = 34,
  SEND_TRACE_PATH = 36, SET_DEVICE_PIN = 37, SET_OTHER_PARAMS = 38,
  SEND_TELEMETRY_REQ = 39, SET_CUSTOM_VAR = 41, GET_ADVERT_PATH = 42,
  SEND_BINARY_REQ = 50, FACTORY_RESET = 51, SEND_PATH_DISCOVERY_REQ = 52,
  SET_FLOOD_SCOPE_KEY = 54, SEND_CONTROL_DATA = 55, GET_STATS = 56, SEND_ANON_REQ = 57,
  SET_AUTOADD_CONFIG = 58, SET_PATH_HASH_MODE = 61, SEND_CHANNEL_DATA = 62,
  SET_DEFAULT_FLOOD_SCOPE = 63, SEND_RAW_PACKET = 65, RUN_CLI_COMMAND = 66,
};

constexpr uint8_t ERR_UNSUPPORTED_CMD = 1, ERR_ILLEGAL_ARG = 6;

Bytes peerKey() {
  Bytes k;
  for (int i = 0; i < PUB_KEY_SIZE; i++) k.push_back((uint8_t)(0x31 + i));
  return k;
}

Bytes contactFrame(size_t optional_bytes) {
  Bytes f = {ADD_UPDATE_CONTACT};
  Bytes key = peerKey();
  put(f, key.data(), key.size());
  putU8(f, 1);              // ADV_TYPE_CHAT
  putU8(f, 0);              // flags
  putU8(f, 0xFF);           // no path known
  putFill(f, MAX_PATH_SIZE, 0);
  putField(f, "peer", 32);
  putU32(f, RTC0 - 100);    // last_advert_timestamp
  Bytes opt;
  putI32(opt, 52500000);
  putI32(opt, 13400000);
  putU32(opt, RTC0 - 50);
  put(f, opt.data(), optional_bytes);
  return f;
}

// One entry: a name for the failure message, and the frame.
struct Case {
  std::string name;
  Bytes frame;
};

Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes f;
  for (const Bytes& p : parts) put(f, p.data(), p.size());
  return f;
}
Bytes u32le(uint32_t v) { Bytes b; putU32(b, v); return b; }
Bytes fill(size_t n, uint8_t v) { return Bytes(n, v); }
Bytes str(const char* s) { return Bytes(s, s + strlen(s)); }
Bytes key() { return peerKey(); }
Bytes prefixN(size_t n) { Bytes k = peerKey(); return Bytes(k.begin(), k.begin() + n); }
Bytes prefix6() { return prefixN(6); }
Bytes fieldOf(const char* s, size_t width) { Bytes f; putField(f, s, width); return f; }

std::vector<Case> catalog() {
  std::vector<Case> c;
  auto add = [&](const char* name, Bytes f) { c.push_back({name, f}); };

  add("DEVICE_QUERY min", {DEVICE_QUERY, 3});
  add("APP_START min", cat({{APP_START, 3}, fill(6, 0)}));
  add("APP_START name", cat({{APP_START, 3}, fill(6, 0), str("app")}));
  add("RUN_CLI_COMMAND min", cat({{RUN_CLI_COMMAND}, str("ve")}));
  add("RUN_CLI_COMMAND ver", cat({{RUN_CLI_COMMAND}, str("ver")}));
  add("RUN_CLI_COMMAND get radio", cat({{RUN_CLI_COMMAND}, str("get radio")}));
  add("SEND_TXT_MSG min", cat({{SEND_TXT_MSG, 0, 0}, u32le(RTC0), prefix6(), str("h")}));
  add("SEND_TXT_MSG text", cat({{SEND_TXT_MSG, 0, 0}, u32le(RTC0), prefix6(), str("hello")}));
  add("SEND_TXT_MSG cli", cat({{SEND_TXT_MSG, 1, 0}, u32le(RTC0), prefix6(), str("ver")}));
  add("SEND_CHANNEL_TXT_MSG min", cat({{SEND_CHANNEL_TXT_MSG, 0, 0}, u32le(RTC0)}));
  add("SEND_CHANNEL_TXT_MSG text", cat({{SEND_CHANNEL_TXT_MSG, 0, 0}, u32le(RTC0), str("hi")}));
  add("SEND_CHANNEL_DATA min", {SEND_CHANNEL_DATA, 0, 0xFF, 0x34});
  add("SEND_CHANNEL_DATA flood", cat({{SEND_CHANNEL_DATA, 0, 0xFF, 0x34, 0x12}, str("ab")}));
  add("SEND_CHANNEL_DATA path", cat({{SEND_CHANNEL_DATA, 0, 2, 7, 8, 0x34, 0x12}, str("ab")}));
  add("SEND_CHANNEL_DATA no data_type", {SEND_CHANNEL_DATA, 0, 2, 7, 8, 0x34});
  // the path length byte announces 8 bytes, the frame holds 2
  add("SEND_CHANNEL_DATA path past end", {SEND_CHANNEL_DATA, 0, 8, 7, 8});
  add("SEND_CHANNEL_DATA 3 byte hashes past end", {SEND_CHANNEL_DATA, 0, 0x85, 7, 8, 9});
  add("GET_CONTACTS min", {GET_CONTACTS});
  add("GET_CONTACTS since short", {GET_CONTACTS, 1, 2, 3});
  add("GET_CONTACTS since", cat({{GET_CONTACTS}, u32le(RTC0 - 200)}));
  add("SET_ADVERT_NAME min", {SET_ADVERT_NAME, 'a'});
  add("SET_ADVERT_NAME", cat({{SET_ADVERT_NAME}, str("node")}));
  add("SET_ADVERT_LATLON min", cat({{SET_ADVERT_LATLON}, u32le(52500000), u32le(13400000)}));
  add("SET_ADVERT_LATLON alt short", cat({{SET_ADVERT_LATLON}, u32le(52500000), u32le(13400000), fill(3, 1)}));
  add("SET_ADVERT_LATLON alt", cat({{SET_ADVERT_LATLON}, u32le(52500000), u32le(13400000), u32le(30)}));
  add("SET_DEVICE_TIME min", cat({{SET_DEVICE_TIME}, u32le(RTC0 + 10)}));
  add("SEND_SELF_ADVERT min", {SEND_SELF_ADVERT});
  add("SEND_SELF_ADVERT flood", {SEND_SELF_ADVERT, 1});
  add("RESET_PATH min", cat({{RESET_PATH}, key()}));
  add("ADD_UPDATE_CONTACT min", contactFrame(0));
  add("ADD_UPDATE_CONTACT latlon", contactFrame(8));
  add("ADD_UPDATE_CONTACT lastmod", contactFrame(12));
  add("REMOVE_CONTACT min", cat({{REMOVE_CONTACT}, key()}));
  add("SHARE_CONTACT min", cat({{SHARE_CONTACT}, key()}));
  add("GET_CONTACT_BY_KEY min", cat({{GET_CONTACT_BY_KEY}, key()}));
  add("EXPORT_CONTACT self", {EXPORT_CONTACT});
  add("EXPORT_CONTACT self, 32", cat({{EXPORT_CONTACT}, fill(31, 0x31)}));
  add("EXPORT_CONTACT key", cat({{EXPORT_CONTACT}, key()}));
  add("IMPORT_CONTACT min", cat({{IMPORT_CONTACT}, fill(98, 0x11)}));
  add("SET_RADIO_PARAMS min", cat({{SET_RADIO_PARAMS}, u32le(869525), u32le(250000), {11, 5}}));
  add("SET_RADIO_PARAMS repeat", cat({{SET_RADIO_PARAMS}, u32le(869525), u32le(250000), {11, 5, 1}}));
  add("SET_RADIO_TX_POWER min", {SET_RADIO_TX_POWER, 20});
  add("SET_TUNING_PARAMS min", cat({{SET_TUNING_PARAMS}, u32le(1500), u32le(2000)}));
  add("SET_OTHER_PARAMS min", {SET_OTHER_PARAMS, 1});
  add("SET_OTHER_PARAMS 3", {SET_OTHER_PARAMS, 1, 0x15});
  add("SET_OTHER_PARAMS 4", {SET_OTHER_PARAMS, 1, 0x15, 2});
  add("SET_OTHER_PARAMS 5", {SET_OTHER_PARAMS, 1, 0x15, 2, 1});
  add("SET_PATH_HASH_MODE min", {SET_PATH_HASH_MODE, 0, 1});
  add("REBOOT min", cat({{REBOOT}, str("reboot")}));
  add("IMPORT_PRIVATE_KEY min", cat({{IMPORT_PRIVATE_KEY}, fill(64, 0)}));
  add("SEND_RAW_DATA min", {SEND_RAW_DATA, 0, 1, 2, 3, 4});
  add("SEND_RAW_DATA path", {SEND_RAW_DATA, 1, 0x44, 1, 2, 3, 4});
  add("SEND_RAW_DATA payload short", {SEND_RAW_DATA, 1, 0x44, 1, 2, 3});
  // the path length byte announces 8 bytes, the frame holds 4
  add("SEND_RAW_DATA path past end", {SEND_RAW_DATA, 8, 1, 2, 3, 4});
  add("SEND_RAW_DATA 2 byte hashes past end", {SEND_RAW_DATA, 0x43, 1, 2, 3, 4, 5});
  add("SEND_LOGIN min", cat({{SEND_LOGIN}, key()}));
  add("SEND_LOGIN password", cat({{SEND_LOGIN}, key(), str("pw")}));
  add("SEND_ANON_REQ min", cat({{SEND_ANON_REQ}, key(), {7}}));
  add("SEND_STATUS_REQ min", cat({{SEND_STATUS_REQ}, key()}));
  add("SEND_PATH_DISCOVERY_REQ min", cat({{SEND_PATH_DISCOVERY_REQ, 0}, key()}));
  add("SEND_TELEMETRY_REQ self", {SEND_TELEMETRY_REQ, 0, 0, 0});
  add("SEND_TELEMETRY_REQ 5", {SEND_TELEMETRY_REQ, 0, 0, 0, 0x31});
  add("SEND_TELEMETRY_REQ 35", cat({{SEND_TELEMETRY_REQ, 0, 0, 0}, prefixN(31)}));
  add("SEND_TELEMETRY_REQ peer", cat({{SEND_TELEMETRY_REQ, 0, 0, 0}, key()}));
  add("SEND_BINARY_REQ min", cat({{SEND_BINARY_REQ}, key(), {7}}));
  add("HAS_CONNECTION min", cat({{HAS_CONNECTION}, key()}));
  add("LOGOUT min", cat({{LOGOUT}, key()}));
  add("GET_CHANNEL min", {GET_CHANNEL, 0});
  add("SET_CHANNEL min", cat({{SET_CHANNEL, 1}, fieldOf("#test", 32), fill(16, 0x21)}));
  add("SET_CHANNEL 65", cat({{SET_CHANNEL, 1}, fieldOf("#test", 32), fill(31, 0x21)}));
  add("SET_CHANNEL 256 bit", cat({{SET_CHANNEL, 1}, fieldOf("#test", 32), fill(32, 0x21)}));
  add("SIGN_START then DATA", {SIGN_DATA, 1});
  add("SEND_TRACE_PATH min", cat({{SEND_TRACE_PATH}, u32le(7), u32le(9), {0, 0x44}}));
  add("SEND_TRACE_PATH longest", cat({{SEND_TRACE_PATH}, u32le(7), u32le(9), {0}, fill(MAX_PATH_SIZE, 0x44)}));
  // the longest frame; the handler's own upper bound (len - 10 >= MAX_PACKET_PAYLOAD - 5)
  // lies above MAX_FRAME_SIZE and is never reached
  add("SEND_TRACE_PATH longest frame", cat({{SEND_TRACE_PATH}, u32le(7), u32le(9), {0}, fill(MAX_FRAME_SIZE - 10, 0x44)}));
  add("SET_DEVICE_PIN min", cat({{SET_DEVICE_PIN}, u32le(123456)}));
  add("SET_CUSTOM_VAR min", cat({{SET_CUSTOM_VAR}, str("a:1")}));
  add("SET_CUSTOM_VAR gps", cat({{SET_CUSTOM_VAR}, str("gps:1")}));
  add("SET_CUSTOM_VAR no separator", cat({{SET_CUSTOM_VAR}, str("abc")}));
  add("GET_ADVERT_PATH min", cat({{GET_ADVERT_PATH, 0}, key()}));
  add("GET_STATS core", {GET_STATS, 0});
  add("GET_STATS radio", {GET_STATS, 1});
  add("GET_STATS packets", {GET_STATS, 2});
  add("GET_STATS other", {GET_STATS, 3});
  add("FACTORY_RESET min", cat({{FACTORY_RESET}, str("reset")}));
  add("SET_FLOOD_SCOPE_KEY min", {SET_FLOOD_SCOPE_KEY, 0});
  // a key read one byte early would not show here: the mock HMAC behind the
  // transport code folds only two bytes of its state into the code
  add("SET_FLOOD_SCOPE_KEY 17", cat({{SET_FLOOD_SCOPE_KEY, 0}, fill(15, 0x42)}));
  add("SET_FLOOD_SCOPE_KEY key", cat({{SET_FLOOD_SCOPE_KEY, 0}, fill(16, 0x42)}));
  add("SET_FLOOD_SCOPE_KEY unscoped", {SET_FLOOD_SCOPE_KEY, 1});
  add("SEND_CONTROL_DATA min", {SEND_CONTROL_DATA, 0x80});
  add("SET_AUTOADD_CONFIG min", {SET_AUTOADD_CONFIG, 1});
  add("SET_AUTOADD_CONFIG hops", {SET_AUTOADD_CONFIG, 1, 3});
  add("SET_DEFAULT_FLOOD_SCOPE min", {SET_DEFAULT_FLOOD_SCOPE});
  add("SET_DEFAULT_FLOOD_SCOPE 47", cat({{SET_DEFAULT_FLOOD_SCOPE}, fieldOf("scope", 31), fill(15, 0x24)}));
  add("SET_DEFAULT_FLOOD_SCOPE", cat({{SET_DEFAULT_FLOOD_SCOPE}, fieldOf("scope", 31), fill(16, 0x24)}));
  // no zero anywhere in the frame: the name runs into the key and past the end
  add("SET_DEFAULT_FLOOD_SCOPE unterminated", cat({{SET_DEFAULT_FLOOD_SCOPE}, fill(47, 0x61)}));
  add("SEND_RAW_PACKET flood", {SEND_RAW_PACKET, 0, 0x11, 0, 0x55});
  // route type 0 carries 4 transport code bytes, which tryParsePacket() reads
  // before it tests the length
  add("SEND_RAW_PACKET transport past end", {SEND_RAW_PACKET, 0, 0x00, 0});
  return c;
}

// A queued packet as text. The host's AES128 mock writes nothing, so the payload
// of an encrypted packet (and its MAC, and a transport code computed over it) is
// whatever the packet buffer held before, which depends on the allocator: on Linux
// it held bytes of an earlier node's frames. For those types only the header, the
// route, the path and the payload length are compared; their content cannot show
// a frame byte on the host anyway. Plain types are compared byte for byte.
std::string packetLine(const Bytes& p) {
  uint8_t type = (p[0] >> 2) & 0x0F;
  bool encrypted = type <= 0x02 || (type >= 0x05 && type <= 0x08);
  if (!encrypted) return hex(p);
  uint8_t route = p[0] & 0x03;
  size_t i = 1 + ((route == 0 || route == 3) ? 4 : 0);
  uint8_t path_len = p[i];
  size_t path_bytes = (path_len & 63) * ((path_len >> 6) + 1);
  size_t payload_at = i + 1 + path_bytes;
  std::ostringstream o;
  o << hex(Bytes(p.begin(), p.begin() + 1)) << " route " << (int)route << " path "
    << hex(Bytes(p.begin() + i, p.begin() + std::min(payload_at, p.size())))
    << " payload " << (p.size() > payload_at ? p.size() - payload_at : 0) << " bytes";
  return o.str();
}

// What one frame did, as text: replies, packets, files, and the counters of the
// collaborators, then what a few later steps show. /channels2 is left out, because CMD_SET_CHANNEL writes stack
// bytes behind the channel name there (see test_frame_sweep.cpp); the channel's
// content is read back through GET_CHANNEL below instead.
std::string outcome(Harness& h, const Bytes& f) {
  g_events.clear();
  int reboots = board.reboot_calls;
  int params = radio_driver.set_params_calls, power = radio_driver.tx_power_calls;
  uint32_t rtc = rtc_clock.now;
  std::ostringstream o;
  for (const Bytes& r : h.send(f)) o << "reply " << hex(r) << "\n";
  for (const Bytes& p : h.mesh().outbound()) o << "packet " << packetLine(p) << "\n";
  for (auto& file : h.fs.files)
    if (file.first != "/channels2") o << "file " << file.first << " " << hex(file.second) << "\n";
  // State that only shows later: a contact listing still running (it writes one
  // contact per loop), the channel a setter stored, and the scope the next flood
  // goes out with.
  for (int i = 0; i < 12; i++) {
    h.serial.written.clear();
    h.mesh().loop();
    for (const Bytes& r : h.serial.written) o << "later " << hex(r) << "\n";
  }
  for (const Bytes& r : h.send({GET_CHANNEL, 1})) o << "channel " << hex(r) << "\n";
  Bytes chat = {SEND_CHANNEL_TXT_MSG, 0, 0};
  putU32(chat, RTC0);
  putStr(chat, "x");
  for (const Bytes& r : h.send(chat)) o << "flood " << hex(r) << "\n";
  for (const Bytes& p : h.mesh().outbound()) o << "queued " << packetLine(p) << "\n";
  for (auto& e : g_events) o << "event " << e << "\n";
  o << "reboot " << board.reboot_calls - reboots << " params " << radio_driver.set_params_calls - params
    << " power " << radio_driver.tx_power_calls - power << " rtc " << rtc_clock.now - rtc
    << " disable " << h.serial.disable_calls << "\n";
  return o.str();
}

// A node with the peer as a contact and a signing round started, which is what
// the catalog's frames expect to find.
void prepare(Harness& h) {
  h.reset();
  h.boot();
  h.send(contactFrame(12));
  h.send({SIGN_START});
}

// The previous frame: a CMD_GET_DEVICE_TIME (which reads nothing past its code) of
// full length, so every byte above the next frame's length is one of these.
Bytes staleFrame(const Bytes& tail) {
  Bytes f = {GET_DEVICE_TIME};
  for (size_t i = 1; i < MAX_FRAME_SIZE; i++) f.push_back(tail[i % tail.size()]);
  return f;
}

// Tails that would change an outcome if a handler used them: zeros, a byte that
// is no terminator and no valid path length, all ones, and a pattern with a zero
// at every eighth byte, which ends an unterminated string at a different place.
const std::vector<Bytes> TAILS = {
  fill(1, 0x00), fill(1, 0x5A), fill(1, 0xFF),
  {0x00, 0x41, 0x42, 0x02, 0x01, 0x30, 0x3A, 0x31},
};

}  // namespace

TEST(FrameBounds, NoCommandReadsThePreviousFrame) {
  Harness h;
  for (const Case& c : catalog()) {
    prepare(h);
    std::string fresh = outcome(h, c.frame);
    for (const Bytes& tail : TAILS) {
      prepare(h);
      h.send(staleFrame(tail));
      EXPECT_EQ(outcome(h, c.frame), fresh) << c.name << ", tail " << hex(tail);
    }
  }
}

// One byte below each command's minimum the answer is UNSUPPORTED_CMD, as for a
// code that is not a command (SEND_CHANNEL_DATA, without a minimum in the table,
// answers ILLEGAL_ARG itself); the minimum itself is accepted, which the catalog
// above shows with a real frame of exactly that length for each of these codes.
TEST(FrameBounds, OneByteBelowEachMinimum) {
  const struct { uint8_t code; size_t min; } MINIMUMS[] = {
    {APP_START, 8}, {SEND_TXT_MSG, 14}, {SEND_CHANNEL_TXT_MSG, 7}, {SET_DEVICE_TIME, 5},
    {SET_ADVERT_NAME, 2}, {ADD_UPDATE_CONTACT, 136}, {SET_RADIO_PARAMS, 11},
    {SET_RADIO_TX_POWER, 2}, {RESET_PATH, 33}, {SET_ADVERT_LATLON, 9},
    {REMOVE_CONTACT, 33}, {SHARE_CONTACT, 33}, {IMPORT_CONTACT, 99}, {REBOOT, 7},
    {SET_TUNING_PARAMS, 9}, {DEVICE_QUERY, 2}, {IMPORT_PRIVATE_KEY, 65},
    {SEND_RAW_DATA, 6}, {SEND_LOGIN, 33}, {SEND_STATUS_REQ, 33}, {HAS_CONNECTION, 33},
    {LOGOUT, 33}, {GET_CONTACT_BY_KEY, 33}, {GET_CHANNEL, 2}, {SET_CHANNEL, 50},
    {SIGN_DATA, 2}, {SEND_TRACE_PATH, 11}, {SET_DEVICE_PIN, 5}, {SET_OTHER_PARAMS, 2},
    {SEND_TELEMETRY_REQ, 4}, {SET_CUSTOM_VAR, 4}, {GET_ADVERT_PATH, 34},
    {SEND_BINARY_REQ, 34}, {FACTORY_RESET, 6}, {SEND_PATH_DISCOVERY_REQ, 34},
    {SET_FLOOD_SCOPE_KEY, 2}, {SEND_CONTROL_DATA, 2}, {GET_STATS, 2}, {SEND_ANON_REQ, 34},
    {SET_AUTOADD_CONFIG, 2}, {SET_PATH_HASH_MODE, 3}, {SEND_RAW_PACKET, 4},
    {RUN_CLI_COMMAND, 3}, {SEND_CHANNEL_DATA, 4},
  };
  Harness h;
  for (const Case& c : catalog()) {
    for (auto m : MINIMUMS) {
      if (c.frame[0] != m.code || c.frame.size() != m.min) continue;
      for (const Bytes& tail : TAILS) {
        prepare(h);
        h.send(staleFrame(tail));
        Bytes shorter(c.frame.begin(), c.frame.end() - 1);
        uint8_t expected = m.code == SEND_CHANNEL_DATA ? ERR_ILLEGAL_ARG : ERR_UNSUPPORTED_CMD;
        EXPECT_EQ(h.reply(shorter), Bytes({1, expected})) << c.name << " minus one, tail " << hex(tail);
        EXPECT_EQ(h.mesh().outboundTotal(), 0) << c.name << " minus one";
      }
    }
  }
  // every minimum has a catalog frame of exactly that length
  for (auto m : MINIMUMS) {
    bool found = false;
    for (const Case& c : catalog()) found |= c.frame[0] == m.code && c.frame.size() == m.min;
    EXPECT_TRUE(found) << "no catalog frame at the minimum of code " << (int)m.code;
  }
}

// Since step 10c the handlers read through FrameReader, which counts every read
// that would have gone past the frame. Only the two commands whose path length
// byte can announce more than the frame holds may run out, and they refuse the
// frame when they do; every other command reads inside its frame at every length
// and fill, and every catalog frame except those two kinds reads inside it.
TEST(FrameBounds, OnlyAnAnnouncedPathRunsOut) {
  Harness h;
  const uint8_t fills[] = {0x00, 0x01, 0x3F, 0x43, 0x81, 0xFF};
  for (int code = 0; code < 70; code++) {
    int overruns = 0;
    for (uint8_t fill : fills) {
      for (size_t len = 1; len <= MAX_FRAME_SIZE; len++) {
        h.reset();
        h.boot();
        Bytes f(1, (uint8_t)code);
        putFill(f, len - 1, fill);
        int before = frame_reader_overruns;
        h.send(f);
        overruns += frame_reader_overruns - before;
      }
    }
    if (code != SEND_RAW_DATA && code != SEND_CHANNEL_DATA) {
      EXPECT_EQ(overruns, 0) << "code " << code;
    }
  }
  for (const Case& c : catalog()) {
    prepare(h);
    int before = frame_reader_overruns;
    h.send(c.frame);
    bool may_run_out = c.name.find("past end") != std::string::npos &&
                       (c.frame[0] == SEND_RAW_DATA || c.frame[0] == SEND_CHANNEL_DATA);
    if (!may_run_out) EXPECT_EQ(frame_reader_overruns, before) << c.name;
  }
}

// The scope key of SET_FLOOD_SCOPE_KEY is set only by a frame that carries all 16
// bytes; one byte less clears it. Which of the two happened shows in the next
// flood: with a key it carries a transport code (route type TRANSPORT_FLOOD, 0),
// without one it does not (FLOOD, 1).
TEST(FrameBounds, FloodScopeKeyNeedsAllSixteenBytes) {
  Harness h;
  for (size_t key_bytes : {15, 16}) {
    h.reset();
    h.boot();
    Bytes f = {SET_FLOOD_SCOPE_KEY, 0};
    putFill(f, key_bytes, 0x42);
    ASSERT_EQ(h.reply(f), Bytes({0}));
    Bytes chat = {SEND_CHANNEL_TXT_MSG, 0, 0};
    putU32(chat, RTC0);
    putStr(chat, "x");
    ASSERT_EQ(h.reply(chat), Bytes({0}));
    auto queued = h.mesh().outbound();
    ASSERT_EQ(queued.size(), 1u);
    EXPECT_EQ(queued[0][0] & 0x03, key_bytes == 16 ? 0 : 1) << key_bytes << " key bytes";
  }
}

// The three handlers that read past the frame before step 10c, each with a frame
// that makes them do it and behind tails that would matter if the bytes were used:
// a previous frame whose bytes complete the announced path and the payload (or end
// the unterminated scope name early). The answer is ILLEGAL_ARG every time,
// nothing is queued for the air, and no file changes (the prefs hold the default
// flood scope).
TEST(FrameBounds, TheReadsPastTheEndThatStep10cRemoved) {
  const struct { const char* name; Bytes frame; } CASES[] = {
    // path length byte 8: eight 1 byte hashes, the frame holds 2 of them
    {"SEND_CHANNEL_DATA", {SEND_CHANNEL_DATA, 0, 8, 7, 8}},
    // path length byte 8 again, the frame holds 4 bytes after it
    {"SEND_RAW_DATA", {SEND_RAW_DATA, 8, 1, 2, 3, 4}},
    // 47 bytes without a zero: name and key run together
    {"SET_DEFAULT_FLOOD_SCOPE", cat({{SET_DEFAULT_FLOOD_SCOPE}, fill(47, 0x61)})},
  };
  const std::vector<Bytes> tails = {
    fill(1, 0x00), fill(1, 0x34), fill(1, 0x61),
    {0x61, 0x62, 0x00, 0x34, 0x12, 0x01, 0x02, 0x03},
  };
  Harness h;
  for (const auto& c : CASES) {
    for (const Bytes& tail : tails) {
      prepare(h);
      h.send(staleFrame(tail));
      auto files_before = h.fs.files;
      EXPECT_EQ(h.reply(c.frame), Bytes({1, ERR_ILLEGAL_ARG})) << c.name << ", tail " << hex(tail);
      EXPECT_EQ(h.mesh().outboundTotal(), 0) << c.name << ", tail " << hex(tail);
      EXPECT_TRUE(h.fs.files == files_before) << c.name << ", tail " << hex(tail);
    }
  }
}
