// CommonCLI: the three memory risks named in docs/refactoring/0013-cli-command-table.md
// (step 13c). Before the fix each of these wrote past a buffer or dereferenced NULL.
//
// 1. Commands that copy their argument into the 132 byte CommonCLI::tmp without a
//    length check: "tempradio", "sensor set" and, on LR2021 builds, "set extra.sf".
// 2. (The "gps" status compared getSettingByKey("gps") without a NULL check. Upstream #3753
//    replaced that code with CommonGpsPrefs::handleCommand, so the test is gone.)
// 3. An unknown "get <key>" echoed the key into the 160 byte reply with sprintf().

#include <gtest/gtest.h>

#include "CliHarness.h"

// A CommonCLI placed in a buffer with a canary area behind it. tmp is the last member,
// so an overflow of tmp lands in the canary instead of in whatever the heap holds.
class MemoryCliTest : public CliTest {
protected:
  static const size_t kCanary = 512;
  alignas(CommonCLI) unsigned char mem[sizeof(CommonCLI) + kCanary];
  CommonCLI* guarded = nullptr;

  void SetUp() override {
    CliTest::SetUp();
    memset(mem, 0xA5, sizeof(mem));
    guarded = new (mem) CommonCLI(brd, clk, sens, regions, acl, &prefs, &cb);
  }

  bool canaryIntact() const {
    for (size_t i = 0; i < kCanary; i++) {
      if (mem[sizeof(CommonCLI) + i] != 0xA5) return false;
    }
    return true;
  }

  // Like CliTest::run(), but on the guarded CommonCLI, and with a command of any length.
  std::string runGuarded(const std::string& command, bool* reply_clean = nullptr) {
    struct Guarded { char pre[16]; char reply[160]; char post[16]; } g;
    memset(g.pre, 0xA5, sizeof(g.pre));
    memset(g.post, 0xA5, sizeof(g.post));
    memset(g.reply, 0, sizeof(g.reply));
    std::vector<char> buf(command.size() + 16, 0);
    memcpy(buf.data(), command.data(), command.size());
    guarded->handleCommand(0, buf.data(), g.reply);
    bool clean = true;
    for (size_t i = 0; i < sizeof(g.pre); i++) {
      clean = clean && (uint8_t)g.pre[i] == 0xA5 && (uint8_t)g.post[i] == 0xA5;
    }
    if (reply_clean) *reply_clean = clean;
    return std::string(g.reply, strnlen(g.reply, sizeof(g.reply)));
  }
};

// ---- 1. tmp ------------------------------------------------------------------------

TEST_F(MemoryCliTest, TempradioWithATooLongArgumentIsRefusedAndWritesNothingPastTmp) {
  std::string cmd = "tempradio 869.618,62.5,8,6,5 " + std::string(300, '1');
  EXPECT_EQ(runGuarded(cmd), "Error, invalid params");
  EXPECT_TRUE(canaryIntact()) << "tempradio wrote past CommonCLI::tmp";
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(MemoryCliTest, TempradioLimitIsTheSizeOfTmp) {
  // arg is the text after "tempradio ": 131 characters fit tmp with its terminator.
  std::string ok_arg = "869.618,62.5,8,6,5";
  ok_arg.resize(131, ' ');
  EXPECT_EQ(runGuarded("tempradio " + ok_arg), "OK - temp params for 5 mins");
  EXPECT_TRUE(canaryIntact());
  EXPECT_EQ(runGuarded("tempradio " + ok_arg + " "), "Error, invalid params");
  EXPECT_TRUE(canaryIntact());
}

TEST_F(MemoryCliTest, SensorSetWithATooLongArgumentIsRefusedAndWritesNothingPastTmp) {
  sens.settings = {{"alpha", "1"}};
  EXPECT_EQ(runGuarded("sensor set alpha " + std::string(300, 'x')), "can't find custom var");
  EXPECT_TRUE(canaryIntact()) << "sensor set wrote past CommonCLI::tmp";
  EXPECT_TRUE(sens.set_calls.empty());
}

TEST_F(MemoryCliTest, SensorSetWithAnArgumentThatFitsTmpStillWorks) {
  sens.settings = {{"alpha", "1"}};
  EXPECT_EQ(runGuarded("sensor set alpha " + std::string(100, 'x')), "ok");
  EXPECT_EQ(sens.set_calls, (std::vector<std::string>{"alpha=" + std::string(100, 'x')}));
  EXPECT_TRUE(canaryIntact());
}

#if defined(USE_LR2021)
TEST_F(MemoryCliTest, SetExtraSfWithATooLongArgumentIsRefusedAndWritesNothingPastTmp) {
  EXPECT_EQ(runGuarded("set extra.sf 7," + std::string(300, '9')), "Invalid extra SF config");
  EXPECT_TRUE(canaryIntact()) << "set extra.sf wrote past CommonCLI::tmp";
  EXPECT_TRUE(takeCalls().empty());
}
#endif

// ---- 3. get <unknown key> -------------------------------------------------------------

// The reply is "??: " plus the key: 4 characters of prefix, the terminator, so the key
// fits up to 155 characters.
TEST_F(MemoryCliTest, GetOfAnUnknownKeyEchoesAKeyThatFits) {
  std::string key(155, 'k');
  bool clean = false;
  EXPECT_EQ(runGuarded("get " + key, &clean), "??: " + key);
  EXPECT_TRUE(clean);
}

TEST_F(MemoryCliTest, GetOfAnUnknownKeyTruncatesALongKeyInsteadOfOverflowingTheReply) {
  for (size_t n : {156u, 157u, 200u, 400u}) {
    std::string key(n, 'k');
    bool clean = false;
    std::string r = runGuarded("get " + key, &clean);
    EXPECT_TRUE(clean) << "reply buffer overrun with a key of " << n << " characters";
    EXPECT_EQ(r, "??: " + key.substr(0, 155)) << n;
  }
}

