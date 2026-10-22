# GPS update deadline is not wrap-safe in seven variants

Status: ready for review. Behaviour change: only at the millis() wrap, see below.

## Context

Seven variants schedule their GPS cache refresh the same way:

```c++
static long next_gps_update = 0;
...
if (millis() > next_gps_update) {
  ...
  next_gps_update = millis() + 1000;
}
```

`variants/heltec_mesh_solar`, `heltec_tracker`, `meshadventurer`, `meshtracker_x1`,
`nano_g2_ultra`, `t1000-e` and `thinknode_m1`, each in `target.cpp`.

The shared `EnvironmentSensorManager` had the same shape and was changed in 63731d3f
to an unsigned deadline with `(long)(millis() - deadline) > 0`. These seven copies
were not part of that change.

### What the old code actually does

63731d3f describes the defect as a sign flip after about 24.8 days, after which the
block "never runs again" and the GPS time sync stalls "permanently until reboot".
Measured, that is not what happens on these targets.

`millis()` returns an unsigned 32-bit value on every platform here (`unsigned long`
on ESP32, `uint32_t` on nRF52). In `millis() > next_gps_update` the signed operand is
converted to `unsigned long`, so the comparison is performed unsigned, and storing
`millis() + 1000` into a `long` and reading it back is bit-preserving. Checked with
every installed toolchain that can build these targets (arm-none-eabi gcc 7.2.1,
12.3.1 and 14.2.1, xtensa-esp32 and xtensa-esp32s3 gcc 8.4.0), each with a
positive and a negative control:

```
sizeof(long) == 4 && sizeof(unsigned long) == 4                  holds
((unsigned long)1 > (long)-1) == false                           holds, so unsigned
(unsigned long)(long)(unsigned long)0x800003E8 == 0x800003E8     holds
```

So there is no sign flip to trip over at 2^31. The counter wrap at 2^32 is a real
problem, but a different one. Driving the loop pass by pass over 3000 s of loop time,
1 ms per pass, with the target's widths:

| Window | Shape | Runs | Longest gap | Longest run of consecutive passes |
|---|---|---:|---:|---:|
| across 2^31, 24.8 days | shipped, 32-bit `long` | 2998 | 1001 ms | 1 |
| across 2^31, 24.8 days | 64-bit `long` | 2998 | 1001 ms | 1 |
| across 2^31, 24.8 days | wrap-safe | 2998 | 1001 ms | 1 |
| across 2^32, 49.7 days | shipped, 32-bit `long` | 3497 | 1001 ms | 501 |
| across 2^32, 49.7 days | 64-bit `long` | 1499 | 1500500 ms | 1 |
| across 2^32, 49.7 days | wrap-safe | 2998 | 1001 ms | 1 |

In the last interval before the counter reaches 2^32, `millis() + 1000` wraps to a
small number while `millis()` is still large, so the comparison is true on every pass.
The block then runs on each loop pass instead of once per second for up to 1000 ms,
and resumes its normal period once the counter wraps as well. The longest gap does not
grow, so nothing stalls.

The permanent stall 63731d3f describes does occur, but only where `long` is 64 bits
wide, which is the test host and none of the firmware targets. The fix in 63731d3f is
right; its stated reason is not.

What the burst costs is small. The guarded block only copies the cached latitude,
longitude and altitude out of the location provider and, in debug builds, prints them;
it is idempotent. This is read from the source of all seven blocks, not measured at
runtime: reaching the wrap takes 49.7 days of uptime.

## Decision

A new header, `src/helpers/MillisDeadline.h`, holds the question as one inline
function:

```c++
inline bool millisHasPassed(uint32_t now, uint32_t deadline) {
  const uint32_t elapsed = now - deadline;
  return elapsed != 0 && elapsed < 0x80000000u;
}
```

All seven variants call it. Three properties are deliberate:

- **Fixed widths, not `long`.** `long` is 32 bits on the firmware targets and 64 bits
  on the test host. A host test written with `long` would exercise the 64-bit shape,
  which does not merely keep the behaviour at the wrap but turns the burst into a
  stall, as the table above shows. With `uint32_t` the host performs the arithmetic
  the target performs, so the test means something.
- **Strict comparison.** The shipped loops fired strictly after the deadline, so the
  period is unchanged away from the wrap (2998 runs, 1001 ms, in the table above).
- **No out-of-range conversion.** `Dispatcher::millisHasNowPassed` casts a difference
  above `LONG_MAX` to `long`, which the standard leaves to the implementation before
  C++20. The two spellings agree on every input, which `test_millis_deadline` pins, and
  they compile to the same instructions, so the well-defined spelling costs nothing.

`thinknode_m1` has a second timer in the same function, `last_switch_check`. It uses
the elapsed form, `millis() - last_switch_check > 1000`, which is already wrap-safe.
Its type changes from `long` to `uint32_t` so that no millis() reading is stored in a
signed variable, and that is all: driven over both boundaries, every single comparison
is identical before and after (2997 runs, longest gap 1002 ms, longest run 1, in both
windows). It is a type cleanup, not a behaviour fix.

