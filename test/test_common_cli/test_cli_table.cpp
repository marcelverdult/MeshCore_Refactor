// Step 13b: cases the table rows can get wrong and the 13a tests did not reach.
// Found by mutating the table (see docs/refactoring/0013-cli-command-table.md).
// New file; the 13a tests are unchanged.

#include <gtest/gtest.h>

#include "CliHarness.h"

TEST_F(CliTest, LogStartStopEraseOnTheSerialConsoleAreNotTheDump) {
  // "log start" is above the serial-only "log" row, so it wins on serial too
  EXPECT_EQ(run("log start", 0), "   logging on");
  EXPECT_EQ(run("log stop", 0), "   logging off");
  EXPECT_EQ(run("log erase", 0), "   log erased");
  EXPECT_EQ(takeCalls(), (Calls{"setLoggingOn true", "setLoggingOn false", "eraseLogFile"}));
}

TEST_F(CliTest, ClockAnswersOverTheAirToo) {
  EXPECT_EQ(run("clock", 9), clockText(1760000000, ""));
  EXPECT_EQ(run("clockwork", 9), clockText(1760000000, ""));
}

TEST_F(CliTest, GetKeyNeedsTheWholeNameNotJustItsFirstLetters) {
  EXPECT_EQ(run("get loopfoo"), "??: loopfoo");
  EXPECT_EQ(run("get loop.detec"), "??: loop.detec");
  EXPECT_EQ(run("get loop.detectfoo"), "> moderate");        // the tail is ignored, pinned as is
}
