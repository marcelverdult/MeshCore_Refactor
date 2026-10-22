#pragma once

// The companion on the host, driven through its frame protocol. A test hands a
// command frame to a fake serial interface, runs one MyMesh::loop() (which is where
// the firmware picks frames up), and gets back every frame the node wrote. Nothing
// reaches into the node: what the protocol cannot report is observed on its
// collaborators (the file system, the radio, the board, the clock, the packets
// queued for the air).
//
// Each node is built in zeroed storage. On a board, MyMesh is a global and starts
// zeroed, and the bytes of cmd_frame above the current frame's length are whatever
// an earlier frame left there. Zeroed storage makes that tail the same on every run,
// so a reply never depends on what the heap held before.

#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <Arduino.h>
#include <target.h>
#include <EventLog.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <LittleFS.h>

#include "MyMesh.h"

namespace companion {

using Bytes = std::vector<uint8_t>;

// Stands in for the BLE, USB or WiFi interface. checkRecvFrame() returns what
// ArduinoSerialInterface returns for a complete frame: its bytes, copied into the
// node's cmd_frame, and its length.
class FakeSerial : public BaseSerialInterface {
public:
  std::deque<Bytes> inbound;
  std::vector<Bytes> written;
  bool enabled = false;
  int disable_calls = 0;

  void enable() override { enabled = true; }
  void disable() override { enabled = false; disable_calls++; }
  bool isEnabled() const override { return enabled; }
  bool isConnected() const override { return true; }
  bool isWriteBusy() const override { return false; }

  size_t writeFrame(const uint8_t src[], size_t len) override {
    written.emplace_back(src, src + len);
    return len;
  }

  size_t checkRecvFrame(uint8_t dest[]) override {
    if (inbound.empty()) return 0;
    Bytes f = inbound.front();
    inbound.pop_front();
    memcpy(dest, f.data(), f.size());
    return f.size();
  }
};

// Opens up what the destructor needs: MyMesh allocates its clock and packet manager
// with new and never frees them, which is fine for a global and not for thousands
// of nodes in one test binary.
class Node : public MyMesh {
public:
  using MyMesh::MyMesh;
  int outboundTotal() const { return _mgr->getOutboundTotal(); }
  // The packets waiting for the air, as they would be transmitted.
  std::vector<Bytes> outbound() {
    std::vector<Bytes> out;
    for (int i = 0; i < _mgr->getOutboundTotal(); i++) {
      uint8_t raw[MAX_TRANS_UNIT + 8];
      int n = _mgr->getOutboundByIdx(i)->writeTo(raw);
      out.emplace_back(raw, raw + n);
    }
    return out;
  }
  void freeOwned() {
    delete static_cast<StaticPoolPacketManager*>(_mgr);
    delete static_cast<ArduinoMillis*>(_ms);
  }
};

constexpr uint32_t T0 = 1000;
constexpr uint32_t RTC0 = 1760000000;

class Harness {
public:
  // The RP2040 data store formats the LittleFS global (CMD_FACTORY_RESET), so the
  // node runs on that instance.
  fs::FS& fs = LittleFS;
  StdRNG rng;
  SimpleMeshTables tables;
  FakeSerial serial;
  std::unique_ptr<DataStore> store;

  Harness() { reset(); }
  ~Harness() { destroy(); }

  void reset() {
    destroy();
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock = MockRTCClock();   // also clears the base's last unique timestamp
    rtc_clock.now = RTC0;
    sensors.node_lat = sensors.node_lon = 0;
    fs.reset();
    rng.begin(1);
    g_events.clear();
    serial = FakeSerial();
  }

  // Boots a node the way main.cpp does: the data store, then the mesh, then the
  // interface. The file system is left as it is, so building a node again after
  // a test stored something is a reboot.
  Node& boot() {
    destroy();
    store.reset(new DataStore(fs, rtc_clock));
    storage = std::unique_ptr<unsigned char[]>(new unsigned char[sizeof(Node) + alignof(Node)]);
    memset(storage.get(), 0, sizeof(Node) + alignof(Node));
    void* p = storage.get();
    size_t space = sizeof(Node) + alignof(Node);
    p = std::align(alignof(Node), sizeof(Node), p, space);
    node = new (p) Node(radio_driver, rng, rtc_clock, tables, *store);
    store->begin();
    node->begin();
    node->startInterface(serial);
    serial.written.clear();
    g_events.clear();
    return *node;
  }

  Node& mesh() { return *node; }

  // One frame in, the frames the node wrote while handling it out.
  std::vector<Bytes> send(const Bytes& frame) {
    EXPECT_GE(frame.size(), 1u);
    EXPECT_LE(frame.size(), (size_t)MAX_FRAME_SIZE);
    serial.written.clear();
    serial.inbound.push_back(frame);
    node->loop();
    return serial.written;
  }

  // The only reply to a frame, or an empty frame if there was not exactly one.
  Bytes reply(const Bytes& frame) {
    auto out = send(frame);
    EXPECT_EQ(out.size(), 1u) << "expected one reply frame";
    return out.size() == 1 ? out[0] : Bytes();
  }

private:
  std::unique_ptr<unsigned char[]> storage;
  Node* node = nullptr;

  void destroy() {
    if (node) {
      node->freeOwned();
      node->~Node();
      node = nullptr;
    }
    storage.reset();
  }
};

// ---- frame building -----------------------------------------------------------

inline Bytes frame(std::initializer_list<uint8_t> b) { return Bytes(b); }

inline void put(Bytes& f, const void* src, size_t n) {
  const uint8_t* s = static_cast<const uint8_t*>(src);
  f.insert(f.end(), s, s + n);
}
inline void putU8(Bytes& f, uint8_t v) { f.push_back(v); }
inline void putU16(Bytes& f, uint16_t v) { put(f, &v, 2); }
inline void putU32(Bytes& f, uint32_t v) { put(f, &v, 4); }
inline void putI32(Bytes& f, int32_t v) { put(f, &v, 4); }
inline void putFill(Bytes& f, size_t n, uint8_t v) { f.insert(f.end(), n, v); }
inline void putStr(Bytes& f, const char* s) { put(f, s, strlen(s)); }
// A fixed width, zero padded text field.
inline void putField(Bytes& f, const char* s, size_t width) {
  Bytes b(width, 0);
  memcpy(b.data(), s, std::min(strlen(s), width));
  put(f, b.data(), width);
}

inline uint32_t u32At(const Bytes& b, size_t at) {
  uint32_t v = 0;
  if (at + 4 <= b.size()) memcpy(&v, &b[at], 4);
  return v;
}
inline uint16_t u16At(const Bytes& b, size_t at) {
  uint16_t v = 0;
  if (at + 2 <= b.size()) memcpy(&v, &b[at], 2);
  return v;
}

inline std::string hex(const Bytes& b) {
  static const char* digits = "0123456789abcdef";
  std::string s;
  for (uint8_t c : b) { s += digits[c >> 4]; s += digits[c & 15]; }
  return s;
}

}  // namespace companion
