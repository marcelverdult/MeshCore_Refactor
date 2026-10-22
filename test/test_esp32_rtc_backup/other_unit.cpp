// A second translation unit that uses ESP32RTCClock, as a variant's target.cpp
// and src/helpers/radiolib/RadioLibTarget.cpp would if both did. The test in
// test_esp32_rtc_backup.cpp calls these and checks that begin(), setCurrentTime()
// and tick() see the same RTC backup whichever unit they run in.

#include <helpers/ESP32Board.h>

void other_unit_set_time(uint32_t time) {
  ESP32RTCClock clock;
  clock.setCurrentTime(time);
}

void other_unit_tick() {
  ESP32RTCClock clock;
  clock.tick();
}

void other_unit_begin() {
  ESP32RTCClock clock;
  clock.begin();
}
