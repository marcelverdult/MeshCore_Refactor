// Tests for the two parsers that turn raw bytes into a mesh::Packet:
// Dispatcher::tryParsePacket(), which every received radio packet and every
// CMD_SEND_RAW_PACKET frame of the companion goes through, and Packet::readFrom(),
// which restores stored adverts, imports contacts and receives bridge packets.
//
// Both used to read the header fields before testing the length (see
// docs/fixes/packet-parse-len.md). The tests here pin two things:
//
// - Neither parser touches a byte at or above `len`. The input sits at the end of a
//   readable page with an unreadable page behind it, so such a read ends the
//   process; each case runs in a child process, so it shows as a failed case.
// - Every input is accepted or refused exactly as by the code before the fix, and an
//   accepted packet is field for field the same. The code before the fix is kept
//   below as the reference, verbatim except for its debug output; it runs on a
//   buffer that is large enough for every read it makes.

#include <gtest/gtest.h>

#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <vector>

#include <Dispatcher.h>
#include <Packet.h>

namespace {

typedef std::vector<uint8_t> Bytes;

// tryParsePacket() as of integration 07fa9c5c.
bool legacyTryParsePacket(mesh::Packet* pkt, const uint8_t* raw, int len) {
  int i = 0;

  pkt->header = raw[i++];
  if (pkt->getPayloadVer() > PAYLOAD_VER_1) {
    return false;
  }

  if (pkt->hasTransportCodes()) {
    memcpy(&pkt->transport_codes[0], &raw[i], 2); i += 2;
    memcpy(&pkt->transport_codes[1], &raw[i], 2); i += 2;
  } else {
    pkt->transport_codes[0] = pkt->transport_codes[1] = 0;
  }

  pkt->path_len = raw[i++];
  uint8_t path_mode = pkt->path_len >> 6;  // upper 2 bits (legacy firmware: 00)
  if (path_mode == 3) {   // Reserved for future
    return false;
  }

  uint8_t path_byte_len = (pkt->path_len & 63) * pkt->getPathHashSize();
  if (path_byte_len > MAX_PATH_SIZE || i + path_byte_len > len) {
    return false;
  }

  memcpy(pkt->path, &raw[i], path_byte_len); i += path_byte_len;

  pkt->payload_len = len - i;  // payload is remainder
  if (pkt->payload_len > sizeof(pkt->payload)) {
    return false;
  }

  memcpy(pkt->payload, &raw[i], pkt->payload_len);

  return true;  // success
}

// Packet::readFrom() as of integration 07fa9c5c.
bool legacyReadFrom(mesh::Packet* pkt, const uint8_t src[], uint8_t len) {
  uint8_t i = 0;
  pkt->header = src[i++];
  if (pkt->hasTransportCodes()) {
    memcpy(&pkt->transport_codes[0], &src[i], 2); i += 2;
    memcpy(&pkt->transport_codes[1], &src[i], 2); i += 2;
  } else {
    pkt->transport_codes[0] = pkt->transport_codes[1] = 0;
  }
  pkt->path_len = src[i++];
  if (!mesh::Packet::isValidPathLen(pkt->path_len)) return false;   // bad encoding

  uint8_t bl = pkt->getPathByteLen();
  memcpy(pkt->path, &src[i], bl); i += bl;

  if (i >= len) return false;   // bad encoding
  pkt->payload_len = len - i;
  if (pkt->payload_len > sizeof(pkt->payload)) return false;  // bad encoding
  memcpy(pkt->payload, &src[i], pkt->payload_len); //i += payload_len;
  return true;   // success
}

// What a caller can see of an accepted packet.
Bytes fields(const mesh::Packet& p) {
  Bytes b;
  auto put = [&b](const void* v, size_t n) {
    const uint8_t* s = (const uint8_t*)v;
    b.insert(b.end(), s, s + n);
  };
  put(&p.header, sizeof(p.header));
  put(p.transport_codes, sizeof(p.transport_codes));
  put(&p.path_len, sizeof(p.path_len));
  put(p.path, p.getPathByteLen());
  put(&p.payload_len, sizeof(p.payload_len));
  put(p.payload, p.payload_len);
  return b;
}

class FakeRadio : public mesh::Radio {
public:
  Bytes next;        // the next packet recvRaw() delivers, once
  uint8_t stale = 0; // what the rest of the receive buffer holds
  int recvRaw(uint8_t* bytes, int sz) override {
    memset(bytes, stale, sz);
    int n = (int)next.size();
    memcpy(bytes, next.data(), n);
    next.clear();
    return n;
  }
  uint32_t getEstAirtimeFor(int len_bytes) override { return 10; }
  float packetScore(float snr, int packet_len) override { return 1.0f; }
  bool startSendRaw(const uint8_t* bytes, int len) override { return false; }
  bool isSendComplete() override { return true; }
  void onSendFinished() override { }
  bool isInRecvMode() const override { return true; }
};

class FakeClock : public mesh::MillisecondClock {
public:
  unsigned long getMillis() override { return 0; }
};

// One packet in the pool; counts how often it comes back.
class OnePacketManager : public mesh::PacketManager {
public:
  mesh::Packet pkt;
  bool in_use = false;
  int frees = 0;
  mesh::Packet* allocNew() override {
    if (in_use) return nullptr;
    in_use = true;
    return &pkt;
  }
  void free(mesh::Packet* packet) override { in_use = false; frees++; }
  void queueOutbound(mesh::Packet* packet, uint8_t priority, uint32_t scheduled_for) override { }
  mesh::Packet* getNextOutbound(uint32_t now) override { return nullptr; }
  int getOutboundCount(uint32_t now) const override { return 0; }
  int getOutboundTotal() const override { return 0; }
  int getFreeCount() const override { return in_use ? 0 : 1; }
  mesh::Packet* getOutboundByIdx(int i) override { return nullptr; }
  mesh::Packet* removeOutboundByIdx(int i) override { return nullptr; }
  void queueInbound(mesh::Packet* packet, uint32_t scheduled_for) override { }
  mesh::Packet* getNextInbound(uint32_t now) override { return nullptr; }
};

// Records the fields of every packet the receive path hands on.
class RecordingDispatcher : public mesh::Dispatcher {
public:
  std::vector<Bytes> received;
  RecordingDispatcher(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::PacketManager& mgr)
    : mesh::Dispatcher(radio, ms, mgr) { }
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override {
    received.push_back(fields(*pkt));
    return ACTION_RELEASE;
  }
};

const uint8_t kRoutes[] = {
  ROUTE_TYPE_TRANSPORT_FLOOD, ROUTE_TYPE_FLOOD, ROUTE_TYPE_DIRECT, ROUTE_TYPE_TRANSPORT_DIRECT,
};

// Path length bytes: no path, short and long paths of each hash size, the largest
// count, and the reserved mode 3.
const uint8_t kPathBytes[] = { 0x00, 0x01, 0x05, 0x3f, 0x40, 0x41, 0x60, 0x7f, 0x80, 0x95, 0xbf, 0xc0, 0xff };

int headerLen(uint8_t header) {
  uint8_t route = header & PH_ROUTE_MASK;
  return (route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT) ? 5 : 1;
}

// A packet of `len` bytes: the header, then a counting pattern, with the path
// length byte (if it fits) replaced by `path_byte`.
Bytes makeRaw(uint8_t header, uint8_t path_byte, int len) {
  Bytes b(len);
  for (int i = 0; i < len; i++) b[i] = (uint8_t)(0x11 + 37 * i);
  if (len > 0) b[0] = header;
  int at = headerLen(header);
  if (at < len) b[at] = path_byte;
  return b;
}

// `len` bytes at the very end of a readable page, followed by a page that cannot
// be read.
class GuardedBuffer {
  size_t _page;
  uint8_t* _base;
public:
  GuardedBuffer() {
    _page = (size_t)sysconf(_SC_PAGESIZE);
    _base = (uint8_t*)mmap(nullptr, 2 * _page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (_base == MAP_FAILED) abort();
    if (mprotect(_base + _page, _page, PROT_NONE) != 0) abort();
  }
  ~GuardedBuffer() { munmap(_base, 2 * _page); }
  const uint8_t* place(const Bytes& b) {
    uint8_t* p = _base + _page - b.size();
    memset(_base, 0xA5, _page);
    memcpy(p, b.data(), b.size());
    return p;
  }
};

const int kMaxGuardedLen = 1 + 4 + 1 + MAX_PATH_SIZE + 2;   // past the longest path

}  // namespace

namespace {

// A dispatcher to call tryParsePacket() on; the parser uses none of its state.
struct Parser {
  FakeRadio radio;
  FakeClock clock;
  OnePacketManager mgr;
  RecordingDispatcher d{radio, clock, mgr};
};

// A read of the guard page ends the child with exit code 3 instead of a crash, so
// the system crash reporter is not started for every failing case.
void exitOnFault(int) { _exit(3); }

// Runs every path length byte of one route and length through a parser on a
// guarded buffer, then exits 0. A read at or above `len` ends it with code 3.
void parseGuarded(uint8_t route, int len, bool read_from) {
  signal(SIGSEGV, exitOnFault);
  signal(SIGBUS, exitOnFault);
  uint8_t header = route | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
  GuardedBuffer g;
  Parser p;
  mesh::Packet pkt;
  for (uint8_t pb : kPathBytes) {
    Bytes raw = makeRaw(header, pb, len);
    if (read_from) {
      pkt.readFrom(g.place(raw), (uint8_t)len);
    } else {
      p.d.tryParsePacket(&pkt, g.place(raw), len);
    }
  }
  exit(0);
}

}  // namespace

// Every route type, every length up to past the longest path, the path length
// bytes above: a read at or above `len` kills the child, which fails the case.
TEST(PacketParseDeathTest, TryParsePacketReadsNothingPastLen) {
  for (uint8_t route : kRoutes) {
    for (int len = 0; len <= kMaxGuardedLen; len++) {
      EXPECT_EXIT(parseGuarded(route, len, false), testing::ExitedWithCode(0), "")
        << "route " << (int)route << " len " << len;
    }
  }
}

TEST(PacketParseDeathTest, ReadFromReadsNothingPastLen) {
  for (uint8_t route : kRoutes) {
    for (int len = 0; len <= kMaxGuardedLen; len++) {
      EXPECT_EXIT(parseGuarded(route, len, true), testing::ExitedWithCode(0), "")
        << "route " << (int)route << " len " << len;
    }
  }
}

namespace {

// Lengths up to past the longest header and path, and around the payload limit.
std::vector<int> referenceLengths(int max_len) {
  std::vector<int> lens;
  for (int len = 0; len <= kMaxGuardedLen + 8; len++) lens.push_back(len);
  for (int len = 180; len <= max_len; len++) lens.push_back(len);
  return lens;
}

// The same input in a buffer large enough for every read the reference makes,
// with the bytes behind `len` set to `stale`.
const uint8_t* padded(uint8_t* buf, size_t size, const Bytes& raw, uint8_t stale) {
  memset(buf, stale, size);
  memcpy(buf, raw.data(), raw.size());
  return buf;
}

const uint8_t kStale[] = { 0x00, 0xff };

// The parsers tell headers apart by version and route type only; each of the 16
// pairs once, with a payload type that differs between them.
std::vector<uint8_t> referenceHeaders() {
  std::vector<uint8_t> h;
  for (int ver = 0; ver < 4; ver++)
    for (int route = 0; route < 4; route++)
      h.push_back((uint8_t)(ver << PH_VER_SHIFT | ((ver * 4 + route) % 16) << PH_TYPE_SHIFT | route));
  return h;
}

}  // namespace

// Every version and route type, every path length byte, lengths 0 to 80 and 180 to
// 256, two fills behind `len`: accepted and refused as before, and accepted packets equal.
TEST(PacketParse, TryParsePacketMatchesReference) {
  uint8_t buf[512];
  Parser p;
  long accepted = 0, refused = 0;
  for (int header : referenceHeaders()) {
    for (int pb = 0; pb < 256; pb++) {
      for (int len : referenceLengths(MAX_TRANS_UNIT + 1)) {
        Bytes raw = makeRaw((uint8_t)header, (uint8_t)pb, len);
        for (uint8_t stale : kStale) {
          mesh::Packet want, got;
          bool w = legacyTryParsePacket(&want, padded(buf, sizeof(buf), raw, stale), len);
          bool g = p.d.tryParsePacket(&got, padded(buf, sizeof(buf), raw, stale), len);
          ASSERT_EQ(w, g) << "header " << header << " path " << pb << " len " << len << " stale " << (int)stale;
          if (w) {
            ASSERT_EQ(fields(want), fields(got)) << "header " << header << " path " << pb << " len " << len;
            accepted++;
          } else {
            refused++;
          }
        }
      }
    }
  }
  // both outcomes are reached in large numbers, so the comparison is not vacuous
  EXPECT_GT(accepted, 50000);
  EXPECT_GT(refused, 50000);
}

TEST(PacketParse, ReadFromMatchesReference) {
  uint8_t buf[512];
  long accepted = 0, refused = 0;
  for (int header : referenceHeaders()) {
    for (int pb = 0; pb < 256; pb++) {
      for (int len : referenceLengths(255)) {
        Bytes raw = makeRaw((uint8_t)header, (uint8_t)pb, len);
        for (uint8_t stale : kStale) {
          mesh::Packet want, got;
          bool w = legacyReadFrom(&want, padded(buf, sizeof(buf), raw, stale), (uint8_t)len);
          bool g = got.readFrom(padded(buf, sizeof(buf), raw, stale), (uint8_t)len);
          ASSERT_EQ(w, g) << "header " << header << " path " << pb << " len " << len << " stale " << (int)stale;
          if (w) {
            ASSERT_EQ(fields(want), fields(got)) << "header " << header << " path " << pb << " len " << len;
            accepted++;
          } else {
            refused++;
          }
        }
      }
    }
  }
  EXPECT_GT(accepted, 50000);
  EXPECT_GT(refused, 50000);
}

// The radio receive path: a packet the reference accepts reaches onRecvPacket()
// with the same fields, any other is put back into the pool, whatever the rest of
// the receive buffer holds.
TEST(PacketParse, RadioReceiveMatchesReference) {
  uint8_t buf[512];
  int accepted = 0, refused = 0;
  for (int header : referenceHeaders()) {
    for (uint8_t pb : kPathBytes) {
      for (int len : referenceLengths(MAX_TRANS_UNIT)) {
        if (len == 0) continue;   // recvRaw() returning 0 means nothing was received
        Bytes raw = makeRaw((uint8_t)header, pb, len);
        for (uint8_t stale : kStale) {
          FakeRadio radio;
          FakeClock clock;
          OnePacketManager mgr;
          RecordingDispatcher d(radio, clock, mgr);
          radio.next = raw;
          radio.stale = stale;
          d.loop();

          mesh::Packet want;
          bool w = legacyTryParsePacket(&want, padded(buf, sizeof(buf), raw, stale), len);
          SCOPED_TRACE(testing::Message() << "header " << header << " path " << (int)pb << " len " << len);
          ASSERT_EQ(mgr.frees, 1);   // put back either way: refused, or released by onRecvPacket()
          if (w) {
            ASSERT_EQ(d.received.size(), 1u);
            ASSERT_EQ(d.received[0], fields(want));
            accepted++;
          } else {
            ASSERT_EQ(d.received.size(), 0u);
            refused++;
          }
        }
      }
    }
  }
  EXPECT_GT(accepted, 1000);
  EXPECT_GT(refused, 1000);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