## Alternatives considered

- **Write the idiom inline at each of the seven places, as 63731d3f did.** Rejected.
  Seven hand-written copies are what produced this defect, and `variants/*/target.cpp`
  does not build on the host, so an inline form cannot be covered by a test. A shared
  function can, and the embedded builds then show the seven call sites compile.
- **Reuse `Dispatcher::millisHasNowPassed`.** Rejected: it is a member function and
  reads the clock through `_ms`, and a `SensorManager` in a variant has no
  `Dispatcher`.
- **Put the helper on `SensorManager`.** Rejected: all seven callers happen to be
  `SensorManager` subclasses, but a deadline comparison is not a sensor concern.
- **Also change `Dispatcher::millisHasNowPassed` to call the new helper.** Left out of
  this change: it is separable, it touches the core that every role uses, and the two
  forms are pinned to each other by a test either way.
- **Fix the remaining naive deadline comparisons too.** Left out. 28 further
  `millis() >` and `millis() >=` comparisons exist in the UI tasks, the simple
  examples and `SerialBLEInterface`. They use `unsigned long` deadlines, so they are a
  different shape from this one, and each would need its own reading before being
  called a defect. Not classified here.

## Compatibility

- No stored format, no CLI reply and no protocol field is touched.
- The period and the behaviour of all seven loops are unchanged except in the last
  interval before the millis() counter wraps.
- The new header is self-contained and includes only `stdint.h`.

## Verification

### Host tests

`test/test_millis_deadline/` runs in the `native` environment and covers:

| Test | What it pins |
|---|---|
| `FiresOnlyStrictlyAfterTheDeadline` | the strict comparison the shipped loops used |
| `MatchesTheSignedDifferenceIdiom` | agreement with `(int32_t)(now - deadline) > 0` over 70 pairs, including differences of 0, 1, 2^31 - 1, 2^31 and 2^32 - 1 |
| `TreatsADeadlineThatWrappedAsStillAhead` | a deadline that wrapped to 500 while `millis()` is 0xFFFFFF00 reads as 756 ms in the future, where the old form read it as long past |
| `PeriodIsUnchangedByTheSignFlipAndTheWrap` | runs, longest gap and longest consecutive run are identical in a quiet window, across 2^31 and across 2^32 |
| `LooksAheadHalfTheCounterAtMost` | the documented horizon of 2^31 - 1 |

Written against the shipped shape first: with the naive body in the header, four of
the five tests fail. With the wrap-safe body all five pass.

Mutation tests, each applied to the helper alone, with the header restored and
re-checked by its sha256 afterwards:

| Mutation | Failing tests |
|---|---:|
| the naive comparison, as shipped | 4 |
| zero guard dropped, fires on the deadline itself | 3 |
| half-plane inclusive, `<=` instead of `<` | 2 |
| half-plane inverted, `>` instead of `<` | 5 |
| arithmetic in `unsigned long` instead of `uint32_t` | 4 |
| horizon off by one, `0x7FFFFFFF` instead of `0x80000000` | 2 |

The fifth row is the reason for the fixed widths: that mutation is invisible on a
32-bit target and wrong on a 64-bit host.

All native suites pass: `pio test -e native -e native_kiss_modem`, 59 of 59.

### Warnings and generated code

The firmware builds with `-w`, so a warning in this header would never surface there.
Compiled on its own with `-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
-Wold-style-cast -Wpedantic`, driven both with an `unsigned long` and with a `uint32_t`
reading to cover the implicit conversion at the call sites:

| Toolchain | Warnings |
|---|---:|
| arm-none-eabi gcc 7.2.1 | 0 |
| arm-none-eabi gcc 12.3.1 | 0 |
| arm-none-eabi gcc 12.3.1, `-Ofast` | 0 |
| arm-none-eabi gcc 14.2.1 | 0 |
| xtensa-esp32 gcc 8.4.0 | 0 |
| xtensa-esp32s3 gcc 8.4.0 | 0 |

`-Ofast` is included because the nRF52 firmware is built with it and it has caught a
difference between host and target before.

The two spellings also generate identical code:

| Target | `millisHasPassed` | `(int32_t)(now - deadline) > 0` |
|---|---|---|
| nRF52, `-Ofast`, gcc 12.3.1 | `sub r0,r0,r1; cmp r0,#0; movle r0,#0; movgt r0,#1; bx lr` | identical |
| ESP32-S3, `-Os`, gcc 8.4.0 | `entry sp,32; sub a2,a2,a3; srai a8,a2,31; sub a2,a8,a2; extui a2,a2,31,1; retw.n` | identical |

### Builds

All 45 environments of the seven variants were built from this change. 42 build. The
three that do not, fail the same way on unchanged `dev` 428a95c6, each checked in a
separate worktree that `git status` reports as clean at that commit:

