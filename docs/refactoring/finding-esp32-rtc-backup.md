# Finding: the RTC backup of ESP32RTCClock is defined in every unit

Status: fix on branch `upstream-candidate/esp32-rtc-backup-once` (based on
`upstream/dev` 46216b2d). Upstream PR candidate, not submitted. No behaviour
change today, see below.

## What happens

`src/helpers/ESP32Board.h` defined the backup that `ESP32RTCClock` restores after
a reset as `static`:

```cpp
static RTC_NOINIT_ATTR uint32_t _rtc_backup_time;
static RTC_NOINIT_ATTR uint32_t _rtc_backup_magic;
```

`static` at namespace scope gives every translation unit that includes the header
its own pair. `begin()`, `setCurrentTime()` and `tick()` are inline members, so
each unit that calls them works on its own pair. That breaks the one definition
rule (an inline function naming an object with internal linkage, defined in
several units): a call the compiler inlines uses the pair of its unit, a call that
is not inlined goes to whichever out of line copy the linker kept.

Today exactly one unit per image uses the members: the variant's `target.cpp`, or
`XiaoC6Board.cpp` / `UnitC6LBoard.cpp` for the two C6 board files (checked with
`git grep` on 46216b2d: no variant names `ESP32RTCClock` or `fallback_clock` in a
second `.cpp`). The day a second unit calls one of them, `begin()` can restore a
pair that `setCurrentTime()` never wrote, and the time no longer survives a reset,
without any error.

## What changes

- `ESP32Board.h` declares the two variables `extern`, with a comment why.
- `ESP32Board.cpp` defines them once, with `RTC_NOINIT_ATTR`, inside its existing
  `#ifdef ESP_PLATFORM`.

Every ESP32 environment compiles `ESP32Board.cpp` (`+<helpers/*.cpp>` in the
resolved `build_src_filter` of all 337 ESP32 envs, `pio project config`), so the
definition is always linked. A second definition would not link (`multiple
definition`), a missing one neither (`undefined reference`).

## Evidence (on upstream/dev 46216b2d and this branch)

- Host test, not committed (upstream does not want test scaffolding for changes
  this small, see the review on #3643): suite `test_esp32_rtc_backup` from the
  fork PR #39 (37cd6a04), its ESP-IDF stand-ins extended locally by `<stdio.h>`
  and a `Serial` stub for the larger `ESP32Board` of 46216b2d. The real
  `ESP32Board.h`/`.cpp`, members called from two translation units: 46216b2d
  5 of 9 failed, this fix 9 of 9 passed.
- `pio test -e native`: 46 of 46.
- Builds before and after, `nm` on `firmware.elf`: Heltec_v2_repeater,
  Tbeam_SX1262_companion_radio_ble, Station_G2_repeater (ESP32);
  Heltec_v3_repeater, Heltec_v3_companion_radio_ble, heltec_v4_repeater,
  T_Beam_S3_Supreme_SX1262_repeater (S3); Xiao_C3_repeater,
  Xiao_C3_companion_radio_ble (C3); Xiao_C6_repeater_,
  M5Stack_Unit_C6L_companion_radio_ble, LilyGo_Tlora_C6_repeater_ (C6). All
  build, RAM and flash are the same, both variables keep their addresses in
  `.rtc_noinit` (0x50000200/04 ESP32, 0x50000000/04 S3, 0x50000018/1c C3,
  0x50000048/4c C6); binding changes from local `b` (`_ZL16_rtc_backup_time`)
  to global `B`. From the addresses, a time saved by the old image should be
  restored by the new one; not tested across a firmware update.
- ThinkNode_M7_repeater and ThinkNode_M9_repeater_ never call
  `fallback_clock.begin()`. Before, `_rtc_backup_magic` is not in the image
  (the unit only writes it); after, it is kept (+24 bytes flash) and
  `_rtc_backup_time` moves from 0x50000000 to 0x50000004. These images never
  restore the backup, before or after, so nothing is lost.
- Generic_ESPNOW_comp_radio_usb does not build on 46216b2d without this change
  (`ESP32Board.h:73: 'P_LORA_DIO_1' was not declared`), unrelated.
- Heltec V3 (`heltecv3-dutch`), stock `Heltec_v3_repeater` of 46216b2d and of
  this branch, each confirmed with `verify-flash`: boots, `advert` -> `OK -
  Advert sent`, `time <now>`, then `reboot` (`RTC_SW_CPU_RST`), `clock` shows
  the time set before the reboot. Same result before and after, as expected for
  a change without behaviour change today.
