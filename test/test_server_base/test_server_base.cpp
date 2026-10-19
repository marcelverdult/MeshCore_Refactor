// Characterization tests for the server underpinnings the three roles carry in
// triplicate (see docs/refactoring/0004-shared-server-base.md). They run the real
// role implementation on the host against the mocks in test/mocks, so the recorded
// behaviour is the firmware's, not a copy of its formulas.
//
// This file pins the temporary radio parameters, which step 4c moves into the
// shared base: what `set radio ...` schedules, when the pending values reach the
// radio, that they reach it once, and when the node reverts to its own settings.
// The room server stands in for the three roles here; the repeater and the sensor
// need an environment of their own, because both of them also define `MyMesh`.

#include <gtest/gtest.h>

#include <Arduino.h>
#include <target.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>

#include "MyMesh.h"

namespace {

// The delay the role gives the CLI to answer before the pending values are applied.
constexpr uint32_t APPLY_DELAY_MS = 2000;

// The clock the role starts from in these tests.
constexpr uint32_t T0 = 1000;

// Dispatcher::millisHasNowPassed() is `(long)(now - timestamp) > 0`, so a timer set
// for T fires at T + 1, not at T. That one millisecond is pinned deliberately: it is
// the observable edge of every scheduled action in the roles, and the signed
// difference is what makes the comparison survive the millis() rollover.

class ServerBaseTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;

  // Built the way the role's own main.cpp builds it.
  std::unique_ptr<MyMesh> mesh;

  float orig_freq = 0, orig_bw = 0;
  uint8_t orig_sf = 0, orig_cr = 0;

  void SetUp() override {
    g_mock_millis = T0;   // not 0, so a scheduled time is distinguishable from "off"
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);

    mesh.reset(new MyMesh(board, radio_driver, ms, rng, rtc_clock, tables));
    mesh->begin(&fs);

    // begin() pushes the node's own settings to the radio; remember them as the
    // values a revert has to restore, without naming the defaults here.
    orig_freq = radio_driver.freq;
    orig_bw = radio_driver.bw;
    orig_sf = radio_driver.sf;
    orig_cr = radio_driver.cr;
    radio_driver.set_params_calls = 0;
  }
};

TEST_F(ServerBaseTest, BeginPushesTheNodesOwnSettingsToTheRadio) {
  EXPECT_GT(orig_freq, 0.0f);
  EXPECT_GT(orig_bw, 0.0f);
  EXPECT_GT(orig_sf, 0);
}

TEST_F(ServerBaseTest, TempParamsAreNotAppliedBeforeTheReplyDelay) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 3);

  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, 0);   // still inside the delay
  EXPECT_FLOAT_EQ(radio_driver.freq, orig_freq);

  // The scheduled moment itself is still too early.
  g_mock_millis = T0 + APPLY_DELAY_MS;
  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, 0);
  EXPECT_FLOAT_EQ(radio_driver.freq, orig_freq);
}

TEST_F(ServerBaseTest, TempParamsReachTheRadioOneMillisecondAfterTheDelay) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 3);

  g_mock_millis = T0 + APPLY_DELAY_MS + 1;
  mesh->loop();

  EXPECT_EQ(radio_driver.set_params_calls, 1);
  EXPECT_FLOAT_EQ(radio_driver.freq, 867.5f);
  EXPECT_FLOAT_EQ(radio_driver.bw, 125.0f);
  EXPECT_EQ(radio_driver.sf, 11);
  EXPECT_EQ(radio_driver.cr, 6);
}

TEST_F(ServerBaseTest, TempParamsAreAppliedOnlyOnce) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 3);

  g_mock_millis = T0 + APPLY_DELAY_MS + 1;
  mesh->loop();
  ASSERT_EQ(radio_driver.set_params_calls, 1);

  mesh->loop();
  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, 1);   // the timer is cleared when it fires
}

TEST_F(ServerBaseTest, TheNodeRevertsToItsOwnSettingsAfterTheTimeout) {
  const uint32_t timeout_mins = 3;
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, (int)timeout_mins);

  g_mock_millis = T0 + APPLY_DELAY_MS + 1;
  mesh->loop();
  ASSERT_FLOAT_EQ(radio_driver.freq, 867.5f);

  // The revert is scheduled from the time of the command, not from the apply, and
  // it is the same strictly-greater comparison.
  const uint32_t revert_at = T0 + APPLY_DELAY_MS + timeout_mins * 60 * 1000;
  g_mock_millis = revert_at;
  mesh->loop();
  EXPECT_FLOAT_EQ(radio_driver.freq, 867.5f);   // the scheduled moment is still early

  g_mock_millis = revert_at + 1;
  mesh->loop();
  EXPECT_FLOAT_EQ(radio_driver.freq, orig_freq);
  EXPECT_FLOAT_EQ(radio_driver.bw, orig_bw);
  EXPECT_EQ(radio_driver.sf, orig_sf);
  EXPECT_EQ(radio_driver.cr, orig_cr);
}

TEST_F(ServerBaseTest, RevertHappensOnlyOnce) {
  mesh->applyTempRadioParams(867.5f, 125.0f, 11, 6, 1);

  g_mock_millis = T0 + APPLY_DELAY_MS + 60 * 1000 + 1;
  mesh->loop();
  ASSERT_FLOAT_EQ(radio_driver.freq, orig_freq);   // applied and reverted in one loop
  const int after_revert = radio_driver.set_params_calls;

  mesh->loop();
  mesh->loop();
  EXPECT_EQ(radio_driver.set_params_calls, after_revert);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
