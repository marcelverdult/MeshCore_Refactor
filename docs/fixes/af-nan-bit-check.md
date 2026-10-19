# "set af nan" was accepted

## Context

`set af` parses its argument and then checks the range:

```cpp
float af = strtof(&command[7], &end);
if (end == &command[7] || af < 0 || af > 9) {
  strcpy(reply, "ERROR: af must be 0-9");
} else {
  setAirtimeFactor(af);
  strcpy(reply, "OK");
}
```

(`src/helpers/CommonRadioPrefs.cpp:69-79` on `upstream/dev` 428a95c6.)

`strtof("nan")` consumes the token, so the parse guard does not fire, and under
IEEE 754 every ordered comparison with a NaN is false, so neither `af < 0` nor
`af > 9` fires either. The else branch stores the NaN and answers `OK`.

`airtime_factor` is the duty cycle budget. The roles hand it to the airtime
accounting, for the repeater through `examples/simple_repeater/MyMesh.h:157`
with matching getters in the room server and the sensor, so a NaN there is not
cosmetic. `set dutycycle` writes the same field through `100/dc - 1` and had the
same hole.

Step 3 of the refactoring series already knew about this and pinned it as
current behaviour rather than changing it: `test/mocks/RadioPrefsChecks.h:238`
on `integration` carries `EXPECT_EQ("OK", accepted("set af nan"))` behind
`#ifndef __FAST_MATH__`, with a comment calling it odd.

### Why only these two commands

The other float commands in the same file phrase their guard the other way
round, `if (value >= low && value <= high) { accept } else { error }`, and a NaN
makes that condition false, so it lands in the error branch. Measured on the
host with both shapes:

| token | guard as in `set af` | guard as in `set rxdelay` |
|---|---|---|
| `nan` | **accepted** | error |
| `-nan` | **accepted** | error |
| `inf` | error | error |
| `1e40` | error | error |
| `0.5` | accepted | accepted |

So on `upstream/dev` the two commands that are reachable with a NaN are exactly
`set af` and `set dutycycle`, the two written as "reject if out of range". That
is also why the board measurement found `af` and nothing else.

The positively phrased guards are safe by accident of phrasing, not by design:
their result still depends on the compiler once `-ffast-math` is in play, which
is why step 3 put a bit check behind all of them on `integration`
(`inRange`, `src/helpers/CommonRadioPrefs.cpp:65`).

## Decision

Keep the parse guard, keep both comparisons, keep both reply strings, and add a
check that the value is a finite number at all. The check reads the bits:

```cpp
bool isFinite(float v) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  return (bits & 0x7F800000u) != 0x7F800000u;
}
```

That is integer arithmetic, so no optimisation can fold it away, and it answers
the same on every platform with any flags. The same guard goes in front of
`set dutycycle`, whose range stays 1..100.

### Why not a comparison or a library call

nRF52 firmware is built with `-Ofast`, which includes `-ffast-math`, and that
lets the compiler assume NaN and infinity never occur. The outcome then depends
on the compiler, and the two disagree. Measured with the same test file:

| | `set af nan` on `upstream/dev` |
|---|---|
| host, IEEE comparisons | accepted |
| host, `-Ofast` (Apple clang 21) | rejected |
| board, nRF52 with `arm-none-eabi-gcc -Ofast` | accepted |

A host run with `-Ofast` alone would therefore have reported this defect as
absent. `std::isnan` is no better: it is the same comparison in a different
spelling and is equally free to be folded away.

## Alternatives considered

- **Compile this file without fast math.** Rejected. It would hide the problem
  behind a build flag that any variant can change, and the defect exists under
  plain IEEE rules as well, which is where it was measured.
- **Take the table driven `inRange` from step 3.** Not available here: this
  branch starts from `upstream/dev`, where the radio CLI is still written out by
  hand. The two will meet when this branch is merged, and the shape of the check
  is deliberately the same so they can be unified.
- **Refuse the value one layer down, in the parser.** That is the right place
  for a value read from `/prefs.json` and is what the range check fix does. It
  is the wrong place here: this value comes from a CLI command and the caller
  has to be told that its command was refused.
- **Also rewrite the positively phrased guards.** Not done, see above: they
  reject a NaN under IEEE, and on `integration` step 3 has already put the bit
  check behind them. Changing them here would widen a fix whose defect is the
  two commands measured above.

## Compatibility

- Every value that was accepted before is still accepted, including the 99 that
  `set dutycycle 1` stores, and every rejected value is still rejected with the
  same message. Only values that are not finite numbers change from accepted to
  refused.
- Server roles and the companion share this handler
  (`src/helpers/CommonCLI.cpp:184` and
  `examples/companion_radio/NodePrefs.h`), so one change covers every role.

## Verification

### Host tests

New `test/test_radio_prefs_af` drives `CommonRadioPrefs::handleCommand` through
a test double. Nine cases: every spelling of not a number, infinity and
overflow, the garbage and out of range cases that must stay rejected with their
old message, the range that must stay accepted, `get af`, and the duty cycle
range including the 99 that step 3 pinned.

The suite runs twice, in `native` with IEEE comparisons and in the new
`native_ofast` with the flags the nRF52 firmware uses:

