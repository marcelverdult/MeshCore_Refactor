# 0012: One RTC backup for ESP32RTCClock

Status: implemented. Stacked on cut D2 of step 11
([0011](0011-variant-target.md), section "D2: what the code commit changes"), where
the trap was found. It fixes a definition, not a behaviour: every ESP32 image keeps
its one copy of the backup at the address it had, except two that never restore it
(below).

## Context

`ESP32RTCClock` (`src/helpers/ESP32Board.h`) keeps the time across a reset that
loses the system time: `setCurrentTime()` and `tick()` write it to
`_rtc_backup_time` and `_rtc_backup_magic` in RTC memory (`RTC_NOINIT_ATTR`, the
section `.rtc_noinit`, which a reset does not clear), and `begin()` restores it when
the magic matches.

The header defined both as `static`:

```cpp
static RTC_NOINIT_ATTR uint32_t _rtc_backup_time;
static RTC_NOINIT_ATTR uint32_t _rtc_backup_magic;
```

`static` at namespace scope gives every translation unit that includes the header
its own pair. The members that use them are inline, so each unit that calls them
compiles a copy of `begin()`, `setCurrentTime()` and `tick()` against its own pair.
That is a violation of the one definition rule (an inline function that names an
object with internal linkage, defined in several units), and what an image does with
it depends on the compiler and the linker:

- A call the compiler inlines reads and writes the pair of the unit it is in.
- A call that is not inlined goes to the one out of line copy the linker keeps of
  each inline member, which uses the pair of whichever unit that copy came from.

Today it does no harm because exactly one unit per image uses the members: the
variant's `target.cpp` before cut D, `src/helpers/radiolib/RadioLibTarget.cpp` after
it for the 22 variants of D2, `XiaoC6Board.cpp` and `UnitC6LBoard.cpp` for the two
C6 board files. The day a second unit calls one of them (a variant that starts the
clock in its board file and sets it in `target.cpp`, a helper that saves the time
before a deep sleep), `begin()` can read a pair that `setCurrentTime()` never wrote,
and the time no longer survives a reset, with no error and no warning.

## What changes

- `ESP32Board.h` declares the two variables `extern`, with a comment why.
- `ESP32Board.cpp` defines them once, with `RTC_NOINIT_ATTR`, inside its existing
  `#ifdef ESP_PLATFORM`.

Names, types and the section stay the same, so code that uses the variables keeps
compiling. With one definition the linker enforces the rest: a second definition
does not link (`multiple definition`), a missing one does not either (`undefined
reference`).

## Alternatives

- **Define them in a file of their own** (`src/helpers/ESP32RTCBackup.cpp`). Built
  first and measured: the ESP32 images are the same as with `ESP32Board.cpp`, but
  the new object file, empty on every other platform, is one more input to every
  link, and on RP2040 that alone reorders the linker's long branch veneers
  (`PicoW_repeater`: 157 bytes differ; built without the file, the image equals the
  start commit's byte for byte). `ESP32Board.cpp` is compiled for every platform
  already, so the other platforms' images stay byte for byte the same.
- **`inline` variables** (C++17, `inline RTC_NOINIT_ATTR uint32_t ...` in the
  header). Not built. Rejected: the definition would still be in every unit that
  uses it, with the linker choosing one, and `RTC_NOINIT_ATTR` gives each unit its
  own section name (`.rtc_noinit.<__COUNTER__>`); which copy lands where is the kind
  of link question this change removes.
- **Move the backup into `ESP32RTCClock`** as static members. Rejected for this
  step: it renames the variables for anyone who uses them, and it still needs one
  out of line definition, so it would be this change plus a rename.

## The address in `.rtc_noinit`

A time saved by the old image is read by the new one only if both variables keep
their address. The address follows from the link: `.rtc_noinit` collects
`*(.rtc_noinit .rtc_noinit.*)` in input order, without `KEEP`. Before, every unit
that included the header carried the pair's sections (the link maps list them for
`main.cpp.o`, `MyMesh.cpp.o`, `UITask.cpp.o` and others), and `--gc-sections` dropped
all but those of the one unit that uses the members; after, they come from
`ESP32Board.cpp.o`. ESP-IDF's own variables there (`s_rtc_last_ticks`,
`s_esp_rtc_time_us`) come from archives linked after the project's objects. Within
the object, the order of the two is the compiler's.

