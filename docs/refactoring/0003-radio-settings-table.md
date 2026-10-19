# 0003: Drive the radio settings from one table

Status: ready for review. Behaviour change: none. Builds on 0001 and 0002.

## Context

After 0001 and 0002, all radio settings live in `RadioPrefsFields`, and
`CommonRadioPrefs` is the one place that persists them and handles their CLI. In
that place each setting was still spelled out up to three times: a `def()` line in
`structure()` for `/prefs.json`, a `get` branch and a `set` branch in
`handleCommand()` with their own parsing, range check and reply (27 branches for
14 settings), plus a branch in the key/value store for the two FEM gains.

## Decision

`src/helpers/CommonRadioPrefs.cpp` gets a table with one row per radio setting:

| Column | Meaning |
|--------|---------|
| JSON key | key in the `radio` object of `/prefs.json`; the row order is the key order |
| field, type | the field in `RadioPrefsFields` (`offsetof`), float, uint8_t or int8_t |
| CLI name, CLI kind | `get <name>` / `set <name> <value>`, and how the value is parsed |
| max, range error | upper limit and the reply for an out of range value |
| key/value | whether boards may read and write it through the key/value store |

CLI kinds, each reproducing the parsing of the branches they replace:

- get only: `> value`
- float: `atof()`, accepted if `0 <= value <= max`; NaN and infinity are rejected
  by looking at the bits (see below)
- uint8_t: `atoi()` into `uint8_t`, accepted if `value <= max` (no limit if none)
- on/off: on if the value starts with `on`

The table drives `structure()`, the key/value store and the CLI of the eight
settings that fit: `freq` (get only), `cad`, `int.thresh`, `rxdelay`, `txdelay`,
`direct.txdelay`, `path.hash.mode`, `multi.acks`. Six settings keep their
hand-written branches, moved verbatim, because their CLI does not fit a kind:
`radio` (four values at once), `af` (a stricter parser that rejects garbage),
`dutycycle` (derived from `af`), `radio.rxgain` and `tx` (pushed to the radio
driver; `get tx` also matches with trailing text), `agc.reset.interval` (unit
conversion and its own reply).

A new setting of a common kind is now a field and one row.

## Alternatives considered

- **Make the six special settings fit generic kinds too.** Rejected for this step:
  each would need its own kind or a hook, and some of their quirks (NaN passes
  `set af`, `get tx foo` reads the TX power, `dutycycle 1` sets an airtime factor
  of 99 that `set af` would refuse) are visible to users. Changing them is a
  behaviour change.
- **A table in the header with pointer-to-member fields.** Rejected: members of
  different types do not fit one array without a type tag anyway, and `offsetof`
  on the standard-layout `RadioPrefsFields` keeps the table plain data.
- **Generate the CLI documentation from the table.** Left for later.

## Compatibility

- `/prefs.json`, all CLI replies, the dirty marking and the key/value store are
  unchanged.
- The public interface of `CommonRadioPrefs` is unchanged; its getters and setters
  stay for source compatibility. `structure()` is now defined in the `.cpp`.

## Verification

### Host tests