| | before the change | after |
|---|---|---|
| `native` (IEEE) | 2 of 9 red | 9 of 9 green |
| `native_ofast` | 9 of 9 green | 9 of 9 green |

The second row is the point of the environment, not a contradiction: on this
host `-Ofast` already rejects the NaN, so an `-Ofast` run alone would have
shown nothing. Both rows green afterwards is what says the result no longer
depends on the flags.

All suites of this branch: `pio test -e native -e native_kiss_modem -e
native_ofast` reports 70 of 70.

### Mutations

Five deliberate errors, each had to turn the suite red. Two of them are only
caught by the IEEE run, which is worth recording:

| Mutation | `native` (IEEE) | `native_ofast` |
|---|---|---|
| `set af` without the finite check | red | green |
| `set dutycycle` without the finite check | red | green |
| bit comparison inverted | red (4) | red (2) |
| wrong mask | red | red |
| mask compared against zero | red (3) | red |

### Builds

All 20 environments of the matrix build on this commit, and so does
`upstream/dev` 428a95c6 in its own worktree with the same toolchain.
`Heltec_v3_terminal_chat` was built as well, because it is the one role with its
own prefs structure, to show it is untouched.

| Environment | Build | RAM against dev | Flash against dev |
|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | OK | +0 | +32 |
| `Heltec_v3_companion_radio_wifi` | OK | +0 | +32 |
| `Heltec_v3_repeater` | OK | +0 | +32 |
| `Heltec_v3_room_server` | OK | +0 | +28 |
| `Heltec_v3_sensor` | OK | +0 | +28 |
| `LilyGo_T-Echo_kiss_modem` | OK | +0 | +0 |
| `LilyGo_T-Echo_room_server` | OK | +0 | +32 |
| `PicoW_repeater` | OK | +0 | +24 |
| `ProMicro_companion_radio_usb` | OK | +0 | +32 |
| `ProMicro_repeater` | OK | +0 | +32 |
| `ProMicro_room_server` | OK | +0 | +32 |
| `ProMicro_sensor` | OK | +0 | +32 |
| `RAK_3112_repeater_bridge_espnow` | OK | +0 | +32 |
| `RAK_4631_companion_radio_ble` | OK | +0 | +32 |
| `RAK_4631_repeater` | OK | +0 | +32 |
| `RAK_4631_room_server` | OK | +0 | +32 |
| `RAK_4631_sensor` | OK | +0 | +32 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | OK | +0 | +24 |
| `wio-e5-mini_repeater` | OK | +0 | +24 |
| `wio-e5-mini_sensor` | OK | +0 | +24 |
| `Heltec_v3_terminal_chat` | OK | +0 | +0 |

RAM is unchanged everywhere, and flash grows by 24 to 32 bytes in every build
that compiles this handler. Exactly two builds come out byte identical to dev,
and both are explained rather than suspicious: `LilyGo_T-Echo_kiss_modem` adds
only `examples/kiss_modem/` to its sources, and `Heltec_v3_terminal_chat` runs
`examples/simple_secure_chat`, which keeps its own prefs structure and never
reaches `CommonRadioPrefs`.

### Hardware

techo-b43b, `LilyGo_T-Echo_room_server`, both images marked and each marker
confirmed in the image before flashing
(`unzip -p firmware.zip '*.bin' | grep -c -a <tag>` = 1) and reported by `ver`
in the same run that produced the readings.

| command | `dev-428a95c6` | `af-nan-wt` |
|---|---|---|
| `set af nan` | `OK` | `ERROR: af must be 0-9` |
| `get af` afterwards | `> 0` | `> 1.0`, unchanged |
| `set af -nan` | not measured | `ERROR: af must be 0-9` |
| `set af inf` | not measured | `ERROR: af must be 0-9` |
| `set af abc` | not measured | `ERROR: af must be 0-9` |
| `set dutycycle nan` | `OK - 0.0%` | `ERROR: dutycycle must be 1-100` |
| `get af` afterwards | `> 0` | `> 2.0`, unchanged |
| `set af 2` | not measured | `OK`, `get af` `> 2.0` |
| `set dutycycle 1` | not measured | `OK - 1.0%`, `get af` `> 99.0` |

Worth recording: on `dev` the poisoned field reads back as `> 0`, so nothing on
the CLI showed that it held a NaN. The board was left on the values it came
with, `af 1.0` and `dutycycle 50.0%`.

## Follow-ups

- **The merge into `integration` needs a hand.** Two things there are not a
  textual conflict and must not be resolved by taking one side:
  `test/mocks/RadioPrefsChecks.h:238-242` expects `set af nan` to be accepted
  and has to be turned into a rejection, and `integration` already has an
  environment called `native_ofast` with a different `test_filter`, which has to
  become the union of both rather than either one.
- `isFinite` now exists twice with the same shape, here and in
  `ConfigSerializer.cpp` from the range check fix. Worth unifying once both are
  on `integration`.
- `set radio` parses `freq` and `bw` with `strtof` and discards the end pointer
  entirely, so `set radio abc def 8 5` is read as zeros and rejected by the
  range check rather than by the parse. Harmless today, and a separate change.
