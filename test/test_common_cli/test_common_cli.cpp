// The text CLI on the host. See CliHarness.h and docs/refactoring/0013-cli-command-table.md.
// The tests of each command group are in their own file; this one holds main().

#include <gtest/gtest.h>

#include "CliHarness.h"

TEST_F(CliTest, smoke) {
  EXPECT_EQ(run("ver"), "v9.9.9 (Build: 01 Jan 2030)");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