Measured with `objdump -t` on every ESP32 image of the matrix below:

- In 55 of the 57 ESP32 images, before and after: exactly one `_rtc_backup_time` and
  one `_rtc_backup_magic`, at the same addresses (for example `0x50000000`/`0x50000004` on the
  S3, `0x50000200`/`0x50000204` on the ESP32, `0x50000018`/`0x5000001c` on the C3,
  `0x50000048`/`0x5000004c` on the C6), and every other symbol in `.rtc_noinit` at
  the same address too. The order of the two is not the same in every image (time
  first in `LilyGo_TLora_V2_1_1_6` and the three C3 builds, magic first elsewhere),
  and each image keeps its own.
- In `ThinkNode_M7_repeater` and `ThinkNode_M9_repeater_`, the address of
  `_rtc_backup_time` changes, from `0x50000000` to `0x50000004`, and
  `_rtc_backup_magic` appears at `0x50000000`. These two variants never call
  `fallback_clock.begin()` (`variants/thinknode_m7/target.cpp`,
  `variants/thinknode_m9/target.cpp`), so the image never reads the backup: with a
  `static` pair, the compiler saw `_rtc_backup_magic` written and never read in the
  unit and dropped it with its stores (disassembly: before, `setCurrentTime()`
  stores the time only; after, it stores both). Nothing is restored on these two
  boards before or after, so the moved address loses nothing. The ESP-IDF variables
  after them keep their addresses (`s_rtc_last_ticks` at `0x50000008`,
  `s_esp_rtc_time_us` at `0x50000010`), since the 4 bytes of the magic fill what was
  alignment padding.

## Verification

### Tests first

