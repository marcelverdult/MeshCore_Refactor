// Every command code at every frame length, against a recorded transcript.
//
// For each code 0 to 255, each length 1 to MAX_FRAME_SIZE and four fill bytes, a
// freshly booted node gets one frame: the code, then the fill byte repeated. What
// comes back (every reply frame, byte for byte) and what changed on the node's
// collaborators is one line of the transcript; consecutive lengths with the same
// line are folded into a range. A last line per code and fill carries a digest of
// every packet the node queued and of the file system after each frame, so content
// that the reply does not show is pinned too. The result has to match
// test/test_companion_frames/frame_sweep.golden exactly.
//
// The four fills reach the sub-commands a code selects by its second byte: 0x00 and
// 0x01 are the two flood scope modes and the path hash mode's reserved byte, 0x81
// has the bit CMD_SEND_CONTROL_DATA requires, and 0xFF is the flood path length of
// CMD_SEND_CHANNEL_DATA and an out of range index everywhere else.
//
// A fifth pass sends zeros again, but only after a full length frame of 0x5A bytes
// (a CMD_GET_DEVICE_TIME, which reads nothing past its code). Whatever a branch
// reads above the frame's own length then comes from that earlier frame, as it
// does on a board, so a branch that reads past `len` shows up as a difference
// between the "00" and the "00/5a" lines of the same code.
//
// The transcript records the protocol as it is, including what is odd about it; it
// is the reference a restructured handleCmdFrame() must reproduce. To regenerate it
// after an intended protocol change, run the suite with COMPANION_SWEEP_UPDATE=1 and
// review the diff of the golden file like code.

#include "CompanionHarness.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

using namespace companion;

namespace {

const char* GOLDEN = "test/test_companion_frames/frame_sweep.golden";
std::string strprintf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  return buf;
}

struct Pass {
  uint8_t fill;
  bool stale_tail;   // a full length frame of 0x5A bytes goes first
  const char* name;
};
const Pass PASSES[] = {
  {0x00, false, "00"}, {0x01, false, "01"}, {0x81, false, "81"}, {0xFF, false, "ff"},
  {0x00, true, "00/5a"},
};

// FNV-1a, so a long run of content folds into one comparable number.
struct Digest {
  uint64_t h = 0xcbf29ce484222325ull;
  void add(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; }
  }
  void add(const std::string& s) { add((const uint8_t*)s.data(), s.size()); }
  void add(const Bytes& b) { add(b.data(), b.size()); }
  std::string str() const {
    char buf[20];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
  }
};

// /channels2 holds records of 4 reserved bytes, a 32 byte name and a 32 byte
// secret. CMD_SET_CHANNEL builds the record in a ChannelDetails on the stack and
// copies the name with StrHelper::strncpy(), which stops at the terminator, so the
// bytes behind the name are whatever the stack held and are written to flash as
// they are (on a board as well). Nothing reads them back, since the name is used
// as a C string, but they would make the digest depend on what ran before. They
// are zeroed here, and only here.
Bytes withoutNameTails(const std::string& name, Bytes data) {
  if (name != "/channels2") return data;
  for (size_t rec = 0; rec + 68 <= data.size(); rec += 68) {
    uint8_t* n = &data[rec + 4];
    size_t end = 0;
    while (end < 32 && n[end]) end++;
    for (size_t i = end; i < 32; i++) n[i] = 0;
  }
  return data;
}

std::string fsDigest(const fs::FS& fs) {
  std::string d;
  for (auto& f : fs.files) {
    d += f.first;
    d += ':';
    d += hex(withoutNameTails(f.first, f.second));
    d += ';';
  }
  return d;
}

// Fills the stack below the caller with one byte value. The handlers build structs
// on the stack and not all of them initialise every byte, so without this a reply
// or a file could depend on whatever ran before, and the sweep would pass or fail
// with the test order. With it, any such byte is the same 0xC3 on every run and is
// pinned like any other.
__attribute__((noinline)) void scrubStack() {
  volatile uint8_t junk[16 * 1024];
  for (size_t i = 0; i < sizeof(junk); i++) junk[i] = 0xC3;
}

