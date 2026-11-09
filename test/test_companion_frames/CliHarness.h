#pragma once

// Host harness for the companion's text CLI, MyMesh::handleCommand() in
// examples/companion_radio/MyMesh.cpp, step 13d-a of the refactoring series
// (docs/refactoring/0013-cli-command-table.md).
//
// handleCommand() is private and has three callers. Two are driven here:
//
// - run():       onCLICommandRecv(), what BaseChatMesh calls for a text message from a
//                contact that may use the remote CLI. The test owns the reply buffer, so
//                it can be guarded; the real caller (BaseChatMesh.cpp) hands over the
//                bytes from offset 5 of a 166 byte array, which is 161 bytes.
// - runFrame():  CMD_RUN_CLI_COMMAND, what the app sends. The reply travels in the
//                RESP_CLI_REPLY frame; the buffer is the node's reply_buf[166].
//
// The third caller, the rescue CLI on the serial port (checkCLIRescueCmd), is a
// separate chain and not part of this step.
//
// The reply buffer of run() holds an empty string and a filler behind it, with guard
// bytes on both sides, so a command that writes beyond the buffer, or leaves the reply
// without a terminator, fails the test that issued it. A command that writes nothing
// leaves the empty string, which is what the real callers hand over too.

#include "CompanionHarness.h"

#ifdef ENABLE_WIFI_INTERFACE
#include <WiFi.h>
#endif

namespace companion {

constexpr size_t CLI_REPLY_CAPACITY = 161;   // what BaseChatMesh::onContactDataRecv gives the remote CLI
constexpr size_t CLI_GUARD = 16;
constexpr uint8_t CLI_GUARD_BYTE = 0xA5;
constexpr char CLI_FILLER = 'Z';

struct CliResult {
  std::string text;      // the reply, up to its terminator
  int saves = 0;         // how often /prefs.json was written while the command ran
  bool handled_frame = true;
};

class CliTest : public ::testing::Test {
protected:
  Harness h;

  void SetUp() override {
    h.boot();
#ifdef ENABLE_WIFI_INTERFACE
    WiFi.reset();
#endif
  }

  // One command over the air, from a contact that may use the CLI.
  CliResult run(const std::string& command, uint32_t sender_timestamp = 0) {
    uint8_t area[CLI_GUARD + CLI_REPLY_CAPACITY + CLI_GUARD];
    memset(area, CLI_GUARD_BYTE, sizeof(area));
    char* reply = reinterpret_cast<char*>(area + CLI_GUARD);
    memset(reply, CLI_FILLER, CLI_REPLY_CAPACITY);
    reply[0] = 0;

    g_events.clear();
    h.mesh().cliRemote(0x10, sender_timestamp, command.c_str(), reply);

    CliResult r;
    r.saves = eventCount("open:/prefs.json");
    for (size_t i = 0; i < CLI_GUARD; i++) {
      EXPECT_EQ(area[i], CLI_GUARD_BYTE) << "wrote before the reply buffer, command: " << command;
      EXPECT_EQ(area[CLI_GUARD + CLI_REPLY_CAPACITY + i], CLI_GUARD_BYTE)
          << "wrote after the reply buffer, command: " << command;
    }
    const void* end = memchr(reply, 0, CLI_REPLY_CAPACITY);
    EXPECT_NE(end, nullptr) << "reply is not terminated inside the buffer, command: " << command;
    r.text = end ? std::string(reply) : std::string(reply, CLI_REPLY_CAPACITY);
    return r;
  }

  // Shorthand for the reply text of a command that is expected to save nothing.
  std::string say(const std::string& command) { return run(command).text; }

  // One command through the app's frame. Returns the text of the RESP_CLI_REPLY frame.
  CliResult runFrame(const std::string& command) {
    Bytes f = {66};   // CMD_RUN_CLI_COMMAND
    putStr(f, command.c_str());
    g_events.clear();
    Bytes r = h.reply(f);
    CliResult out;
    out.saves = eventCount("open:/prefs.json");
    EXPECT_FALSE(r.empty());
    if (!r.empty()) {
      EXPECT_EQ(r[0], 29) << "RESP_CLI_REPLY";
      out.text = std::string(r.begin() + 1, r.end());
    }
    return out;
  }

  // The node restarts on the same file system: what a command saved is what it reads.
  void reboot() {
    h.boot();
#ifdef ENABLE_WIFI_INTERFACE
    WiFi.reset();
#endif
  }

  // The stored preferences, as text.
  std::string prefsFile() {
    auto it = h.fs.files.find("/prefs.json");
    if (it == h.fs.files.end()) return "<no file>";
    return std::string(it->second.begin(), it->second.end());
  }
};

}  // namespace companion