`test/test_esp32_rtc_backup` (`pio test -e native_esp32_rtc_backup`, added to the
unit test workflow): the real `ESP32Board.h` and `ESP32Board.cpp` on the host,
against stand ins for the ESP-IDF headers they include (`test/mocks/esp32_idf/`:
`esp_reset_reason()` returns what a test sets, `settimeofday()` records instead of
setting the host's clock, `RTC_NOINIT_ATTR` is empty) and for the variant's
`target.h`. The members are called from two translation units, the test and
`other_unit.cpp`, in both directions: a time set in one is restored by `begin()` in
the other, also when `tick()` saved it, and the backup the test reads directly is
the one the members wrote. Further tests pin `begin()`: no backup or a wrong magic
seeds `RTC_TIME_MIN`, a time before `RTC_TIME_MIN` is not restored, a wake from deep
sleep keeps the system time.

On the start commit (`12e37302`, the tests with the firmware sources of
`a0511463`) 5 of the 9 tests fail: the test's unit sees its own zero pair. The 4
cross unit tests that call members in both units pass there, because at `-O0`
nothing is inlined and the host linker kept the out of line members of
`other_unit.cpp`, so all calls agreed on that unit's pair (as the 5 failures,
all reading the test unit's pair, show): the hazard depends on
inlining, which is why the tests also read the pair directly. On the code commit
(`836678d2`) all 9 pass.

`e6072261` adds the suite to `test_ignore` of the `native` environment, which would
otherwise pick it up without the stand ins; it changes no firmware source.
`24e3ead4` includes `<time.h>` in the stand ins: on macOS `<sys/time.h>` declares
`time()`, with glibc it does not, and the unit test workflow on Linux failed to
compile the suite without it. It changes no firmware source either.

### Host suites

All 23 native environments: 716 tests pass (707 on the head of D2 plus the 9 new).

### Mutation tests

| mutation | result |
|---|---|
| M1: the start commit's `ESP32Board.h` and `ESP32Board.cpp` (`static` in the header) | 5 tests fail |
| M2: `setCurrentTime()` does not write the magic | 3 tests fail |
| M3: `begin()` ignores the magic | 1 test fails |
| M4: `begin()` restores after a wake from deep sleep | 1 test fails |
| M5: `tick()` saves nothing | 2 tests fail |
| M6: `begin()` without the lower bound `RTC_TIME_MIN` | 1 test fails |
| M7: `begin()` accepts a time equal to `RTC_TIME_MIN` | all pass: equivalent, restoring `RTC_TIME_MIN` and seeding it set the same time |
| M8: `_rtc_backup_magic` not defined | does not link (`Undefined symbols ... _rtc_backup_magic`) |

### Build matrix

64 environments, built from `12e37302` and `836678d2` in the same worktree with
`SOURCE_DATE_EPOCH=1760000000`: one per ESP32 variant folder (44; all 44 use
`ESP32RTCClock`), every role of `heltec_v3` (repeater, both bridges, room server,
terminal chat, companion USB, BLE and WiFi, sensor, KISS modem), the SX1268 builds
of `generic-e22` and `meshadventurer`, `RAK_3112_repeater_bridge_espnow`, more C6
builds (`Xiao_C6_companion_radio_ble_`, `Meshimi_repeater_`,
`M5Stack_Unit_C6L_companion_radio_ble`), and nRF52, RP2040 and STM32 controls
(`RAK_4631_repeater`, `RAK_4631_companion_radio_ble`, `ProMicro_repeater`,
`PicoW_repeater`, `wio-e5-mini_repeater`).

- 62 build on both commits. `Generic_ESPNOW_terminal_chat` and
  `SenseCapIndicator-ESPNow_comp_radio_usb` fail on both, with the same error
  (`'P_LORA_DIO_1' was not declared in this scope` in `ESP32Board.h`, a board
  without LoRa); not touched here.
- **nRF52, RP2040 and STM32: the same image, byte for byte** (5 of 5).
- **ESP32, C3 and C6: flash and RAM unchanged** in 55 of 57; `ThinkNode_M7` +28 and
  `ThinkNode_M9` +24 bytes of flash (the magic stores the compiler now keeps), RAM
  unchanged.
- The ESP32, S3 and C6 images (52) differ only in the 64 or 65 bytes that hash the ELF
  (at `0xb0` and at the end of the image), so their contents are the same. The three
  C3 builds (`Heltec_ct62`, `Tenstar_C3_sx1262`, `Xiao_C3`) differ in 16 or 17 more bytes,
  all in `ESP32RTCClock::tick()`: same length, same instructions apart from how the
  address of `_rtc_backup_time` is formed (before `lui a5,0x50000; add a5,a5,24;
  lw a3,0(a5)`, after `lui a4,0x50000; lw a3,24(a4)`, because a global symbol's
  offset can be folded into the load), and the registers that follow from it.
- `.rtc_noinit`: as in the section above.
- The two C6 board files (`variants/xiao_c6/XiaoC6Board.cpp`,
  `variants/m5stack_unit_c6l/UnitC6LBoard.cpp`) are the one unit that uses the
  members in their images, as `target.cpp` is elsewhere; their five builds above
  have one copy at `0x50000048`/`0x5000004c` before and after. They need no change.

No hardware evidence (without board). A board run would set the time on an ESP32
with the old image, flash the new one, and read the time after the reset that
follows the flash: the same, not `RTC_TIME_MIN`.

## Follow-up outside this step

`ThinkNode_M7` and `ThinkNode_M9` never call `fallback_clock.begin()`, so on these
two boards the time is not restored after a reset, even without the trap above.
Whether that is meant
(both have an RTC chip that `AutoDiscoverRTCClock` may find) is a question for the
boards' maintainers, not a refactoring.

<details>
<summary>All 64 environments</summary>

| Environment | Flash before | Flash after | Flash delta | RAM before | RAM after | RAM delta | image | backup before | backup after |
|---|---:|---:|---:|---:|---:|---:|---|---|---|
| `Ebyte_EoRa-S3_Repeater` | 1,108,433 | 1,108,433 | +0 | 60,500 | 60,500 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Generic_E22_sx1262_repeater` | 1,143,573 | 1,143,573 | +0 | 60,620 | 60,620 | +0 | differs (65 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `Generic_ESPNOW_terminal_chat` | | | | | | | does not build on either commit | | |
| `Heltec_ct62_repeater` | 1,151,874 | 1,151,874 | +0 | 53,772 | 53,772 | +0 | differs (81 bytes) | time @ 0x50000018, magic @ 0x5000001c | magic @ 0x5000001c, time @ 0x50000018 |
| `Heltec_E213_repeater` | 1,162,241 | 1,162,241 | +0 | 71,940 | 71,940 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_E290_repeater` | 1,160,261 | 1,160,261 | +0 | 72,124 | 72,124 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `heltec_rc32_without_display_repeater` | 1,162,121 | 1,162,121 | +0 | 62,176 | 62,176 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_T190_repeater_` | 1,139,373 | 1,139,373 | +0 | 71,872 | 71,872 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_Wireless_Tracker_repeater` | 1,157,521 | 1,157,521 | +0 | 61,088 | 61,088 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `heltec_tracker_v2_repeater` | 1,226,209 | 1,226,209 | +0 | 74,352 | 74,352 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v2_repeater` | 1,151,901 | 1,151,901 | +0 | 60,604 | 60,604 | +0 | differs (65 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `Heltec_v3_repeater` | 1,174,961 | 1,174,961 | +0 | 62,512 | 62,512 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `heltec_v4_repeater` | 1,173,089 | 1,173,089 | +0 | 62,484 | 62,484 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `heltec_v4_r8_repeater` | 1,176,589 | 1,176,589 | +0 | 62,400 | 62,400 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_Wireless_Paper_repeater` | 1,121,873 | 1,121,873 | +0 | 60,376 | 60,376 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `LilyGo_T3S3_sx1262_repeater` | 1,108,537 | 1,108,537 | +0 | 60,500 | 60,500 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `LilyGo_T3S3_sx1276_repeater` | 1,105,521 | 1,105,521 | +0 | 60,484 | 60,484 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `LilyGo_TBeam_1W_repeater` | 1,154,973 | 1,154,973 | +0 | 72,752 | 72,752 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Tbeam_SX1262_repeater` | 1,205,157 | 1,205,157 | +0 | 61,212 | 61,212 | +0 | differs (64 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `Tbeam_SX1276_repeater` | 1,201,113 | 1,201,113 | +0 | 61,092 | 61,092 | +0 | differs (65 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `T_Beam_S3_Supreme_SX1262_repeater` | 1,155,517 | 1,155,517 | +0 | 61,072 | 61,072 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `LilyGo_TDeck_repeater` | 1,144,713 | 1,144,713 | +0 | 61,424 | 61,424 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `LilyGo_TETH_Elite_sx1262_repeater` | 1,076,289 | 1,076,289 | +0 | 59,408 | 59,408 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `LilyGo_Tlora_C6_repeater_` | 542,449 | 542,449 | +0 | 29,856 | 29,856 | +0 | differs (64 bytes) | time @ 0x5000004c, magic @ 0x50000048 | magic @ 0x50000048, time @ 0x5000004c |
| `LilyGo_TLora_V2_1_1_6_repeater` | 1,663,977 | 1,663,977 | +0 | 62,472 | 62,472 | +0 | differs (65 bytes) | time @ 0x50000200, magic @ 0x50000204 | magic @ 0x50000204, time @ 0x50000200 |
| `M5Stack_Unit_C6L_repeater` | 552,821 | 552,821 | +0 | 29,752 | 29,752 | +0 | differs (65 bytes) | time @ 0x5000004c, magic @ 0x50000048 | magic @ 0x50000048, time @ 0x5000004c |
| `Meshadventurer_sx1262_repeater` | 1,162,029 | 1,162,029 | +0 | 61,084 | 61,084 | +0 | differs (65 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `meshnology_w12_repeater` | 1,183,449 | 1,183,449 | +0 | 62,800 | 62,800 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `nibble_screen_connect_repeater_` | 1,103,209 | 1,103,209 | +0 | 60,216 | 60,216 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `nibble_zero_connect_repeater_` | 1,103,301 | 1,103,301 | +0 | 60,216 | 60,216 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `RAK_3112_repeater` | 1,165,765 | 1,165,765 | +0 | 62,424 | 62,424 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `SenseCapIndicator-ESPNow_comp_radio_usb` | | | | | | | does not build on either commit | | |
| `Station_G2_repeater` | 1,207,885 | 1,207,885 | +0 | 74,712 | 74,712 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Station_G3_ESP32_repeater` | 1,209,469 | 1,209,469 | +0 | 74,720 | 74,720 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Tenstar_C3_sx1262_repeater` | 1,152,378 | 1,152,378 | +0 | 53,796 | 53,796 | +0 | differs (82 bytes) | time @ 0x50000018, magic @ 0x5000001c | magic @ 0x5000001c, time @ 0x50000018 |
| `ThinkNode_M2_Repeater` | 1,110,181 | 1,110,181 | +0 | 58,160 | 58,160 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `ThinkNode_M5_Repeater` | 1,127,465 | 1,127,465 | +0 | 63,648 | 63,648 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `ThinkNode_M7_repeater` | 1,089,241 | 1,089,269 | +28 | 59,688 | 59,688 | +0 | differs: length 1089664 -> 1089680 | time @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `ThinkNode_M9_repeater_` | 1,182,977 | 1,183,001 | +24 | 62,144 | 62,144 | +0 | differs: length 1183392 -> 1183424 | time @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Waveshare_S3_LR1121_repeater` | 1,084,869 | 1,084,869 | +0 | 59,868 | 59,868 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Xiao_C3_repeater` | 1,158,046 | 1,158,046 | +0 | 54,076 | 54,076 | +0 | differs (82 bytes) | time @ 0x50000018, magic @ 0x5000001c | magic @ 0x5000001c, time @ 0x50000018 |
| `Xiao_C6_repeater_` | 542,479 | 542,479 | +0 | 29,856 | 29,856 | +0 | differs (64 bytes) | time @ 0x5000004c, magic @ 0x50000048 | magic @ 0x50000048, time @ 0x5000004c |
| `Xiao_S3_repeater` | 1,137,605 | 1,137,605 | +0 | 61,680 | 61,680 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Xiao_S3_WIO_repeater` | 1,127,969 | 1,127,969 | +0 | 61,320 | 61,320 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_repeater_bridge_rs232` | 1,177,429 | 1,177,429 | +0 | 64,096 | 64,096 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_repeater_bridge_espnow` | 1,185,425 | 1,185,425 | +0 | 64,168 | 64,168 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_room_server` | 1,170,101 | 1,170,101 | +0 | 66,200 | 66,200 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_terminal_chat` | 602,317 | 602,317 | +0 | 95,868 | 95,868 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_companion_radio_usb` | 658,149 | 658,149 | +0 | 103,404 | 103,404 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_companion_radio_ble` | 1,671,593 | 1,671,593 | +0 | 194,844 | 194,844 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_companion_radio_wifi` | 1,107,797 | 1,107,797 | +0 | 172,384 | 172,384 | +0 | differs (64 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_sensor` | 1,181,489 | 1,181,489 | +0 | 60,600 | 60,600 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Heltec_v3_kiss_modem` | 581,185 | 581,185 | +0 | 24,580 | 24,580 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Generic_E22_sx1268_repeater` | 1,143,361 | 1,143,361 | +0 | 60,620 | 60,620 | +0 | differs (65 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `Meshadventurer_sx1268_repeater` | 1,161,797 | 1,161,797 | +0 | 61,084 | 61,084 | +0 | differs (64 bytes) | time @ 0x50000204, magic @ 0x50000200 | magic @ 0x50000200, time @ 0x50000204 |
| `RAK_3112_repeater_bridge_espnow` | 1,176,269 | 1,176,269 | +0 | 64,072 | 64,072 | +0 | differs (65 bytes) | time @ 0x50000004, magic @ 0x50000000 | magic @ 0x50000000, time @ 0x50000004 |
| `Xiao_C6_companion_radio_ble_` | 1,446,683 | 1,446,683 | +0 | 164,292 | 164,292 | +0 | differs (64 bytes) | time @ 0x5000004c, magic @ 0x50000048 | magic @ 0x50000048, time @ 0x5000004c |
| `RAK_4631_repeater` | 441,964 | 441,964 | +0 | 37,240 | 37,240 | +0 | same | none | none |
| `RAK_4631_companion_radio_ble` | 465,224 | 465,224 | +0 | 149,768 | 149,768 | +0 | same | none | none |
| `PicoW_repeater` | 492,668 | 492,668 | +0 | 85,368 | 85,368 | +0 | same | none | none |
| `wio-e5-mini_repeater` | 204,136 | 204,136 | +0 | 17,172 | 17,172 | +0 | same | none | none |
| `ProMicro_repeater` | 346,276 | 346,276 | +0 | 33,900 | 33,900 | +0 | same | none | none |
| `Meshimi_repeater_` | 542,535 | 542,535 | +0 | 29,856 | 29,856 | +0 | differs (64 bytes) | time @ 0x5000004c, magic @ 0x50000048 | magic @ 0x50000048, time @ 0x5000004c |
| `M5Stack_Unit_C6L_companion_radio_ble` | 1,454,227 | 1,454,227 | +0 | 164,180 | 164,180 | +0 | differs (65 bytes) | time @ 0x5000004c, magic @ 0x50000048 | magic @ 0x50000048, time @ 0x5000004c |

</details>