// What one frame did, as one line. The reply bytes are exact; the side effects are
// flags, because their content is pinned by the per command tests.
std::string outcome(Harness& h, const Bytes& f, Digest& packets, Digest& files) {
  std::string before = fsDigest(h.fs);
  uint32_t rtc_before = rtc_clock.now;
  int params_before = radio_driver.set_params_calls;
  int power_before = radio_driver.tx_power_calls;

  scrubStack();
  auto replies = h.send(f);

  std::string line;
  for (size_t i = 0; i < replies.size(); i++) {
    if (i) line += ' ';
    line += replies[i].empty() ? "-" : hex(replies[i]);
  }
  if (replies.empty()) line = "(none)";

  std::string flags;
  if (fsDigest(h.fs) != before) flags += 'F';               // something was written to flash
  if (h.mesh().outboundTotal() > 0) flags += 'Q';           // a packet was queued for the air
  if (board.reboot_calls) flags += 'B';                     // the board was rebooted
  if (radio_driver.set_params_calls != params_before) flags += 'P';   // radio parameters applied
  if (radio_driver.tx_power_calls != power_before) flags += 'W';      // TX power applied
  if (rtc_clock.now != rtc_before) flags += 'T';            // the clock was set
  if (h.serial.disable_calls) flags += 'D';                 // the interface was switched off
  if (!flags.empty()) line += " [" + flags + "]";

  // The content behind the Q and F flags, folded per code and pass: which packet
  // was queued and what was stored, at every length.
  for (const Bytes& p : h.mesh().outbound()) packets.add(p);
  packets.add("|");
  files.add(fsDigest(h.fs));
  return line;
}

std::string sweep() {
  Harness h;
  std::ostringstream out;
  for (int code = 0; code < 256; code++) {
    for (const Pass& pass : PASSES) {
      Digest packets, files;
      std::string prev;
      int from = 0;
      auto flush = [&](int to) {
        char head[40];
        if (from == to) snprintf(head, sizeof(head), "%02x %s %d", code, pass.name, from);
        else snprintf(head, sizeof(head), "%02x %s %d-%d", code, pass.name, from, to);
        out << head << ": " << prev << "\n";
      };
      for (int len = 1; len <= MAX_FRAME_SIZE; len++) {
        h.reset();
        h.boot();
        if (pass.stale_tail) {
          Bytes tail(1, 5);   // CMD_GET_DEVICE_TIME
          putFill(tail, MAX_FRAME_SIZE - 1, 0x5A);
          h.send(tail);
        }
        Bytes f(1, (uint8_t)code);
        putFill(f, len - 1, pass.fill);
        std::string o = outcome(h, f, packets, files);
        if (len == 1) { prev = o; from = 1; continue; }
        if (o != prev) { flush(len - 1); prev = o; from = len; }
      }
      flush(MAX_FRAME_SIZE);
      out << strprintf("%02x %s packets %s files %s\n", code, pass.name,
                       packets.str().c_str(), files.str().c_str());
    }
  }
  return out.str();
}

}  // namespace

TEST(FrameSweep, MatchesTheRecordedTranscript) {
  std::string actual = sweep();

  if (getenv("COMPANION_SWEEP_UPDATE")) {
    std::ofstream(GOLDEN) << actual;
    GTEST_SKIP() << "wrote " << GOLDEN;
  }

  std::ifstream in(GOLDEN);
  ASSERT_TRUE(in.good()) << GOLDEN << " is missing; run with COMPANION_SWEEP_UPDATE=1";
  std::stringstream golden;
  golden << in.rdbuf();

  if (actual == golden.str()) return;

  // Name the first lines that differ, so a failure says which code and length.
  std::istringstream a(actual), g(golden.str());
  std::string la, lg;
  int shown = 0, lineno = 0;
  while (shown < 10) {
    bool ha = (bool)std::getline(a, la), hg = (bool)std::getline(g, lg);
    lineno++;
    if (!ha && !hg) break;
    if (la != lg) {
      ADD_FAILURE() << "line " << lineno << "\n  golden: " << (hg ? lg : "(end)")
                    << "\n  actual: " << (ha ? la : "(end)");
      shown++;
    }
  }
  FAIL() << "the frame sweep differs from " << GOLDEN;
}