| Environment | Error | Cause | On unchanged `dev` |
|---|---|---|---|
| `Heltec_mesh_solar_companion_radio_usb` | `'USB' is not a member of 'InterfaceType'` | the `meshsolar` dependency defines `NONE` as a macro in `src/utils/logger.h:18`, which replaces the `NONE` enumerator of `InterfaceType` in `src/helpers/MultiSerialInterface.h:11` and makes the enum body invalid | fails, 57 s |
| `Heltec_mesh_solar_companion_radio_ble` | `'Bluetooth' is not a member of 'InterfaceType'` | the same macro collision | fails, 89 s |
| `MeshTracker_X1_kiss_modem` | `MicroNMEA.h: No such file or directory` | the `kiss_modem` environment of this variant does not inherit `stevemarple/MicroNMEA`, while the four other environments of the variant list it and `target.cpp` includes the provider unconditionally | fails, 85 s |

Neither cause is in the files this change touches, and neither is related to it. Both
are handled separately.

The 18 environments of the regression matrix were built from this change and from
`dev` in two worktrees with the same toolchain. All 36 builds succeed, and flash and
RAM are identical in every one of the 18:

| Environment | Platform | Flash | Flash vs `dev` | RAM | RAM vs `dev` |
|---|---|---:|---:|---:|---:|
| `Heltec_v3_repeater` | ESP32-S3 | 1174449 | +0 | 62512 | +0 |
| `Heltec_v3_room_server` | ESP32-S3 | 1169705 | +0 | 66200 | +0 |
| `Heltec_v3_companion_radio_ble` | ESP32-S3 | 1666437 | +0 | 194836 | +0 |
| `Heltec_v3_companion_radio_wifi` | ESP32-S3 | 1102677 | +0 | 172376 | +0 |
| `Heltec_v3_sensor` | ESP32-S3 | 1180593 | +0 | 60560 | +0 |
| `RAK_4631_repeater` | nRF52840 | 440952 | +0 | 37240 | +0 |
| `RAK_4631_room_server` | nRF52840 | 434088 | +0 | 40928 | +0 |
| `RAK_4631_companion_radio_ble` | nRF52840 | 462548 | +0 | 149760 | +0 |
| `RAK_4631_sensor` | nRF52840 | 437544 | +0 | 35288 | +0 |
| `PicoW_repeater` | RP2040 | 491972 | +0 | 85368 | +0 |
| `wio-e5-mini_repeater` | STM32WL | 203552 | +0 | 17172 | +0 |
| `wio-e5-mini_sensor` | STM32WL | 202904 | +0 | 15236 | +0 |
| `RAK_3112_repeater_bridge_espnow` | ESP32-S3 | 1175717 | +0 | 64080 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | RP2040 | 240368 | +0 | 26704 | +0 |
| `ProMicro_repeater` | nRF52840 | 345424 | +0 | 33892 | +0 |
| `ProMicro_room_server` | nRF52840 | 338456 | +0 | 37588 | +0 |
| `ProMicro_companion_radio_usb` | nRF52840 | 357300 | +0 | 99460 | +0 |
| `ProMicro_sensor` | nRF52840 | 341900 | +0 | 31948 | +0 |

That they are identical is expected: none of these targets includes the new header.
A difference would have been a finding.

### In the built firmware

`t1000e_repeater` was built from this change and from unchanged `dev`, and
`T1000SensorManager::loop` disassembled in both. The deadline test changes from a
register compare with an unsigned branch to the difference form:

```
dev   cmp  r0, r3        fix   subs r0, r0, r3
      bls  ...                 cmp  r0, #0
```

Counted over the whole function, register-to-register compares followed by an
unsigned branch drop from 2 to 1, and the instruction count rises by 1 (279 to 280).
The compare that remains comes from other inlined code, not from a deadline. The
`subs`, `cmp #0`, signed-branch sequence that appears unchanged earlier in the same
function is the already corrected test in `MicroNMEALocationProvider::loop`, inlined
through `_nmea->loop()`. Section sizes are unchanged (text 292956, data 932, bss
31944 in both), so the extra instruction fits in existing alignment padding and costs
no flash here.

### Hardware

Not done. The counter wrap is 49.7 days of uptime away, so it cannot be reached on a
board at all; what a board run could show is that normal GPS operation is unaffected.
The T1000-E available for this work was not connected while the change was made, so
there is no runtime evidence either way. The host tests and the 42 builds are the
whole of the verification.

## Follow-ups

- Decide whether `Dispatcher::millisHasNowPassed` should call `millisHasPassed`, so
  the idiom exists once.
- Classify the 28 remaining naive deadline comparisons listed above.
- None of the 15 environments the pull request check builds covers any of these seven
  variants, so a compile error in them would not be caught by it. Consider adding one
  variant with a GPS loop to that matrix.