Before the change, a new test (`RadioCliEdgeCases`, run for both `NodePrefs`
classes) pinned about 70 edge cases of the radio CLI on the old code: boundaries,
out of range values, values that do not fit the field (`int.thresh 300` is stored
as 44, `tx -5` reads back as 251), garbage (read as 0 by `atof`/`atoi`), NaN
(accepted by `set af`, rejected by `set rxdelay`), prefix look-alikes and commands
other handlers own (`set freq`). Together with the characterization tests of 0001
(exact `/prefs.json`, every command's reply and side effect) they pass unchanged
after the change.

A new native environment, `native_ofast`, runs the prefs tests again with `-Ofast`,
the optimisation the nRF52 firmware is built with (see "NaN and -Ofast" below).
The unit test workflow runs it.

Mutation tests (number of failing tests; `native` and `native_ofast` on the
author's machine use clang, the CI uses GCC):

| Mutation | `native` | `native_ofast` (clang) |
|----------|---------:|-----------------------:|
| swap the `sf` and `cr` rows (JSON order) | 2 | 2 |
| float check as `v < 0 \|\| v > max` (lets NaN through) | 2 | 0 |
| uint8_t limit off by one | 2 | 2 |
| on/off compares three characters | 2 | 2 |
| `tx` exposed in the key/value store | 2 | 2 |
| `set <name>` without checking the space after the name | 2 | 2 |
| bit check removed, plain `!(v >= 0 && v <= max)` | 0 | 0 |

The last row is the defect the hardware run found (below). Clang keeps rejecting
NaN with that comparison even under `-Ofast`; GCC does not. Pushed to a temporary
branch, the same mutation made the CI's `native_ofast` (GCC) fail with 2 failing
tests, while the unmutated branch passed.

### NaN and -Ofast

nRF52 firmware is compiled with `-Ofast` (set by PlatformIO's Adafruit nRF52
builder), which includes `-ffast-math`: the compiler may assume that floats are
never NaN or infinite. The old hand-written branch `if (db >= 0 && db <= 20.0f)`
happened to reject NaN on the boards; the first version of the table, with an
equivalent comparison written differently, accepted `set rxdelay nan` on a T-Echo.
The table now recognises NaN and infinity by their bits, which rejects them with
any compiler and flags, as the old branches did on the boards and in IEEE builds.
`set af`, whose branch did not change, accepts `nan` on the boards before and after
this step (measured); how it behaves elsewhere depends on the compiler.

### Builds

The same 18 environments were built from this step and from 0002. All build. Every
firmware is 8 to 96 bytes smaller than with 0002; RAM is unchanged.

| Environment | Platform | Flash vs 0002 | RAM vs 0002 | Flash vs `dev` | RAM vs `dev` |
|---|---|---:|---:|---:|---:|
| `Heltec_v3_repeater` | ESP32-S3 | -84 | +0 | -1148 | +0 |
| `Heltec_v3_room_server` | ESP32-S3 | -68 | +0 | -1080 | +0 |
| `Heltec_v3_companion_radio_ble` | ESP32-S3 | -8 | +0 | -1048 | +8 |
| `Heltec_v3_companion_radio_wifi` | ESP32-S3 | -68 | +0 | -1116 | +8 |
| `Heltec_v3_sensor` | ESP32-S3 | -56 | +0 | -856 | +8 |
| `RAK_4631_repeater` | nRF52840 | -96 | +0 | -432 | +0 |
| `RAK_4631_room_server` | nRF52840 | -80 | +0 | -384 | +0 |
| `RAK_4631_companion_radio_ble` | nRF52840 | -64 | +0 | -352 | +8 |
| `RAK_4631_sensor` | nRF52840 | -64 | +0 | -128 | +8 |
| `PicoW_repeater` | RP2040 | -40 | +0 | -728 | +0 |
| `wio-e5-mini_repeater` | STM32WL | -24 | +0 | -608 | +0 |
| `wio-e5-mini_sensor` | STM32WL | -32 | +0 | -480 | +8 |
| `RAK_3112_repeater_bridge_espnow` | ESP32-S3 | -60 | +0 | -1108 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | RP2040 | -40 | +0 | -720 | +0 |
| `ProMicro_repeater` | nRF52840 | -64 | +0 | -416 | +0 |
| `ProMicro_room_server` | nRF52840 | -96 | +0 | -384 | +0 |
| `ProMicro_companion_radio_usb` | nRF52840 | -48 | +0 | -336 | +8 |
| `ProMicro_sensor` | nRF52840 | -64 | +0 | -128 | +8 |

### Hardware

Two LilyGo T-Echo boards (nRF52840, SX1262), room server and USB companion, each
running the 0002 image and the image of this step in turn (marked builds, see
0001). A script of 58 radio commands (edge cases that are safe on air: no
frequency change, no extreme TX power) ran on both images from the same starting
values, through the serial console (room server) and `CMD_RUN_CLI_COMMAND` frames
(companion).

- All 58 replies were identical between 0002 and this step on both boards,
  including garbage, out of range values, NaN and infinity, and look-alikes.
- `set af nan` behaved the same on both images (accepted).
- Each image read the settings the other had written (upgrade and downgrade).

The first version of this step failed this run: it answered `OK` to
`set rxdelay nan` and `set txdelay nan` where 0002 answered with an error. That
led to the bit check and to `native_ofast` described above.

On the room server, the first read right after flashing the 0002 image twice
returned a reply for a garbled command (`??: txdelay`); repeated reads were
identical. It was not observed with the image of this step in these runs.

## Follow-ups

- Generate the radio part of `docs/cli_commands.md` from the table.
- Decide whether the quirks listed above should be fixed (behaviour change, own
  pull request).
