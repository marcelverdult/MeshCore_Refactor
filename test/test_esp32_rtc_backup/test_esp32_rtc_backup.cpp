// The RTC backup of ESP32RTCClock (src/helpers/ESP32Board.h): setCurrentTime()
// and tick() save the time in RTC memory that a reset does not clear, begin()
// restores it after a reset that lost the system time. The backup has to be one
// object for the whole image, so the tests write it in one translation unit
// (other_unit.cpp) and read it in another (this one), in both directions.

#include <gtest/gtest.h>
#include <time.h>

#include <helpers/ESP32Board.h>

void other_unit_set_time(uint32_t time);
void other_unit_tick();
void other_unit_begin();

namespace {

const uint32_t SAVED_TIME = RTC_TIME_MIN + 123456;

class Esp32RtcBackupTest : public ::testing::Test {
protected:
  void SetUp() override {
    // RTC_NOINIT memory holds anything after power-on; start without a backup.
    _rtc_backup_time = 0;
    _rtc_backup_magic = 0;
    g_esp_reset_reason = ESP_RST_POWERON;
    g_settimeofday_calls = 0;
    g_settimeofday_sec = 0;
  }

  // A reset that lost the system time, then begin() in this unit.
  void resetAndBeginHere(esp_reset_reason_t reason = ESP_RST_SW) {
    g_esp_reset_reason = reason;
    g_settimeofday_calls = 0;
    ESP32RTCClock clock;
    clock.begin();
  }
};

TEST_F(Esp32RtcBackupTest, BeginRestoresTimeSetInAnotherUnit) {
  other_unit_set_time(SAVED_TIME);
  resetAndBeginHere();
  EXPECT_EQ(g_settimeofday_calls, 1);
  EXPECT_EQ(g_settimeofday_sec, (time_t)SAVED_TIME);
}

TEST_F(Esp32RtcBackupTest, BeginInAnotherUnitRestoresTimeSetHere) {
  ESP32RTCClock clock;
  clock.setCurrentTime(SAVED_TIME);
  g_esp_reset_reason = ESP_RST_PANIC;
  g_settimeofday_calls = 0;
  other_unit_begin();
  EXPECT_EQ(g_settimeofday_calls, 1);
  EXPECT_EQ(g_settimeofday_sec, (time_t)SAVED_TIME);
}

TEST_F(Esp32RtcBackupTest, BeginRestoresTimeSavedByTickInAnotherUnit) {
  // tick() saves the system time, here the host's, which is after RTC_TIME_MIN.
  time_t before = time(NULL);
  other_unit_tick();
  time_t after = time(NULL);
  ASSERT_GT(before, (time_t)RTC_TIME_MIN);
  resetAndBeginHere(ESP_RST_TASK_WDT);
  EXPECT_EQ(g_settimeofday_calls, 1);
  EXPECT_GE(g_settimeofday_sec, before);
  EXPECT_LE(g_settimeofday_sec, after);
}

TEST_F(Esp32RtcBackupTest, SetCurrentTimeSetsSystemTimeAndBackup) {
  ESP32RTCClock clock;
  clock.setCurrentTime(SAVED_TIME);
  EXPECT_EQ(g_settimeofday_calls, 1);
  EXPECT_EQ(g_settimeofday_sec, (time_t)SAVED_TIME);
  EXPECT_EQ(_rtc_backup_time, SAVED_TIME);
  EXPECT_EQ(_rtc_backup_magic, (uint32_t)RTC_BACKUP_MAGIC);
}

TEST_F(Esp32RtcBackupTest, BeginWithoutBackupSeedsMinimumTime) {
  resetAndBeginHere(ESP_RST_POWERON);
  EXPECT_EQ(g_settimeofday_calls, 1);
  EXPECT_EQ(g_settimeofday_sec, (time_t)RTC_TIME_MIN);
}

TEST_F(Esp32RtcBackupTest, BeginIgnoresBackupWithoutMagic) {
  _rtc_backup_time = SAVED_TIME;
  _rtc_backup_magic = RTC_BACKUP_MAGIC ^ 1;
  resetAndBeginHere();
  EXPECT_EQ(g_settimeofday_sec, (time_t)RTC_TIME_MIN);
}

TEST_F(Esp32RtcBackupTest, BeginIgnoresBackupBeforeMinimumTime) {
  _rtc_backup_time = RTC_TIME_MIN - 1;
  _rtc_backup_magic = RTC_BACKUP_MAGIC;
  resetAndBeginHere();
  EXPECT_EQ(g_settimeofday_sec, (time_t)RTC_TIME_MIN);
}

TEST_F(Esp32RtcBackupTest, BeginAfterDeepSleepKeepsSystemTime) {
  other_unit_set_time(SAVED_TIME);
  resetAndBeginHere(ESP_RST_DEEPSLEEP);
  EXPECT_EQ(g_settimeofday_calls, 0);
}

TEST_F(Esp32RtcBackupTest, TickSavesSystemTimeOverOlderBackup) {
  // setCurrentTime() saves any time; tick() saves the system time when it is
  // after RTC_TIME_MIN and differs from the backup, as the host's clock does.
  other_unit_set_time(RTC_TIME_MIN - 10);
  EXPECT_EQ(_rtc_backup_time, (uint32_t)(RTC_TIME_MIN - 10));
  ESP32RTCClock clock;
  clock.tick();
  EXPECT_GT(_rtc_backup_time, (uint32_t)RTC_TIME_MIN);
}

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
