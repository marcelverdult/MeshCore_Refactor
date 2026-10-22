#pragma once

// Stand-in for the parts of the ESP32 Arduino core and ESP-IDF that
// src/helpers/ESP32Board.h and ESP32Board.cpp name, so both compile on the host.
// Only ESP32RTCClock is exercised: esp_reset_reason() returns what a test sets,
// and settimeofday() is redirected to a recorder, so a test sees which time
// begin() and setCurrentTime() set without touching the host's clock. The other
// declarations only have to compile.

#include <stdint.h>
#include <sys/time.h>

// esp_attr.h: on the ESP32 a section in RTC memory that a reset does not clear;
// on the host ordinary (zero initialized) memory.
#define RTC_NOINIT_ATTR

typedef enum {
  ESP_RST_UNKNOWN,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
} esp_reset_reason_t;

inline esp_reset_reason_t g_esp_reset_reason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return g_esp_reset_reason; }
inline void esp_restart() { }

// settimeofday() as ESP32RTCClock calls it: records the time instead of setting
// the host's clock. <sys/time.h> is included above, so the header's own include
// of it does not see the macro.
inline int g_settimeofday_calls = 0;
inline time_t g_settimeofday_sec = 0;
inline int esp32_test_settimeofday(const struct timeval* tv, const void*) {
  g_settimeofday_calls++;
  g_settimeofday_sec = tv->tv_sec;
  return 0;
}
#define settimeofday esp32_test_settimeofday

typedef int gpio_num_t;
enum { GPIO_INTR_POSEDGE = 1, GPIO_INTR_HIGH_LEVEL = 5 };
#ifndef HIGH
#define HIGH 1
#endif
inline int gpio_get_level(gpio_num_t) { return 0; }
inline void gpio_wakeup_enable(gpio_num_t, int) { }
inline void gpio_wakeup_disable(gpio_num_t) { }
inline void gpio_set_intr_type(gpio_num_t, int) { }
inline void esp_sleep_enable_timer_wakeup(uint64_t) { }
inline void esp_sleep_enable_gpio_wakeup() { }
inline void esp_light_sleep_start() { }
enum { ESP_SLEEP_WAKEUP_ALL = 0 };
inline void esp_sleep_disable_wakeup_source(int) { }
inline void esp_deep_sleep_start() { }
inline void rtc_gpio_hold_en(gpio_num_t) { }
inline void gpio_hold_en(gpio_num_t) { }
inline void digitalWrite(int, int) { }

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

inline float temperatureRead() { return 0.0f; }
