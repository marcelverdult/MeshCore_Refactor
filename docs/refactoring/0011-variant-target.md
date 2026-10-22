# 0011: One generic variant target built from a board description

Status: cut A (one shared `radio_new_identity()`) and cut B (one shared FEM RX gain
handler) implemented, each its own pull request, B stacked on A. Cut C (one default
`radio_init()` for 31 nRF52 variants) implemented, its own pull request, on top of
A and B. Cut D (the radio, wrapper and RTC globals) is designed as two pull requests;
D1 (31 nRF52 variants) is implemented, D2 (22 ESP32 variants) follows on top of it,
see "Cut D". Cut E is planned. Behaviour change: none.

## Context

Every board is a directory in `variants/` with a `target.h`, which declares the
globals the roles use (`board`, `radio_driver`, `rtc_clock`, `sensors`, and
`display` and `user_btn` where there is a screen), and a `target.cpp`, which
defines them and the functions `radio_init()` and `radio_new_identity()`. Each new
board starts as a copy of the closest existing one, so a change to what a variant
provides has to be made in every copy. The review lists the cost of that in
release 1.18: the remote RX gain toggle touched 162 variant files, moving it into
the radio wrappers 23 days later touched 156 again, and one line added to every
`radio_init()` touched 64 (see [architecture-review-v1.18.md](architecture-review-v1.18.md), F3).

### Measured on the commit this step started from

Measured with `tools/refactoring/variant_duplication.py` (in this step), which
extracts every function a `target.cpp` defines and compares the bodies twice: with
only runs of spaces collapsed ("strict"), and with comments and all white space
removed ("normalized"). Run it from the repository root to repeat the numbers.

- 92 variant directories, 89 with a `target.cpp`, 4,690 lines in all of them.
  The three without one (`m5stack_unit_c6l`, `xiao_c6`, `wio-tracker-l1-eink`)
  are covered under "Observations"; the two C6 ones carry two more copies of the
  same body in their board file.
- 618 firmware environments (631 with the host test environments).

| function | files | strict bodies | normalized bodies | largest identical group |
|---|---:|---:|---:|---:|
| `radio_new_identity()` | 89 | 4 | 2 | 87 |
| `radio_init()` | 89 | 39 | 29 | 28 |
| `radio_set_tx_power()` | 3 | 1 | 1 | 3 |
| `radio_get_rng_seed()` | 2 | 1 | 1 | 2 |
| `radio_set_params()` | 2 | 1 | 1 | 2 |

`radio_new_identity()` in detail:

| copies | body |
|---:|---|
| 76 | `RadioNoiseListener rng(radio); return mesh::LocalIdentity(&rng);` with the comment `// create new random identity` |
| 10 | the same without the comment |
| 1 | the same with the opening brace on its own line |
| 2 | `ESP_RNG rng; return mesh::LocalIdentity(&rng);` (`generic_espnow`, `sensecap_indicator-espnow`, which have no LoRa radio and use the ESP32 hardware RNG) |

The review counted 74 identical copies "ignoring repeated spaces"; the strict count
here is 76, and 87 once the comment and line breaks are ignored. Either way the 87
compile to the same thing.

The FEM gain handlers of the board classes, compared with the board class name
replaced by a placeholder:

| function | HeltecV4, HeltecV4R8, T096, HeltecTrackerV2 | TBeam1W | StationG3 | WioTrackerL1 |
|---|---|---|---|---|
| `attachDynamicPrefs()` | one body (217 characters normalized) | own (fan prefs) | own | own |
| `handleCommand()` | one body (999 characters normalized) | own | own | own |

What else repeats across `target.cpp`, by normalized line (each counted once per
file): `WRAPPER_CLASS radio_driver(radio, board);` in 87 files,
`AutoDiscoverRTCClock rtc_clock(fallback_clock);` in 79, `rtc_clock.begin(Wire);` in
76, `#ifdef DISPLAY_CLASS` in 72, `DISPLAY_CLASS display;` in 57,
`MomentaryButton user_btn(PIN_USER_BTN, 1000, true);` in 47, the GPS sensor manager
pair in 42 or 43. Replacing the board class name, 73 of the 89 files are still
different from each other; the largest group of whole-file copies is 4
(`lilygo_t_impulse_plus`, `lilygo_techo`, `lilygo_techo_lite`, `thinknode_m6`). So
the copies are of building blocks, not of whole files, and the blocks are what a
generic target can share.

## Goal and cut plan

The goal from the [README](README.md): one generic variant target built from a board
description, so a new board says what it has (radio chip and pins, RTC, sensors,
display, button) and writes code only for what is really its own.

Each cut is one concern, one pull request, and keeps every existing and downstream
variant compiling. The shared code goes into `src/helpers/radiolib/RadioLibTarget.cpp`
(later cuts may add a sibling for non-RadioLib parts) and uses weak definitions, so
a variant that keeps its own definition still links and its own wins (see "Why
weak" below).

| cut | what | files touched | status |
|---|---|---|---|
| A | one `radio_new_identity()` for all RadioLib variants | 87 `target.cpp`, 1 new source, 1 header | implemented, see "Cut A" |
| B | one class for the FEM gain handlers of HeltecV4, HeltecV4R8, T096, HeltecTrackerV2 | 4 board classes, 1 new helper | implemented, see "Cut B" |
| C | a shared default `radio_init()` for the bodies that need no new description (28 + 3 of 89, all nRF52) | 31 `target.cpp`, 1 source, 1 header | implemented, see "Cut C" |
| D | the radio, wrapper and RTC globals from the board description (D1: 31 nRF52, D2: 22 ESP32 variants) | 53 `target.cpp` and `target.h`, 1 source | D1 implemented, D2 next, see "Cut D" |
| E | sensors, display and button from the board description | the rest | planned |

Cut B in more detail: the four bodies are the same once the class name is replaced,
and they all read and write the `fem_rxgain` key through `_prefs`. The open upstream
pull request meshcore-dev/MeshCore#3496 changes exactly those `setByKey` lines to
`if (_prefs) _prefs->setByKey(...)`, so a shared class has to accept a null prefs
pointer and the note for cut B has to say which of the two lands first.

Cut C: the 28 bodies of the largest group are `rtc_clock.begin(Wire); return
radio.std_init(&SPI);`. The groups differ in whether there is a `fallback_clock` to
start and which SPI object is passed, which is board description, so cut C and D
are related: C can share the bodies that need no new description, D introduces the
description.

What "board description" means is the open question for D and E. The options as
they look today: (1) the build flags and `target.h` macros the variants already use
(`RADIO_CLASS`, `WRAPPER_CLASS`, `P_LORA_*`, `DISPLAY_CLASS`) plus a few new ones,
read by the generic target; (2) a small `constexpr` struct per board in `target.h`;
(3) a data file per board from which the build generates `target.cpp`. Option 1
changes the least and keeps the PlatformIO files as the one place a board is
described; option 3 would be a new build step.

Decision (Marcel, 20.10.2026): option 1. The board description is the build flags
and `target.h` macros a variant already has, plus the few new ones cuts D and E
need. Options 2 and 3 are rejected: option 2 rewrites every variant, option 3 adds
a build step.

## Cut A: one `radio_new_identity()`

### What changes

- `src/helpers/radiolib/RadioLibTarget.cpp` (new) defines `radio_new_identity()`
  once, as a weak function, compiled only where `WRAPPER_CLASS` is set:
  ```cpp
  __attribute__((weak)) mesh::LocalIdentity radio_new_identity() {
    RadioNoiseListener rng(radio_driver);
    return mesh::LocalIdentity(&rng);
  }
  ```
- `RadioNoiseListener` gets a constructor from a `RadioLibWrapper`, and
  `RadioLibWrapper` names it a friend, so the listener can read the wrapped radio
  without a new public accessor.
- The 87 identical copies are deleted from `variants/*/target.cpp`. The diff in
  `variants/` is deletions only: 484 lines (each copy plus the blank line that
  separated it), no line added or changed, line endings kept (3 of the files use
  CRLF).
- The declaration stays in every `target.h`; the roles get it from there.
- The two ESP-NOW variants keep their own definition and are not touched.

### Why it is the same function

- `RadioLibTarget.cpp` is built in every firmware environment: `helpers/radiolib/*.cpp`
  is part of `arduino_base.build_src_filter`, which every variant extends. Checked
  with `pio project config --json-output` over all 631 environments: the 612 that set
  `WRAPPER_CLASS` all build `helpers/radiolib/*.cpp`.
- `WRAPPER_CLASS` is what tells a RadioLib variant apart. The six environments that
  build `helpers/radiolib/*.cpp` without it are the five ESP-NOW ones, which define
  their own `radio_new_identity()`, and `Generic_E22_kiss_modem` (see
  "Observations").
- The copies used the variant's global `radio`, which no `target.h` declares, so the
  shared code cannot name it. It uses `radio_driver` instead, which every `target.h`
  declares. In all 87 variants (and only there) `target.cpp` contains
  `WRAPPER_CLASS radio_driver(radio, board);`, so `radio_driver._radio` is `&radio`
  and the listener reads the same `PhysicalLayer` as before. Checked by script: the
  set of files with `RadioNoiseListener rng(radio)` and the set with that
  constructor line are the same 87.
- No copy was inside an `#if` block (checked by script), so no environment loses a
  definition it had or gains one it did not have.

### Why weak

A strong shared definition would make every remaining copy a duplicate symbol
error. Remaining copies exist in three places: the ESP-NOW variants, the variants in
downstream forks, and the open upstream pull requests that add a board (about 50 of
the 120 open pull requests whose diff was read carry a new `target.cpp` with its own
copy, see "Upstream"). With a weak definition all of them keep linking and their own
definition takes precedence. MeshCore already uses weak definitions for the same
reason in `src/armstubs.cpp`.

The cost: a variant that defines the function by mistake silently replaces the
shared one instead of failing the build. This is the same situation as today, where
each variant has its own.

Limit: weak precedence works because a variant's `target.cpp` is built as an object
of the firmware, as all variants are. A strong definition placed in a library
archive would not be pulled in, because the weak one already satisfies the
reference. No variant does that.

### Alternatives considered

- **A helper the variants call** (`return new_identity_from(radio);` in each
  `target.cpp`). Rejected: it keeps 87 definitions, and the next change to the
  signature touches 87 files again.
- **A strong definition and delete every copy.** Rejected: it breaks every downstream
  variant and every open pull request that adds a board, which rule 4 of the series
  (source compatible) excludes.
- **An opt-in macro per variant** (`#define TARGET_SHARED_NEW_IDENTITY`). Rejected:
  it touches every `target.h` as well, and a variant that forgets it fails to link
  with an error that does not say why.
- **Declare `extern RADIO_CLASS radio;` in the shared file.** Rejected: it relies on
  a name that is not part of any variant's declared interface; `radio_driver` is.
- **A public `getRadio()` on `RadioLibWrapper`.** Rejected for now: it widens the
  wrapper's interface for one caller. A friend keeps the change local; if cut D needs
  the radio from the driver in more places, an accessor can be proposed there.
- **Move identity creation into the wrapper** (`radio_driver.newIdentity()`).
  Rejected: identities are not the radio driver's concern.

### Host tests

`native_variant_target` (new) builds the real `RadioLibWrappers.cpp` and
`RadioLibTarget.cpp` against the RadioLib stand-in, with `test/mocks/radiolib_variant`
standing in for the `target.h` of a RadioLib variant. The Ed25519 constructor is
replaced by one that keeps the seed, which is the only input the real key derivation
has, so a test sees exactly what was drawn.

The first commit pins the variants' body (copied verbatim into the test) before
anything moves:

- the seed is 32 bytes, each the radio's random byte XOR the low byte of Arduino's
  `random(0, 256)`, in order,
- two radios with different noise give different identities.

The second commit adds, with the first two tests unchanged:

- the shared definition draws the same seed as the variants' copy,
- it listens on the radio that `radio_driver` wraps and not on another one,
- a listener built from the driver produces the same bytes as one built from the
  radio.

`native_variant_target_override` (new) defines its own `radio_new_identity()` and
checks that it is the one called; it does not link if the shared definition is not
weak.

The `PhysicalLayer` stand-in gets a per instance `random_byte` (default `0x5A`, the
value it returned so far), so a test can tell two radios apart. All 15 host
environments pass on both commits: 269 test cases on the first, 273 on the second.

Mutation tests, each of which must fail:

| mutation | result |
|---|---|
| M1: shared definition uses Arduino `random()` only, not the radio | 2 tests fail |
| M2: shared definition not weak | `native_variant_target_override` does not link (`duplicate symbol 'radio_new_identity()'`) |
| M3: shared definition guarded by `RADIO_CLASS` instead of `WRAPPER_CLASS` | `native_variant_target` does not link (`radio_new_identity()` undefined) |
| M4: `RadioNoiseListener` drops the `random()` mix | 2 tests fail |
| M5: `RadioNoiseListener` fills one byte less | 2 tests fail |

### Builds

102 environments were built from the commit this step started from (`2e456f74`,
`integration`) and from the code commit of this step (`248b1d12`), each in its own
worktree, with the same toolchain. 618 firmware environments is too many to build
twice for a change that is the same in every variant, so the set is chosen to cover
every file this step touches, every way the shared file can compile, and the usual
matrix of the series:

- the 18 environments every step of the series is built for (all roles, all
  platforms),
- one environment for each of the 87 variants whose `target.cpp` this step edits
  (a repeater where the variant has one), so every edited file is compiled and
  linked,
- one environment for each of the 13 pairs of `WRAPPER_CLASS` and platform that
  occur in the 612 RadioLib environments, so `RadioLibTarget.cpp` and the changed
  `RadioLibWrappers.h` are compiled against every wrapper class on every toolchain
  (only `Generic_E22_sx1268_repeater` had to be added for that),
- `Xiao_C6_repeater_` and `M5Stack_Unit_C6L_repeater`, the variants without a
  `target.cpp` (see below), and `Generic_ESPNOW_repeatr` for the variants without
  `WRAPPER_CLASS`.

Platforms in the set: 45 ESP32 (S3, C3, C6 and classic), 45 nRF52, 5 STM32WL, 7 RP2040.

Result:

- 100 build on both commits. 2 fail on both commits with the same errors, so not
  because of this step:
  - `ThinkNode_M8_repeater` does not link (`EnvironmentSensorManager` undefined, the
    variant does not build `helpers/sensors`). Its `target.cpp` compiles on both
    commits, and `radio_new_identity()` is not among the undefined symbols, so the
    shared definition is what it links. It is the only edited variant without an
    environment that links; the defect is the subject of a separate fix.
  - `Generic_ESPNOW_repeatr` does not compile (`P_LORA_DIO_1` and `P_LORA_NSS`
    undeclared in `ESP32Board.h`). The other three ESP-NOW environments tried
    (`Generic_ESPNOW_room_svr`, `Generic_ESPNOW_comp_radio_usb`,
    `SenseCapIndicator-ESPNow_comp_radio_usb`) fail the same way on both commits.
    That `RadioLibTarget.cpp` stays empty there rests on the configuration check
    above (none of the ESP-NOW environments sets `WRAPPER_CLASS`), not on a build.
- RAM is unchanged in all 100.
- Flash changes by -16 to +16 bytes: 38 environments +0, 11 smaller (-8 or -16), 51
  larger (+4 to +16). The function now loads the radio from `radio_driver` instead
  of taking the address of `radio`, which is one load more, and it sits in another
  translation unit, so the alignment padding around it moves. Measured with `nm`:
  `radio_new_identity()` is 0x24 bytes on `RAK_4631_repeater` before and after,
  0x16 and 0x1b bytes on `Heltec_v3_repeater`, and it is a weak symbol (`W`) in the
  new firmware where it was a normal one (`T`).
- The two C6 variants define `radio_new_identity()` in their board file, so they have
  their own definition next to the weak shared one. Both build to exactly the same
  size as before, and `nm` shows their own definition (`T`) in the firmware: the
  precedence the override test checks on the host, on the RISC-V toolchain.

<details>
<summary>All 102 environments</summary>

| Environment | Platform | Wrapper | Flash before | Flash after | Flash delta | RAM delta |
|---|---|---|---:|---:|---:|---:|
| `Heltec_v3_repeater` | espressif32 | CustomSX1262Wrapper | 1,174,193 | 1,174,209 | +16 | +0 |
| `Heltec_v3_room_server` | espressif32 | CustomSX1262Wrapper | 1,169,481 | 1,169,497 | +16 | +0 |
| `Heltec_v3_companion_radio_ble` | espressif32 | CustomSX1262Wrapper | 1,666,081 | 1,666,097 | +16 | +0 |
| `Heltec_v3_companion_radio_wifi` | espressif32 | CustomSX1262Wrapper | 1,102,253 | 1,102,269 | +16 | +0 |
| `Heltec_v3_sensor` | espressif32 | CustomSX1262Wrapper | 1,180,701 | 1,180,717 | +16 | +0 |
| `RAK_4631_repeater` | nordicnrf52 | CustomSX1262Wrapper | 441,384 | 441,368 | -16 | +0 |
| `RAK_4631_room_server` | nordicnrf52 | CustomSX1262Wrapper | 434,552 | 434,552 | +0 | +0 |
| `RAK_4631_companion_radio_ble` | nordicnrf52 | CustomSX1262Wrapper | 462,084 | 462,068 | -16 | +0 |
| `RAK_4631_sensor` | nordicnrf52 | CustomSX1262Wrapper | 438,152 | 438,136 | -16 | +0 |
| `PicoW_repeater` | platform-raspberrypi.git | CustomSX1262Wrapper | 492,420 | 492,420 | +0 | +0 |
| `wio-e5-mini_repeater` | ststm32 | CustomSTM32WLxWrapper | 203,832 | 203,832 | +0 | +0 |
| `wio-e5-mini_sensor` | ststm32 | CustomSTM32WLxWrapper | 203,304 | 203,304 | +0 | +0 |
| `RAK_3112_repeater_bridge_espnow` | espressif32 | CustomSX1262Wrapper | 1,175,509 | 1,175,521 | +12 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | platform-raspberrypi.git | CustomSX1262Wrapper | 240,832 | 240,832 | +0 | +0 |
| `ProMicro_repeater` | nordicnrf52 | CustomSX1262Wrapper | 345,824 | 345,824 | +0 | +0 |
| `ProMicro_room_server` | nordicnrf52 | CustomSX1262Wrapper | 338,920 | 338,920 | +0 | +0 |
| `ProMicro_companion_radio_usb` | nordicnrf52 | CustomSX1262Wrapper | 356,836 | 356,836 | +0 | +0 |
| `ProMicro_sensor` | nordicnrf52 | CustomSX1262Wrapper | 342,476 | 342,476 | +0 | +0 |
| `Ebyte_EoRa-S3_Repeater` | espressif32 | CustomSX1262Wrapper | 1,107,901 | 1,107,913 | +12 | +0 |
| `GAT562_30S_Mesh_Kit_repeater` | nordicnrf52 | CustomSX1262Wrapper | 410,992 | 410,992 | +0 | +0 |
| `GAT562_Mesh_EVB_Pro_repeater` | nordicnrf52 | CustomSX1262Wrapper | 365,220 | 365,220 | +0 | +0 |
| `GAT562_Mesh_Tracker_Pro_repeater` | nordicnrf52 | CustomSX1262Wrapper | 380,684 | 380,700 | +16 | +0 |
| `GAT562_Mesh_Watch13_companion_radio_ble` | nordicnrf52 | CustomSX1262Wrapper | 397,480 | 397,480 | +0 | +0 |
| `Generic_E22_sx1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,142,973 | 1,142,985 | +12 | +0 |
| `Heltec_ct62_repeater` | espressif32 | CustomSX1262Wrapper | 1,151,498 | 1,151,490 | -8 | +0 |
| `Heltec_E213_repeater` | espressif32 | CustomSX1262Wrapper | 1,161,701 | 1,161,713 | +12 | +0 |
| `Heltec_E290_repeater` | espressif32 | CustomSX1262Wrapper | 1,159,617 | 1,159,629 | +12 | +0 |
| `Heltec_mesh_solar_repeater` | nordicnrf52 | CustomSX1262Wrapper | 365,184 | 365,184 | +0 | +0 |
| `heltec_rc32_repeater` | espressif32 | CustomSX1262Wrapper | 1,168,277 | 1,168,293 | +16 | +0 |
| `Heltec_t096_repeater` | nordicnrf52 | CustomSX1262Wrapper | 413,888 | 413,904 | +16 | +0 |
| `Heltec_t1_repeater` | nordicnrf52 | CustomSX1262Wrapper | 412,948 | 412,964 | +16 | +0 |
| `Heltec_t114_repeater` | nordicnrf52 | CustomSX1262Wrapper | 397,884 | 397,884 | +0 | +0 |
| `Heltec_T190_repeater_` | espressif32 | CustomSX1262Wrapper | 1,138,889 | 1,138,897 | +8 | +0 |
| `Heltec_tower_v2_repeater` | nordicnrf52 | CustomSX1262Wrapper | 309,128 | 309,144 | +16 | +0 |
| `Heltec_Wireless_Tracker_repeater` | espressif32 | CustomSX1262Wrapper | 1,157,053 | 1,157,065 | +12 | +0 |
| `heltec_tracker_v2_repeater` | espressif32 | CustomSX1262Wrapper | 1,225,429 | 1,225,437 | +8 | +0 |
| `Heltec_v2_repeater` | espressif32 | CustomSX1276Wrapper | 1,151,425 | 1,151,437 | +12 | +0 |
| `heltec_v4_expansionkit_repeater` | espressif32 | CustomSX1262Wrapper | 1,172,333 | 1,172,341 | +8 | +0 |
| `heltec_v4_r8_repeater` | espressif32 | CustomSX1262Wrapper | 1,175,789 | 1,175,793 | +4 | +0 |
| `Heltec_Wireless_Paper_repeater` | espressif32 | CustomSX1262Wrapper | 1,121,353 | 1,121,365 | +12 | +0 |
| `ikoka_handheld_nrf_e22_30dbm_repeater` | nordicnrf52 | CustomSX1262Wrapper | 357,720 | 357,720 | +0 | +0 |
| `ikoka_nano_nrf_22dbm_repeater` | nordicnrf52 | CustomSX1262Wrapper | 360,616 | 360,616 | +0 | +0 |
| `ikoka_stick_nrf_22dbm_repeater` | nordicnrf52 | CustomSX1262Wrapper | 373,192 | 373,192 | +0 | +0 |
| `KeepteenLT1_repeater` | nordicnrf52 | CustomSX1262Wrapper | 321,612 | 321,628 | +16 | +0 |
| `LilyGo_T3S3_sx1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,107,949 | 1,107,961 | +12 | +0 |
| `LilyGo_T3S3_sx1276_repeater` | espressif32 | CustomSX1276Wrapper | 1,105,021 | 1,105,033 | +12 | +0 |
| `LilyGo_T_Impulse_Plus_repeater` | nordicnrf52 | CustomSX1262Wrapper | 365,932 | 365,948 | +16 | +0 |
| `LilyGo_TBeam_1W_repeater` | espressif32 | CustomSX1262Wrapper | 1,154,409 | 1,154,425 | +16 | +0 |
| `Tbeam_SX1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,204,605 | 1,204,617 | +12 | +0 |
| `Tbeam_SX1276_repeater` | espressif32 | CustomSX1276Wrapper | 1,200,645 | 1,200,653 | +8 | +0 |
| `T_Beam_S3_Supreme_SX1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,154,977 | 1,154,993 | +16 | +0 |
| `LilyGo_TDeck_repeater` | espressif32 | CustomSX1262Wrapper | 1,144,121 | 1,144,137 | +16 | +0 |
| `LilyGo_T-Echo_repeater` | nordicnrf52 | CustomSX1262Wrapper | 349,488 | 349,488 | +0 | +0 |
| `LilyGo_T-Echo_Card_repeater` | nordicnrf52 | CustomSX1262Wrapper | 321,952 | 321,952 | +0 | +0 |
| `LilyGo_T-Echo-Lite_repeater` | nordicnrf52 | CustomSX1262Wrapper | 335,340 | 335,340 | +0 | +0 |
| `LilyGo_TETH_Elite_sx1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,075,753 | 1,075,765 | +12 | +0 |
| `LilyGo_Tlora_C6_repeater_` | platform-espressif32.zip | CustomSX1262Wrapper | 542,067 | 542,077 | +10 | +0 |
| `LilyGo_TLora_V2_1_1_6_repeater` | espressif32 | CustomSX1276Wrapper | 1,662,685 | 1,662,693 | +8 | +0 |
| `Mesh_pocket_repeater` | nordicnrf52 | CustomSX1262Wrapper | 326,456 | 326,456 | +0 | +0 |
| `Meshadventurer_sx1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,161,541 | 1,161,553 | +12 | +0 |
| `meshnology_w12_repeater` | espressif32 | CustomLR2021Wrapper | 1,182,665 | 1,182,677 | +12 | +0 |
| `Meshtiny_companion_radio_ble` | nordicnrf52 | CustomSX1262Wrapper | 335,568 | 335,584 | +16 | +0 |
| `MeshTracker_X1_repeater` | nordicnrf52 | CustomLR2021Wrapper | 300,752 | 300,736 | -16 | +0 |
| `Minewsemi_me25ls01_repeater` | nordicnrf52 | CustomLR1110Wrapper | 306,592 | 306,592 | +0 | +0 |
| `muzi_base_duo_repeater` | nordicnrf52 | CustomLR1121Wrapper | 296,948 | 296,932 | -16 | +0 |
| `R1Neo_repeater` | nordicnrf52 | CustomSX1262Wrapper | 368,356 | 368,356 | +0 | +0 |
| `Nano_G2_Ultra_repeater` | nordicnrf52 | CustomSX1262Wrapper | 319,084 | 319,084 | +0 | +0 |
| `nibble_screen_connect_repeater_` | espressif32 | CustomSX1262Wrapper | 1,102,645 | 1,102,657 | +12 | +0 |
| `nibble_zero_connect_repeater_` | espressif32 | CustomSX1262Wrapper | 1,102,745 | 1,102,757 | +12 | +0 |
| `RAK_11310_repeater` | platform-raspberrypi.git | CustomSX1262Wrapper | 235,924 | 235,924 | +0 | +0 |
| `RAK_3401_repeater` | nordicnrf52 | CustomSX1262Wrapper | 410,464 | 410,464 | +0 | +0 |
| `RAK_3x72_repeater` | ststm32 | CustomSTM32WLxWrapper | 196,472 | 196,472 | +0 | +0 |
| `RAK_WisMesh_Tag_repeater` | nordicnrf52 | CustomSX1262Wrapper | 368,516 | 368,516 | +0 | +0 |
| `SenseCap_Solar_repeater` | nordicnrf52 | CustomSX1262Wrapper | 365,532 | 365,548 | +16 | +0 |
| `Station_G2_logging_repeater` | espressif32 | CustomSX1262Wrapper | 1,207,117 | 1,207,129 | +12 | +0 |
| `Station_G3_ESP32_logging_repeater` | espressif32 | CustomSX1262Wrapper | 1,208,705 | 1,208,721 | +16 | +0 |
| `t1000e_repeater` | nordicnrf52 | CustomLR1110Wrapper | 294,360 | 294,344 | -16 | +0 |
| `Tenstar_C3_sx1262_repeater` | espressif32 | CustomSX1262Wrapper | 1,152,016 | 1,152,016 | +0 | +0 |
| `ThinkNode_M1_repeater` | nordicnrf52 | CustomSX1262Wrapper | 305,780 | 305,764 | -16 | +0 |
| `ThinkNode_M2_Repeater` | espressif32 | CustomSX1262Wrapper | 1,109,625 | 1,109,637 | +12 | +0 |
| `ThinkNode_M3_repeater` | nordicnrf52 | CustomLR1110Wrapper | 301,908 | 301,908 | +0 | +0 |
| `ThinkNode_M4_repeater` | nordicnrf52 | CustomLR1110Wrapper | 301,884 | 301,900 | +16 | +0 |
| `ThinkNode_M5_Repeater` | espressif32 | CustomSX1262Wrapper | 1,126,905 | 1,126,917 | +12 | +0 |
| `ThinkNode_M6_repeater` | nordicnrf52 | CustomSX1262Wrapper | 365,652 | 365,668 | +16 | +0 |
| `ThinkNode_M7_repeater` | espressif32 | CustomLR1110Wrapper | 1,088,661 | 1,088,673 | +12 | +0 |
| `ThinkNode_M8_repeater` | nordicnrf52 | CustomSX1262Wrapper | FAIL | FAIL | | |
| `ThinkNode_M9_repeater_` | espressif32 | CustomLR1110Wrapper | 1,182,137 | 1,182,153 | +16 | +0 |
| `Tiny_Relay_repeater` | ststm32 | CustomSTM32WLxWrapper | 196,480 | 196,480 | +0 | +0 |
| `Waveshare_S3_LR1121_repeater` | espressif32 | CustomLR1121Wrapper | 1,084,309 | 1,084,317 | +8 | +0 |
| `wio-e5-repeater_bridge_rs232` | ststm32 | CustomSTM32WLxWrapper | 198,760 | 198,768 | +8 | +0 |
| `WioTrackerL1_repeater` | nordicnrf52 | CustomSX1262Wrapper | 377,148 | 377,148 | +0 | +0 |
| `WioTrackerL1-1W_repeater` | nordicnrf52 | CustomSX1262Wrapper | 378,172 | 378,172 | +0 | +0 |
| `wio_wm1110_repeater` | nordicnrf52 | CustomLR1110Wrapper | 344,828 | 344,828 | +0 | +0 |
| `Xiao_C3_repeater` | espressif32 | CustomSX1262Wrapper | 1,157,598 | 1,157,590 | -8 | +0 |
| `Xiao_nrf52_repeater` | nordicnrf52 | CustomSX1262Wrapper | 361,224 | 361,224 | +0 | +0 |
| `Xiao_rp2040_repeater` | platform-raspberrypi.git | CustomSX1262Wrapper | 235,548 | 235,548 | +0 | +0 |
| `Xiao_S3_repeater` | espressif32 | CustomSX1262Wrapper | 1,136,809 | 1,136,821 | +12 | +0 |
| `Xiao_S3_WIO_repeater` | espressif32 | CustomSX1262Wrapper | 1,127,189 | 1,127,201 | +12 | +0 |
| `Generic_E22_sx1268_repeater` | espressif32 | CustomSX1268Wrapper | 1,142,769 | 1,142,781 | +12 | +0 |
| `Generic_ESPNOW_repeatr` | espressif32 | none | FAIL | FAIL | | |
| `Xiao_C6_repeater_` | platform-espressif32.zip | CustomSX1262Wrapper | 542,095 | 542,095 | +0 | +0 |
| `M5Stack_Unit_C6L_repeater` | platform-espressif32.zip | CustomSX1262Wrapper | 552,445 | 552,445 | +0 | +0 |

</details>

### Hardware

No hardware evidence. The change moves a function whose body is unchanged into
another translation unit, and the builds show the firmware links the shared
definition. A board run would only show that a new identity is created on first
boot, which nothing in this step can change differently per board. If a reviewer
wants one, the check is: erase the identity on a board (`/identity` on the file
system), boot this firmware, see a new public key in `ver`/advert.

## Cut B: one FEM RX gain handler

### What changes

- `src/helpers/FemRxGain.h` (new) holds the bodies of `attachDynamicPrefs()` and
  `handleCommand()` that HeltecV4Board, HeltecV4R8Board, T096Board and
  HeltecTrackerV2Board carried, as a class template on the board class:
  ```cpp
  template <class BOARD>
  class FemRxGain {
    KeyValueStore* _prefs = NULL;
  public:
    void attachPrefs(BOARD& board, KeyValueStore* prefs);
    bool handleCommand(BOARD& board, const char* command, char* reply);
  };
  ```
  The bodies are the boards' own, with `board.` in front of the board members they
  use (`loRaFEMControl`, `setLoRaFemLnaEnabled()`, `isLoRaFemLnaEnabled()`) and one
  difference, the null check described below.
- Each of the four board headers replaces `KeyValueStore* _prefs = NULL;` with
  ```cpp
  friend class FemRxGain<HeltecV4Board>;
  FemRxGain<HeltecV4Board> fem_rxgain;
  ```
  and includes the new header. Each board source keeps both functions, as one line
  that forwards to `fem_rxgain`. The diff in `variants/` is 8 files, 20 lines added,
  156 removed.
- Unchanged: the boards' base classes, their public interface (both functions keep
  their signature, `handleCommand()` still overrides `MainBoard`), their own
  `setLoRaFemLnaEnabled()` and `isLoRaFemLnaEnabled()`, which stay private
  (protected in T096Board), and their `LoRaFEMControl` classes.

Measured with `tools/refactoring/variant_duplication.py` after the step: the four
boards' `attachDynamicPrefs()` is 94 characters normalized (was 217),
`handleCommand()` 146 (was 999), still one body for the four, now a forwarding line.
TBeam1W, StationG3 and WioTrackerL1 keep their own handlers (fan settings, other
commands), as planned.

### Why it is the same code

- The bodies were compared as text before the move (one body for the four once the
  class name is replaced, see "Measured on the commit this step started from"). The
  host tests run a verbatim copy of the HeltecV4Board bodies and the shared template
  against the same cases, with the same expectations (see "Host tests").
- `_prefs` keeps its place: it was the first data member of each board and
  `fem_rxgain` is now, and `FemRxGain` holds nothing but that pointer (a test checks
  `sizeof(FemRxGain<...>) == sizeof(KeyValueStore*)`). So the boards keep size and
  layout, and RAM is unchanged in every build (see "Builds").
- The callers are unchanged: `ServerMesh::begin()` and the companion's `begin()` call
  `board.attachDynamicPrefs(_prefs.getCustom())`, `CommonCLI` calls
  `_board->handleCommand(...)` (the three server roles). Both go through the board's
  own functions as before.

### The null store, and how this relates to meshcore-dev/MeshCore#3496

The open upstream pull request #3496 (KISS modem AGC and FEM gain control) changes
the two `setByKey` lines in each of the four boards to
`if (_prefs) _prefs->setByKey(...)`, because its KISS modem calls the board's
`handleCommand()` without ever calling `attachDynamicPrefs()`, and the copies then
dereference NULL. The shared code has the same check, in the same two places:

```cpp
if (_prefs) _prefs->setByKey("fem_rxgain", "1");
```

- Behaviour in this tree: unchanged. The KISS modem here does not call
  `handleCommand()`, and every role that does (the three server roles through
  `CommonCLI`) attaches the store in `ServerMesh::begin()` first. So no reply and no
  store write changes; only the case that dereferenced NULL before is now defined
  (the LNA is switched, the reply is the same, nothing is stored). A test pins that.
- `attachPrefs(board, NULL)` is not guarded, as in the copies and in #3496: no caller
  passes NULL, and what a board should do without a store at boot (LNA on, off or
  unchanged) is a product decision this step does not make.
- Order: the check is taken over now so that the shared code is correct for either
  order. If this step lands first, #3496 drops its four variant hunks on rebase (they
  edit lines that no longer exist and the shared code already has the check); the
  rest of #3496 is not touched by this step. If #3496 lands first, this step's
  variant diff deletes the guarded lines instead of the unguarded ones and nothing
  else changes. Checked on 20.10.2026: the variant hunks of #3496 are exactly those
  two lines per board, 8 in all (`git diff upstream/dev pull/3496/head` per file), and
  applied to this step's tree with `git apply --check -3` all four conflict, as
  expected.

The other two upstream pull requests that touch these four files:

- **#3453** (standardized `powerOff()`, `shutdownPeripherals()`, `enterDeepSleep()`,
  open) edits `powerOff()` in all four sources and headers and removes
  `HeltecV4R8Board::enterDeepSleep()`. It does not change the FEM handlers. Applied to
  this step's tree with `git apply --check -3`: six of the eight files apply cleanly,
  `HeltecV4Board.cpp` and `HeltecTrackerV2Board.cpp` conflict on one hunk that only
  re-indents the closing brace of `handleCommand()`, a brace this step replaces. A
  rebase resolution, no logic.
- **#2682** (ThinkNode M4 variant, open) lists the four files, but its head is
  identical to `upstream/dev` in all of them (0 changed lines,
  `git diff upstream/dev pull/2682/head` per file): the entries come from a merge of
  `dev` into the branch. No conflict.

### Alternatives considered

- **A base class with virtual calls** (`class FemLnaBoard { virtual bool
  setLoRaFemLnaEnabled(bool) = 0; ... }` and a non-template handler compiled once).
  Rejected: it adds a vtable pointer to each board object (RAM) and indirect calls
  for code that exists once per firmware anyway, because a firmware contains exactly
  one board. Upstream had such virtuals on `MainBoard` (`setLoRaFemLnaEnabled()`,
  `canControlLoRaFemLna()`, `isLoRaFemLnaEnabled()`, with the commands in `CommonCLI`)
  and removed them in August 2026 (`47b0b7b3`, `a1cf5bd8`), moving the commands into
  the boards; going back to them is not this step's call.
- **A mixin between the board and its base** (`class HeltecV4Board : public
  FemRxGainBoard<ESP32Board, HeltecV4Board>`). Rejected: it changes each board's base
  class, and T096Board initializes its virtual base `NRF52Board` directly, which a
  mixin template would have to forward. The forwarding lines it saves are two per
  board.
- **Move `setLoRaFemLnaEnabled()` and `isLoRaFemLnaEnabled()` into the shared code
  as well.** Not in this step: they are identical in the four, but they belong with
  the boards' `LoRaFEMControl`, which is its own class per variant (the four boards'
  `LoRaFEMControl.cpp` all differ, as do those of HeltecTowerV2 and StationG3). That
  is a separate concern for a later step.
- **Template on the FEM control class instead of the board.** Rejected: the handler
  needs the board's `setLoRaFemLnaEnabled()`, which also puts the FEM back into RX
  mode, not only the FEM control.

### Host tests

`test_fem_rxgain` (new, in the `native` environment, header only) is a typed suite
that runs the same cases against two boards built on a stand-in for the boards'
common parts (public `loRaFEMControl`, the boards' own `setLoRaFemLnaEnabled()` and
`isLoRaFemLnaEnabled()`, plus a switch that makes a switch fail):

- the first commit: a verbatim copy of the HeltecV4Board bodies, class name replaced,
- the second commit adds the shared `FemRxGain`, with the cases unchanged.

The store is a recording `KeyValueStore` whose `getByKey()` copies like
`DynamicConfigSerializer` (strncpy, terminator at `max_len`). Commands are passed in a
zero filled buffer, as `CommonCLI` passes them. Pinned:

- attach reads `fem_rxgain` once with `max_len` 7, switches the LNA on only for exactly
  `"1"` (off for `"0"`, `""`, `"on"`, `"10"`, `" 1"`, `"1 "`, `"true"`, a missing key and a
  value cut to seven characters), tries even when the LNA cannot be controlled, and
  writes nothing,
- `get radio.fem.rxgain`: `> on`, `> off`, or `Error: unsupported`,
- `set radio.fem.rxgain on|off`: switches the LNA, writes `"1"` or `"0"` to the store
  attached last, replies `OK - LoRa FEM RX gain on|off`, ignores the store's return
  value; the state matches by its first two or three characters (`onward`, `offline`);
  any other state is `Error: state must be on or off`; `Error: unsupported` when the
  LNA cannot be controlled; `Error: failed to apply LoRa FEM RX gain` and nothing
  stored when the switch fails,
- every other command (`get radio.fem.rxgain ` with a trailing space, `set
  radio.fem.rxgain` without one, other keys, upper case, empty) is not handled and
  the reply is left as it was.

The second commit adds two tests for the shared code only: `set` without an attached
store switches the LNA and replies as with one, and `FemRxGain` is the size of a
pointer.

All 15 host environments pass on both commits: 289 test cases on the first, 307 on
the second (273 on cut A).

Mutation tests on the shared code, each of which must fail:

| mutation | result |
|---|---|
| M1: drop the null check before the `"1"` write | the test without a store crashes (SIGSEGV), the suite fails |
| M2: store `"on"` instead of `"1"` | 1 test fails |
| M3: attach switches on for anything but `"1"` (`!= 0`) | 6 tests fail |
| M4: match the `set` prefix with 20 instead of 21 characters | 1 test fails (`set radio.fem.rxgain` without the space is handled) |
| M5: read the stored value with `max_len` 8 | the terminator lands outside the 8 byte buffer, the stack protector aborts the suite |
| M6: write `"0"` before switching the LNA off | 3 tests fail |
| M7: match `off` by two characters | 1 test fails (`of` is taken as off) |
| M8: reply `>on` instead of `> on` | 2 tests fail |
| M9: drop the `unsupported` check of `set` | 1 test fails |

### Builds

68 environments were built from cut A (`a3d3961e`, the commit this step started
from; the test commit changes no firmware source) and from the code commit of this
step (`2884fd34`), each side in its own worktrees, with the same toolchain:

- the 18 environments every step of the series is built for (all roles, all
  platforms),
- all 50 environments of the four boards (every environment whose
  `build_src_filter` names `variants/heltec_v4`, `heltec_v4_r8`, `heltec_t096` or
  `heltec_tracker_v2`, from `pio project config`): repeater, room server, sensor,
  terminal chat, companion USB, BLE and WiFi, KISS modem and the bridges, OLED and
  TFT. No other source file includes the four board headers (checked with `grep`).

Platforms in the set: 49 ESP32, 15 nRF52, 2 STM32WL, 2 RP2040. Of the four boards,
T096 is nRF52 (7 environments) and the other three are ESP32-S3 (43).

Result:

- 67 build on both commits. `heltec_tracker_v2_kiss_modem` fails on both with the
  same error (`examples/kiss_modem/main.cpp:118`, `USBCDC` has no
  `setTxBufferSize`), in the KISS modem, not in the board; the other three boards'
  KISS modem environments build and link the shared code.
- Five builds were stopped by a signal (exit 143) on the first run, by another
  session's `pkill` on the same machine, and were rebuilt from clean: all five
  build.
- RAM is unchanged in all 67.
- Flash: unchanged in the 18 series environments (none builds one of the four
  boards). In the 49 environments of the four boards it grows by 16 to 72 bytes:
  - T096 (nRF52), 7 environments: +16. Measured with `nm`: `T096Board::handleCommand()`
    and its virtual thunk (the board has a virtual base) are each 8 bytes larger
    (0x148 to 0x150, 0x14c to 0x150), the two null checks; the template is inlined.
  - ESP32, 42 environments: +24 to +72, most +56. On `heltec_v4_repeater` (+56):
    `.flash.text` +32, of which `FemRxGain<HeltecV4Board>::handleCommand()` is 8 bytes
    larger than the board's own body was (0xe2 to 0xea, the null checks) and the
    board's `handleCommand()` is now a forwarder of 0x15 bytes, with alignment
    between functions; `.flash.rodata` +24, not attributed further.
    `attachDynamicPrefs()` is inlined into the board's function and keeps its size
    (0x52).

The growth is the price of the null check and, on ESP32, of one forwarding function
that the compiler does not merge with the template. A board without a virtual base
could avoid the forwarder only by making `handleCommand()` the template's own
override (the rejected mixin).

<details>
<summary>All 68 environments</summary>

| Environment | Flash before | Flash after | Flash delta | RAM before | RAM after | RAM delta |
|---|---:|---:|---:|---:|---:|---:|
| `Heltec_v3_repeater` | 1,174,209 | 1,174,209 | +0 | 62,512 | 62,512 | +0 |
| `Heltec_v3_room_server` | 1,169,497 | 1,169,497 | +0 | 66,200 | 66,200 | +0 |
| `Heltec_v3_companion_radio_ble` | 1,666,097 | 1,666,097 | +0 | 194,844 | 194,844 | +0 |
| `Heltec_v3_companion_radio_wifi` | 1,102,269 | 1,102,269 | +0 | 172,384 | 172,384 | +0 |
| `Heltec_v3_sensor` | 1,180,717 | 1,180,717 | +0 | 60,600 | 60,600 | +0 |
| `RAK_4631_repeater` | 441,368 | 441,368 | +0 | 37,240 | 37,240 | +0 |
| `RAK_4631_room_server` | 434,552 | 434,552 | +0 | 40,928 | 40,928 | +0 |
| `RAK_4631_companion_radio_ble` | 462,068 | 462,068 | +0 | 149,768 | 149,768 | +0 |
| `RAK_4631_sensor` | 438,136 | 438,136 | +0 | 35,328 | 35,328 | +0 |
| `PicoW_repeater` | 492,420 | 492,420 | +0 | 85,368 | 85,368 | +0 |
| `wio-e5-mini_repeater` | 203,832 | 203,832 | +0 | 17,172 | 17,172 | +0 |
| `wio-e5-mini_sensor` | 203,304 | 203,304 | +0 | 15,276 | 15,276 | +0 |
| `RAK_3112_repeater_bridge_espnow` | 1,175,521 | 1,175,521 | +0 | 64,072 | 64,072 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | 240,832 | 240,832 | +0 | 26,704 | 26,704 | +0 |
| `ProMicro_repeater` | 345,824 | 345,824 | +0 | 33,892 | 33,892 | +0 |
| `ProMicro_room_server` | 338,920 | 338,920 | +0 | 37,588 | 37,588 | +0 |
| `ProMicro_companion_radio_usb` | 356,836 | 356,836 | +0 | 99,468 | 99,468 | +0 |
| `ProMicro_sensor` | 342,476 | 342,476 | +0 | 31,988 | 31,988 | +0 |
| `heltec_v4_r8_repeater` | 1,175,793 | 1,175,857 | +64 | 62,400 | 62,400 | +0 |
| `heltec_v4_r8_room_server` | 1,171,153 | 1,171,209 | +56 | 66,088 | 66,088 | +0 |
| `heltec_v4_r8_terminal_chat` | 603,329 | 603,389 | +60 | 95,752 | 95,752 | +0 |
| `heltec_v4_r8_companion_radio_usb` | 654,449 | 654,509 | +60 | 103,288 | 103,288 | +0 |
| `heltec_v4_r8_companion_radio_ble` | 1,667,785 | 1,667,845 | +60 | 194,732 | 194,732 | +0 |
| `heltec_v4_r8_companion_radio_wifi` | 1,103,937 | 1,103,993 | +56 | 172,272 | 172,272 | +0 |
| `heltec_v4_r8_sensor` | 1,182,385 | 1,182,441 | +56 | 60,488 | 60,488 | +0 |
| `heltec_v4_r8_tft_repeater` | 1,177,885 | 1,177,941 | +56 | 62,496 | 62,496 | +0 |
| `heltec_v4_r8_tft_room_server` | 1,173,109 | 1,173,165 | +56 | 66,192 | 66,192 | +0 |
| `heltec_v4_r8_tft_terminal_chat` | 603,721 | 603,777 | +56 | 95,752 | 95,752 | +0 |
| `heltec_v4_r8_tft_companion_radio_usb` | 658,313 | 658,369 | +56 | 103,440 | 103,440 | +0 |
| `heltec_v4_r8_tft_companion_radio_ble` | 1,284,809 | 1,284,869 | +60 | 172,044 | 172,044 | +0 |
| `heltec_v4_r8_tft_companion_radio_wifi` | 1,107,789 | 1,107,845 | +56 | 172,424 | 172,424 | +0 |
| `heltec_v4_r8_tft_sensor` | 1,184,369 | 1,184,429 | +60 | 60,592 | 60,592 | +0 |
| `heltec_v4_r8_kiss_modem` | 582,409 | 582,465 | +56 | 24,464 | 24,464 | +0 |
| `heltec_v4_r8_tft_kiss_modem` | 582,797 | 582,853 | +56 | 24,464 | 24,464 | +0 |
| `Heltec_t096_repeater` | 413,904 | 413,920 | +16 | 35,564 | 35,564 | +0 |
| `Heltec_t096_repeater_bridge_rs232` | 416,144 | 416,160 | +16 | 37,148 | 37,148 | +0 |
| `Heltec_t096_room_server` | 407,040 | 407,056 | +16 | 39,260 | 39,260 | +0 |
| `Heltec_t096_sensor` | 410,724 | 410,740 | +16 | 33,660 | 33,660 | +0 |
| `Heltec_t096_companion_radio_ble` | 431,524 | 431,540 | +16 | 148,052 | 148,052 | +0 |
| `Heltec_t096_companion_radio_usb` | 424,788 | 424,804 | +16 | 101,140 | 101,140 | +0 |
| `Heltec_t096_kiss_modem` | 340,360 | 340,376 | +16 | 21,700 | 21,700 | +0 |
| `heltec_v4_repeater` | 1,172,273 | 1,172,329 | +56 | 62,476 | 62,476 | +0 |
| `heltec_v4_expansionkit_repeater` | 1,172,341 | 1,172,393 | +52 | 62,476 | 62,476 | +0 |
| `heltec_v4_repeater_bridge_espnow` | 1,182,797 | 1,182,857 | +60 | 64,132 | 64,132 | +0 |
| `heltec_v4_room_server` | 1,167,677 | 1,167,745 | +68 | 66,164 | 66,164 | +0 |
| `heltec_v4_terminal_chat` | 599,877 | 599,933 | +56 | 95,832 | 95,832 | +0 |
| `heltec_v4_companion_radio_usb` | 651,001 | 651,057 | +56 | 103,368 | 103,368 | +0 |
| `heltec_v4_companion_radio_ble` | 1,664,305 | 1,664,357 | +52 | 194,808 | 194,808 | +0 |
| `heltec_v4_companion_radio_wifi` | 1,100,445 | 1,100,501 | +56 | 172,348 | 172,348 | +0 |
| `heltec_v4_sensor` | 1,178,909 | 1,178,965 | +56 | 60,564 | 60,564 | +0 |
| `heltec_v4_tft_repeater` | 1,174,021 | 1,174,077 | +56 | 62,572 | 62,572 | +0 |
| `heltec_v4_tft_repeater_bridge_espnow` | 1,184,529 | 1,184,581 | +52 | 64,228 | 64,228 | +0 |
| `heltec_v4_tft_room_server` | 1,169,221 | 1,169,277 | +56 | 66,268 | 66,268 | +0 |
| `heltec_v4_tft_terminal_chat` | 599,877 | 599,933 | +56 | 95,832 | 95,832 | +0 |
| `heltec_v4_tft_companion_radio_usb` | 652,677 | 652,733 | +56 | 103,464 | 103,464 | +0 |
| `heltec_v4_tft_companion_radio_ble` | 1,279,125 | 1,279,181 | +56 | 172,056 | 172,056 | +0 |
| `heltec_v4_tft_companion_radio_wifi` | 1,102,173 | 1,102,229 | +56 | 172,444 | 172,444 | +0 |
| `heltec_v4_tft_sensor` | 1,180,513 | 1,180,569 | +56 | 60,668 | 60,668 | +0 |
| `heltec_v4_kiss_modem` | 578,941 | 578,997 | +56 | 24,544 | 24,544 | +0 |
| `heltec_tracker_v2_repeater` | 1,225,437 | 1,225,501 | +64 | 74,344 | 74,344 | +0 |
| `heltec_tracker_v2_repeater_bridge_espnow` | 1,235,873 | 1,235,917 | +44 | 76,000 | 76,000 | +0 |
| `heltec_tracker_v2_room_server` | 1,220,737 | 1,220,793 | +56 | 78,040 | 78,040 | +0 |
| `heltec_tracker_v2_terminal_chat` | 627,357 | 627,413 | +56 | 107,480 | 107,480 | +0 |
| `heltec_tracker_v2_companion_radio_usb` | 707,033 | 707,105 | +72 | 115,272 | 115,272 | +0 |
| `heltec_tracker_v2_companion_radio_ble` | 1,333,625 | 1,333,681 | +56 | 183,836 | 183,836 | +0 |
| `heltec_tracker_v2_companion_radio_wifi` | 1,153,609 | 1,153,633 | +24 | 184,224 | 184,224 | +0 |
| `heltec_tracker_v2_sensor` | 1,231,965 | 1,232,021 | +56 | 72,440 | 72,440 | +0 |
| `heltec_tracker_v2_kiss_modem` | FAIL | FAIL | | | | |

</details>

### Hardware

No hardware evidence. None of the four boards (Heltec V4, V4 R8, T096, Tracker V2) is
in the device pool (`kb device list`, 20.10.2026). The handlers' code is unchanged
apart from the null check, the host tests show the same replies and store writes, and
the builds show every environment of the four boards compiles and links the shared
code with unchanged RAM. If a reviewer wants a board run, the check on any of the
four: `get radio.fem.rxgain`, `set radio.fem.rxgain off`, reboot, `get radio.fem.rxgain`
shows `> off`; the same with `on`; on a board whose FEM cannot switch the LNA both
reply `Error: unsupported`.

## Cut C: one default `radio_init()`

The design (up to "Upstream collisions") was written and committed before any code
of this cut; the results follow from "Host tests" on.

### Inventory of the `radio_init()` bodies

Measured on `e5a8f164` (`integration`, cut B merged) with
`tools/refactoring/variant_duplication.py` (the bodies compared normalized:
comments and white space removed), plus `pio project config` for the environments
of each variant (an environment belongs to a variant when its `build_src_filter`
names `variants/<dir>`). 89 `target.cpp`, 29 different bodies. The groups with
three or more files:

| files | envs | body (normalized) | platform, chip | variants |
|---:|---:|---|---|---|
| 28 | 172 | `rtc_clock.begin(Wire); return radio.std_init(&SPI);` | nRF52, SX1262 | `gat562_30s_mesh_kit`, `gat562_mesh_evb_pro`, `gat562_mesh_tracker_pro`, `gat562_mesh_watch13`, `heltec_mesh_solar`, `heltec_tower_v2`, `ikoka_handheld_nrf`, `ikoka_nano_nrf`, `ikoka_stick_nrf`, `keepteen_lt1`, `lilygo_t_impulse_plus`, `lilygo_techo`, `lilygo_techo_card`, `lilygo_techo_lite`, `meshtiny`, `muziworks_r1_neo`, `nano_g2_ultra`, `promicro`, `rak3401`, `rak4631`, `rak_wismesh_tag`, `sensecap_solar`, `thinknode_m1`, `thinknode_m6`, `thinknode_m8`, `wio-tracker-l1-1w`, `wio-tracker-l1`, `xiao_nrf52` |
| 18 | 168 | `fallback_clock.begin(); rtc_clock.begin(Wire);` then `std_init(&spi)` if `P_LORA_SCLK`, else `std_init()` | ESP32, SX1262/SX1268/SX1276 | `generic-e22`, `heltec_e213`, `heltec_e290`, `heltec_rc32`, `heltec_t190`, `heltec_tracker`, `heltec_tracker_v2`, `heltec_v2`, `heltec_v3`, `heltec_v4`, `heltec_v4_r8`, `heltec_wireless_paper`, `lilygo_tbeam_SX1276`, `lilygo_tlora_c6`, `lilygo_tlora_v2_1`, `meshadventurer`, `rak3112`, `tenstar_c3` |
| 5 | 32 | `fallback_clock.begin(); rtc_clock.begin(Wire); return radio.std_init(&spi);` | ESP32, SX1262 | `lilygo_t3s3`, `lilygo_tbeam_SX1262`, `lilygo_teth_elite`, `nibble_screen_connect`, `nibble_zero_connect` |
| 4 | 29 | as the 18, with `spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI)` before `std_init(&spi)` | ESP32, SX1262/SX1276 | `lilygo_t3s3_sx1276`, `station_g2`, `station_g3_esp32`, `xiao_c3` |
| 3 | 23 | `rtc_clock.begin(Wire);` then `std_init(&SPI)` if `P_LORA_SCLK`, else `std_init()` | nRF52, SX1262 | `heltec_t096`, `heltec_t1`, `heltec_t114` |
| 3 | 26 | as the 4, with `pinMode(21, INPUT); pinMode(48, OUTPUT);` first | ESP32, SX1262 | `thinknode_m2`, `xiao_s3`, `xiao_s3_wio` |

The other 25 bodies occur once or twice; most of them are their own radio setup
(LR11x0, STM32WL, own `begin()` arguments), the rest differ in the SPI object
(`SPI1` on `rak11310` and `rpi_picow`) or leave out the RTC (`mesh_pocket`,
`meshtracker_x1`).

### What cut C shares, and why exactly that

Cut C shares the 28 and the 3 nRF52 bodies (31 files, 195 environments) and
nothing else. The criterion is the one the cut plan set: share only what needs no
new board description.

- **The 28 and the 3 are one function.** The 3 differ from the 28 only in passing
  nothing instead of `&SPI` when `P_LORA_SCLK` is not defined. `std_init()` of
  every chip class that has one (`CustomSX1262`, `CustomSX1268`, `CustomSX1276`,
  `CustomLLCC68`, `CustomLR2021`) reads its `spi` argument only inside
  `#if defined(P_LORA_SCLK)`, so without `P_LORA_SCLK` the argument is unused and
  both calls do the same; with it, both pass `&SPI`. Checked by reading the five
  headers (`grep -n "P_LORA_SCLK\|spi" src/helpers/radiolib/Custom*.h`).
- **Everything the body names is already declared for the shared code.** All 31
  `target.h` declare `extern AutoDiscoverRTCClock rtc_clock;`, `Wire` and `SPI` are
  the Arduino globals, and the radio is reached through `radio_driver` (see
  below). Checked by script over the 31 files: each has exactly the declaration,
  defines `RADIO_CLASS radio = new Module(...)` and `WRAPPER_CLASS
  radio_driver(radio, board);`, uses `VolatileRTCClock fallback_clock` (which
  needs no `begin()`, which is why none of them calls it), and has its
  `radio_init()` outside any `#if`. None uses CRLF.
- **All 195 environments are nRF52 with `CustomSX1262` and `CustomSX1262Wrapper`**
  (from `pio project config`), so one platform and one chip class to verify.
- **The 18, 5, 4 and 3 ESP32 bodies are left for cut D.** They start
  `fallback_clock`, which is an `ESP32RTCClock` there and a file local global no
  `target.h` declares, and most pass `spi`, a file local `SPIClass` of the
  variant. Sharing them needs exactly the new description cut D introduces (which
  fallback clock, which SPI object). `heltec_ct62` (`fallback_clock.begin()` and
  `&SPI`) and the two `SPI1` variants belong to D for the same reason.

### What changes

- `src/helpers/radiolib/RadioLibTarget.cpp` gets a second weak definition, next
  to `radio_new_identity()`:
  ```cpp
  #if defined(NRF52_PLATFORM) && defined(USE_SX1262)
  __attribute__((weak)) bool radio_init() {
    rtc_clock.begin(Wire);
    return RadioLibTargetAccess::radio<RADIO_CLASS>(radio_driver).std_init(&SPI);
  }
  #endif
  ```
- `RadioLibWrapper` names one more friend, a small accessor the shared target code
  uses to reach the radio it wraps, as `RadioNoiseListener` does for cut A:
  `static_cast<RADIO_CLASS&>(*driver._radio)`. The cast is the one every chip
  wrapper already makes (`((CustomSX1262 *)_radio)`), and `radio_driver` wraps
  `radio` in all 31 files (checked above).
- The 31 copies are deleted from `variants/*/target.cpp`, deletions only, as in
  cut A. The declaration stays in every `target.h`.

The guard: `NRF52_PLATFORM` and `USE_SX1262` (which `CustomSX1262Wrapper.h`
defines and every `target.h` of the 31 includes) are existing macros, so no new
description. It is that narrow on purpose:

- The default is only correct where it was the copy. On ESP32 every RadioLib
  variant starts its `fallback_clock`, on RP2040 they pass `SPI1` or `spi`. A new
  ESP32 board that forgets its `radio_init()` should fail to link, not silently get
  an nRF52 default that never starts its clock.
- The weak function is compiled in every environment where the guard holds, also
  in variants that keep their own. Among the nRF52 variants with another chip,
  `meshtracker_x1` (LR2021) declares `rtc_clock` as `VolatileRTCClock`, which has
  no `begin(Wire)`, and the LR11x0 classes have no `std_init()`, so a wider guard
  would not compile there. The two nRF52 SX1262 variants that keep their own
  body (`mesh_pocket`, `muzi_base`) both declare the same `rtc_clock` and have
  `std_init()`, so the default compiles there and is not used.

### Alternatives considered

- **Share the ESP32 groups now as well, with `fallback_clock` and `spi` declared
  `extern` in the shared file.** Rejected: both are file local names of the
  variant that no `target.h` declares, and whether there is a `spi` and which
  clock class it is, is board description. That is cut D.
- **`extern RADIO_CLASS radio;` in the shared file.** Rejected for the reason given
  in cut A: `radio` is not part of any variant's declared interface,
  `radio_driver` is. Also, the weak default is compiled where the variant keeps
  its own, and would then reference `radio` from a function that is not used.
- **A public `getRadio()` on `RadioLibWrapper`.** Rejected for now, as in cut A: it
  widens the wrapper's interface for the shared target code only. With two users
  (cut A's listener and this default) a friend accessor is still local; if cut D
  constructs the radio in the shared code, the accessor goes away again.
- **A virtual `std_init()` on the wrapper.** Rejected: a vtable entry and an indirect
  call in every RadioLib wrapper for a function called once at boot, and the
  LR11x0 and STM32WL wrappers have no such function to override.
- **One default for every platform with `#if` branches per platform inside.**
  Rejected: it would write the ESP32 and RP2040 board description as `#if` chains
  into the shared code, which is what cut D replaces with the build flags.
- **Leave out the 3 (`heltec_t096`, `heltec_t1`, `heltec_t114`).** Not chosen: they
  are the same function (see above), and deleting their copy is the same edit.

### Verification plan

- Host tests in `native_variant_target`, as for cut A. A first commit pins the
  variants' body, copied verbatim into the test, with a recording RTC clock and a
  recording radio: the RTC is started first, with `Wire`; then `std_init()` is
  called once on the radio that `radio_driver` wraps, with `&SPI`; its result is
  returned unchanged (`true` and `false`). The second commit runs the same cases
  against the shared default. `native_variant_target_override` gets a variant
  `radio_init()` of its own and checks it is the one called (does not link if the
  shared one is not weak).
- Mutation tests on the shared default (examples: RTC started after the radio,
  `std_init()` without `&SPI`, result inverted or dropped, not weak, guarded wider
  or narrower).
- Builds from `e5a8f164` and from the code commit, each in its own worktree: one
  environment for each of the 31 edited variants, `mesh_pocket` and `muzi_base`
  (compile the default, keep their own), `meshtracker_x1` and an LR1110 nRF52
  environment (guard false), the 18 environments of the series' matrix (ESP32,
  nRF52, RP2040, STM32), and the two C6 variants without a `target.cpp`. Flash and
  RAM compared per environment; on one environment `nm` shows `radio_init()` weak
  (`W`) and a variant that keeps its own shows it strong (`T`).
- No hardware unless a board from the 31 is free in the pool (`kb device claim`);
  otherwise recorded as without board.

### Upstream collisions

Checked on 20.10.2026 against meshcore-dev/MeshCore: the file lists of all 500
open pull requests (GraphQL, 100 files each; the four with more than 100 files
read through the REST file list), and the diffs of every open pull request that
touches one of the 31 `target.cpp`, checked hunk by hunk for changed lines in or
around `radio_init()`.

- **No open pull request changes a line of the 31 `radio_init()` bodies.**
- **#1962** (keep time across warm resets on nRF52, open, updated 2026-10-12)
  touches 19 of the 31 files, but only the line `VolatileRTCClock fallback_clock;`
  (to `NRF52RTCClock`). No conflict with this cut. It is relevant for cut D: it
  makes the nRF52 fallback clock a board choice as well.
- Context only (a hunk that shows `radio_init()` lines without changing them, so a
  rebase without logic): #1799 (5 files), #1913, #1937, #2185, #2794, #3331,
  #3374, #3527, #3613 (1 file each).
- **#2682** (ThinkNode M4) lists `thinknode_m8` and `wio-tracker-l1-1w` as changed,
  but its head is identical to `upstream/dev` in both (`git diff upstream/dev
  pull/2682/head`, 0 lines), as found for cut B: a merge of `dev`, not a change.
- Open pull requests that add an nRF52 SX1262 board carry their own
  `radio_init()`; with the weak default they keep linking unchanged.

### Host tests

`native_variant_target` builds the real `RadioLibTarget.cpp` against the variant
stand-in, which gets a recording radio (`TestRadio`, a `PhysicalLayer` with
`std_init()`), a recording RTC clock with `begin(TwoWire&)`, and stand-ins for
`Wire.h` and `SPI.h` (only the types and the globals), all in
`test/mocks/radiolib_variant`. The environment sets `RADIO_CLASS=TestRadio` and
`NRF52_PLATFORM`, as the 195 environments do; the stand-in `target.h` defines
`USE_SX1262`, as `CustomSX1262Wrapper.h` does.

The first commit (`da1589cf`) pins the variants' body, copied verbatim into the
test, as a parameterized suite:

- the RTC is started with `Wire`, then `std_init(&SPI)` is called, in that order
  and nothing else,
- `std_init()` is called once,
- its result is returned unchanged, `true` and `false` (on `false` the RTC is still
  started first),
- a second radio that `radio_driver` does not wrap is not touched.

The code commit (`2b7122c6`) runs the same five cases against the shared default,
with the cases unchanged, and `native_variant_target_override` defines a
`radio_init()` of its own and checks it is the one called.

All 17 host environments pass on both commits: 571 test cases on the first, 577 on
the second (566 on `e5a8f164`).

Mutation tests on the shared default, each of which must fail:

| mutation | result |
|---|---|
| M1: start the radio before the RTC | 2 tests fail |
| M2: `std_init()` without `&SPI` | 2 tests fail |
| M3: return `true` whatever `std_init()` returned | 1 test fails |
| M4: do not start the RTC | 2 tests fail |
| M5: call `std_init()` twice | 3 tests fail |
| M6: shared default not weak | `native_variant_target_override` does not link (`duplicate symbol 'radio_init()'`) |
| M7: guard `USE_SX1268` instead of `USE_SX1262` | `native_variant_target` does not link (`radio_init()` undefined) |

### What the code commit changes

As designed above: the weak default and `RadioLibTargetAccess` in
`RadioLibTarget.cpp` (24 lines), one friend line in `RadioLibWrappers.h`, and in
`variants/` deletions only: 31 files, 183 lines (each copy and the blank line before
it), no line added or changed. `tools/refactoring/variant_duplication.py` after the
step: `radio_init()` in 58 files with 27 normalized bodies (was 89 and 29), the
largest group is the 18 ESP32 bodies left for cut D.

### Builds

53 environments were built from `e5a8f164` and from the code commit `2b7122c6`, each
side in its own worktree, with the same toolchain:

- the 18 environments every step of the series is built for (all roles; ESP32,
  nRF52, RP2040, STM32),
- one environment for each of the 31 edited variants (a repeater where there is
  one; `rak4631` and `promicro` are among the 18),
- `Mesh_pocket_repeater` and `muzi_base_uno_repeater` (nRF52, SX1262, own body:
  the default is compiled and not used),
- `MeshTracker_X1_repeater` (nRF52, LR2021) and `t1000e_repeater` (nRF52, LR1110),
  where the guard is false,
- `Xiao_C6_repeater_` and `M5Stack_Unit_C6L_repeater`, the variants without a
  `target.cpp`.

Platforms in the set: 41 nRF52, 8 ESP32, 2 RP2040, 2 STM32WL.

Result:

- 52 build on both commits. `ThinkNode_M8_repeater` fails on both with the same
  link errors (`EnvironmentSensorManager` undefined, known since cut A);
  `radio_init()` is not among the undefined symbols, so it links the shared
  default.
- RAM is unchanged in all 52.
- Flash is unchanged in every environment outside the 31 variants (ESP32, RP2040,
  STM32, the two C6, and the four nRF52 controls).
- In the 31 variants' environments flash grows by 0 to 32 bytes: 30 by +16, 2 by
  +32 (`ProMicro_repeater`, `ProMicro_sensor`), 4 by +0. Measured with `nm`:
  `radio_init()` is 16 bytes larger than the variant's own (0xe8 to 0xf8 on
  `RAK_4631_repeater`, 0xf4 to 0x104 on `ProMicro_repeater` and on the +0 ones),
  because the radio is now loaded from `radio_driver` and cast, instead of being a
  fixed address; on the +0 environments `.text` keeps its size, the 16 bytes fall
  into alignment padding. `radio_init()` is a weak symbol (`W`) in the new firmware
  where it was a normal one (`T`).
- `Mesh_pocket_repeater` keeps its own definition: same size, and `nm` shows its
  `radio_init()` as `T` with the same size (0xd8) and address as before. The
  precedence the override test checks on the host, on the nRF52 toolchain.

<details>
<summary>All 53 environments</summary>

| Environment | Platform | Flash before | Flash after | Flash delta | RAM before | RAM after | RAM delta |
|---|---|---:|---:|---:|---:|---:|---:|
| `Heltec_v3_repeater` | ESP32 | 1,174,765 | 1,174,765 | +0 | 62,512 | 62,512 | +0 |
| `Heltec_v3_room_server` | ESP32 | 1,169,913 | 1,169,913 | +0 | 66,200 | 66,200 | +0 |
| `Heltec_v3_companion_radio_ble` | ESP32 | 1,669,989 | 1,669,989 | +0 | 194,844 | 194,844 | +0 |
| `Heltec_v3_companion_radio_wifi` | ESP32 | 1,106,129 | 1,106,129 | +0 | 172,384 | 172,384 | +0 |
| `Heltec_v3_sensor` | ESP32 | 1,181,293 | 1,181,293 | +0 | 60,600 | 60,600 | +0 |
| `RAK_4631_repeater` | nRF52 | 441,768 | 441,784 | +16 | 37,240 | 37,240 | +0 |
| `RAK_4631_room_server` | nRF52 | 434,920 | 434,936 | +16 | 40,928 | 40,928 | +0 |
| `RAK_4631_companion_radio_ble` | nRF52 | 464,036 | 464,052 | +16 | 149,768 | 149,768 | +0 |
| `RAK_4631_sensor` | nRF52 | 438,616 | 438,632 | +16 | 35,328 | 35,328 | +0 |
| `PicoW_repeater` | RP2040 | 492,668 | 492,668 | +0 | 85,368 | 85,368 | +0 |
| `wio-e5-mini_repeater` | STM32 | 204,136 | 204,136 | +0 | 17,172 | 17,172 | +0 |
| `wio-e5-mini_sensor` | STM32 | 203,664 | 203,664 | +0 | 15,276 | 15,276 | +0 |
| `RAK_3112_repeater_bridge_espnow` | ESP32 | 1,176,065 | 1,176,065 | +0 | 64,072 | 64,072 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | RP2040 | 241,080 | 241,080 | +0 | 26,704 | 26,704 | +0 |
| `ProMicro_repeater` | nRF52 | 346,208 | 346,240 | +32 | 33,892 | 33,892 | +0 |
| `ProMicro_room_server` | nRF52 | 339,272 | 339,288 | +16 | 37,588 | 37,588 | +0 |
| `ProMicro_companion_radio_usb` | nRF52 | 358,820 | 358,836 | +16 | 99,468 | 99,468 | +0 |
| `ProMicro_sensor` | nRF52 | 342,924 | 342,956 | +32 | 31,988 | 31,988 | +0 |
| `GAT562_30S_Mesh_Kit_repeater` | nRF52 | 411,392 | 411,408 | +16 | 35,932 | 35,932 | +0 |
| `GAT562_Mesh_EVB_Pro_repeater` | nRF52 | 365,588 | 365,604 | +16 | 35,124 | 35,124 | +0 |
| `GAT562_Mesh_Tracker_Pro_repeater` | nRF52 | 381,100 | 381,116 | +16 | 35,492 | 35,492 | +0 |
| `GAT562_Mesh_Watch13_companion_radio_ble` | nRF52 | 399,432 | 399,448 | +16 | 147,888 | 147,888 | +0 |
| `Heltec_mesh_solar_repeater` | nRF52 | 365,584 | 365,600 | +16 | 34,284 | 34,284 | +0 |
| `Heltec_tower_v2_repeater` | nRF52 | 309,512 | 309,528 | +16 | 33,292 | 33,292 | +0 |
| `ikoka_handheld_nrf_e22_30dbm_repeater` | nRF52 | 358,136 | 358,136 | +0 | 34,468 | 34,468 | +0 |
| `ikoka_nano_nrf_22dbm_repeater` | nRF52 | 361,016 | 361,016 | +0 | 34,556 | 34,556 | +0 |
| `ikoka_stick_nrf_22dbm_repeater` | nRF52 | 373,608 | 373,608 | +0 | 34,708 | 34,708 | +0 |
| `KeepteenLT1_repeater` | nRF52 | 322,012 | 322,028 | +16 | 33,436 | 33,436 | +0 |
| `LilyGo_T_Impulse_Plus_repeater` | nRF52 | 366,332 | 366,348 | +16 | 35,236 | 35,236 | +0 |
| `LilyGo_T-Echo_repeater` | nRF52 | 350,080 | 350,096 | +16 | 38,484 | 38,484 | +0 |
| `LilyGo_T-Echo_Card_repeater` | nRF52 | 322,352 | 322,368 | +16 | 34,004 | 34,004 | +0 |
| `LilyGo_T-Echo-Lite_repeater` | nRF52 | 335,756 | 335,772 | +16 | 37,716 | 37,716 | +0 |
| `Meshtiny_companion_radio_usb` | nRF52 | 330,136 | 330,152 | +16 | 141,364 | 141,364 | +0 |
| `R1Neo_repeater` | nRF52 | 368,740 | 368,756 | +16 | 34,892 | 34,892 | +0 |
| `Nano_G2_Ultra_repeater` | nRF52 | 319,484 | 319,500 | +16 | 33,332 | 33,332 | +0 |
| `RAK_3401_repeater` | nRF52 | 410,864 | 410,880 | +16 | 35,636 | 35,636 | +0 |
| `RAK_WisMesh_Tag_repeater` | nRF52 | 368,900 | 368,916 | +16 | 34,916 | 34,916 | +0 |
| `SenseCap_Solar_repeater` | nRF52 | 365,932 | 365,948 | +16 | 34,876 | 34,876 | +0 |
| `ThinkNode_M1_repeater` | nRF52 | 306,164 | 306,180 | +16 | 32,980 | 32,980 | +0 |
| `ThinkNode_M6_repeater` | nRF52 | 366,036 | 366,052 | +16 | 34,932 | 34,932 | +0 |
| `ThinkNode_M8_repeater` | nRF52 | FAIL | FAIL | | | | |
| `WioTrackerL1-1W_repeater` | nRF52 | 378,556 | 378,556 | +0 | 35,436 | 35,436 | +0 |
| `WioTrackerL1_repeater` | nRF52 | 377,548 | 377,564 | +16 | 35,428 | 35,428 | +0 |
| `Xiao_nrf52_repeater` | nRF52 | 361,624 | 361,640 | +16 | 34,596 | 34,596 | +0 |
| `Heltec_t096_repeater` | nRF52 | 414,288 | 414,304 | +16 | 35,564 | 35,564 | +0 |
| `Heltec_t1_repeater` | nRF52 | 413,332 | 413,348 | +16 | 35,204 | 35,204 | +0 |
| `Heltec_t114_without_display_repeater` | nRF52 | 366,084 | 366,100 | +16 | 35,140 | 35,140 | +0 |
| `Mesh_pocket_repeater` | nRF52 | 326,840 | 326,840 | +0 | 36,820 | 36,820 | +0 |
| `muzi_base_uno_repeater` | nRF52 | 301,236 | 301,236 | +0 | 32,932 | 32,932 | +0 |
| `MeshTracker_X1_repeater` | nRF52 | 301,152 | 301,152 | +0 | 32,956 | 32,956 | +0 |
| `t1000e_repeater` | nRF52 | 294,712 | 294,712 | +0 | 32,876 | 32,876 | +0 |
| `Xiao_C6_repeater_` | ESP32 | 542,481 | 542,481 | +0 | 29,856 | 29,856 | +0 |
| `M5Stack_Unit_C6L_repeater` | ESP32 | 552,823 | 552,823 | +0 | 29,752 | 29,752 | +0 |

</details>

### Hardware

No hardware evidence (without board). Of the boards in the device pool
(`kb device list`, 20.10.2026), four are of the 31 variants and free: two are the
FUOTA proof of concept boards (`faketec-6e71`, ProMicro, and `p1-4134`, SenseCAP
Solar) with their own flash layout, one is a running sensor node with its own
build (`techo-b43b`, T-Echo), and the T114 is the companion of a test phone.
Flashing a plain repeater onto any of them would destroy what they are kept for,
so none was used. The change moves a function whose body is unchanged into another
translation unit, and the builds show every edited variant links the shared
definition. If a reviewer wants a board run, the check on any of the 31: flash the
repeater firmware, see it boot past `radio_init()` (no `Radio init failed`), and
`get radio` answers with the stored settings.

## Cut D: the radio, wrapper and RTC globals from the board description

The design below was written and committed before any code of this cut.

### Inventory of the globals

Measured on `e0d05fde` (`integration`, cuts A to C, 9b and 10c merged) with
`tools/refactoring/variant_target_globals.py` (in this cut). For every
`target.cpp` it takes the "radio block": the declarations of the SPI object,
`radio`, `radio_driver`, `fallback_clock` and `rtc_clock` and the preprocessor
lines around them, compares the blocks normalized and groups them, with the
environments and platforms of each group from `pio project config`. 89
`target.cpp`, 606 firmware environments build one of them.

| variants | envs | block (normalized, `PINS` = `P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY`) | platform |
|---:|---:|---|---|
| 30 | 185 | `radio(new Module(PINS, SPI))`, Volatile fallback, AutoDiscover | nRF52 |
| 20 | 175 | `#if P_LORA_SCLK`: `static SPIClass spi;` and `Module(PINS, spi)`, else `Module(PINS)`; ESP32 fallback, AutoDiscover | ESP32 |
| 8 | 61 | as the 20, without the `#if` (`P_LORA_SCLK` is defined in all their environments) | ESP32 |
| 4 | 34 | as the 20, with `spi(FSPI)` | ESP32 |
| 4 | 20 | `Module(PINS, SPI)`, `VolatileRTCClock rtc_clock` without fallback (LR11x0, LR2021) | nRF52 |
| 3 | 23 | `#if P_LORA_SCLK`: `Module(PINS, SPI)`, else `Module(PINS)`; Volatile fallback | nRF52 |
| 3 | 20 | as the 20, with the SX1276 pins `P_LORA_DIO_0, ..., P_LORA_DIO_1` | ESP32 |
| 3 | 20 | `Module(PINS, SPI1)`, Volatile fallback | RP2040 |
| 4 | 16 | `STM32WLx_Module`, RF switch tables in between, `VolatileRTCClock rtc_clock` | STM32 |
| 2 | 5 | `ESP32RTCClock rtc_clock` only (ESP-NOW, no LoRa radio) | ESP32 |
| 8 | 47 | one each: `spi(HSPI)`, `spi(0)`, SX1276 pins without `#if`, `Module(PINS)` on ESP32 with `P_LORA_SCLK` (`heltec_ct62`), `Module(PINS, SPI)` on ESP32 (`thinknode_m9`), `Module(PINS)` on RP2040, and two nRF52 blocks with sensors or a display declared in between (`mesh_pocket`, `muziworks_r1_neo`) | mixed |

So the block is not one text but a few rules, and almost all of it follows from
macros the variants already have:

- **Pins**: SX1276 (`USE_SX1276`, defined by `CustomSX1276Wrapper.h`) takes
  `P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1` after `P_LORA_NSS`, every other chip
  `P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY`.
- **Fallback clock**: `ESP32RTCClock` on ESP32, `VolatileRTCClock` elsewhere, and
  `AutoDiscoverRTCClock rtc_clock(fallback_clock)` on top.
- **SPI**: nRF52 passes `SPI`; ESP32 has its own `static SPIClass spi` exactly when
  `P_LORA_SCLK` is defined and passes nothing otherwise. The three nRF52 blocks with
  `#if P_LORA_SCLK` are the `SPI` rule: `P_LORA_SCLK` is defined in all 23 of their
  environments (20 by build flag, `heltec_t1` in its `variant.h`).
- **Not derivable**: the SPI host of the ESP32 bus (`FSPI` in 4, `0` in
  `lilygo_tlora_c6`; `HSPI` in `lilygo_teth_elite` is the `SPIClass` default).

"`Module(PINS)`" and "`Module(PINS, SPI)`" are not the same thing, even where
RadioLib's default bus is `SPI`: the first makes RadioLib call `SPI.begin()` itself
(`ArduinoHal()` sets `initInterface`), the second leaves it to the caller
(`ArduinoHal(spi, settings)`). The rules keep exactly the constructor each variant
calls.

### Which variants can take their globals from shared code

A variant can drop its block only if nothing else in its `target.cpp` names
`radio`, `spi` or `fallback_clock`, since those then live in another translation
unit (the script checks this, after removing a `radio_init()` body that a shared
default can take over):

- **31 nRF52 variants, 195 environments**: exactly the 31 of cut C, whose
  `radio_init()` is already the shared one. Their blocks are the "30" and "3"
  groups above minus `muzi_base`, `thinknode_m3`, `thinknode_m4` (own
  `radio_init()`), plus `muziworks_r1_neo`.
- **22 ESP32 variants, 195 environments**: the 18 and 5 `radio_init()` groups that
  cut C left for this cut, minus `lilygo_teth_elite` (its `target.cpp` has four
  more functions on `radio`). Their two `radio_init()` bodies are one function:
  the 5 call `std_init(&spi)` without the `#if`, and `P_LORA_SCLK` is defined in
  all their environments, so with the `#if` they take the same branch.
- **34 stay as they are**: their own code names the globals (LR11x0 and STM32WL
  setup, RP2040 on `SPI1`, the ESP32 bodies with `spi.begin()`, `pinMode()` or
  extra calls, `mesh_pocket` and the nRF52 LR variants, `heltec_ct62`,
  `thinknode_m9`). Sharing their globals would mean adding `extern` declarations
  of `radio` to their `target.cpp`, which is not "deletions only"; that is left to
  a later cut, or to a board that is touched anyway.

### Why the globals cannot be weak

Cuts A and C shared functions with weak definitions, so a variant that keeps its
own still links. Objects with a constructor cannot be shared that way. A weak
object is still constructed by the initializer of its translation unit, and that
initializer writes to the symbol the linker resolved, which is the variant's
object. Measured: a host program with a weak `Obj radio_driver(1)` in one file and
a strong `Obj radio_driver(2)` in another runs the constructor twice, at the same
address (`Obj ctor #1 (1) at 0x100db000c`, `Obj ctor #2 (2) at 0x100db000c`). With
`arm-none-eabi-g++` the weak file's initializer is guarded only by a guard variable
of its own (`_ZGV12radio_driver`), which the strong file never sets, so the same
happens on the firmware toolchain. For `radio` that would mean a second
`new Module`, for `spi` a second mutex, in every variant that keeps its block.

So the variant has to say that it takes the shared globals. That is the one new
description macro every variant of this cut gets.

### What changes

- `RADIOLIB_TARGET_GLOBALS` (new), defined in the variant's `target.h`: the
  variant takes `radio`, `radio_driver`, `fallback_clock`, `rtc_clock` and, on
  ESP32 with `P_LORA_SCLK`, `spi` from `RadioLibTarget.cpp`. In `target.h` because
  both translation units read it (the variant's and the shared one).
- `P_LORA_SPI_HOST` (new, optional): the host of the ESP32 radio's own SPI bus,
  for the 5 variants that do not use the `SPIClass` default.
- `RadioLibTarget.cpp` defines, under `WRAPPER_CLASS` and `RADIOLIB_TARGET_GLOBALS`,
  in the order the variants have them:
  ```cpp
  #if defined(ESP32) && defined(P_LORA_SCLK)
    static SPIClass spi;                      // or spi(P_LORA_SPI_HOST)
    RADIO_CLASS radio = new Module(PINS, spi);
  #elif defined(NRF52_PLATFORM)
    RADIO_CLASS radio = new Module(PINS, SPI);
  #else
    RADIO_CLASS radio = new Module(PINS);
  #endif
  WRAPPER_CLASS radio_driver(radio, board);
  FALLBACK_CLOCK fallback_clock;              // ESP32RTCClock or VolatileRTCClock
  AutoDiscoverRTCClock rtc_clock(fallback_clock);
  ```
  `radio` and `spi` keep internal linkage (`static`), as `spi` had: no new name
  becomes visible to the rest of the firmware. `fallback_clock` keeps the external
  linkage it has today, because `UnitC6LBoard.cpp` and `XiaoC6Board.cpp` define one
  of their own and are not touched (they have no `target.h` with the macro).
- For ESP32, a weak default `radio_init()` next to cut C's, under `ESP32` and
  `RADIOLIB_TARGET_GLOBALS`: `fallback_clock.begin(); rtc_clock.begin(Wire);` then
  `std_init(&spi)` with `P_LORA_SCLK`, else `std_init()`. It uses the shared `radio`
  directly, no accessor.
- In each variant: `target.cpp` loses its block (and on ESP32 its `radio_init()`),
  deletions only; `target.h` gains the macro line (and `P_LORA_SPI_HOST` where
  needed), additions only.

Initialization order: the five objects are now constructed by the shared file's
initializer instead of the variant's, so their order relative to the variant's
other globals (`board`, `sensors`, `display`, `user_btn`) is the link order instead
of the source order. Among the five the order stays the same. None of the
constructors involved reads another global: `RadioLibWrapper` and
`AutoDiscoverRTCClock` store references, `new Module` allocates, `SPIClass`
allocates a mutex, `VolatileRTCClock` reads `millis()`, which is 0 before the
scheduler starts on nRF52 (`tick2ms(xTaskGetTickCount())`), and the variant's
objects that use the globals take their address (`MicroNMEALocationProvider(Serial1,
&rtc_clock)`). What can change is the order of heap allocations at start-up, so
heap addresses, not anything a user, an app or the mesh sees. The pull requests
show this by reading the constructors of every global the 53 `target.cpp` keep,
and by comparing images (see "Verification plan").

The constructors of the moved globals, read in the sources the builds use
(RadioLib at `6d893483`, the libraries PlatformIO installed for the environments;
arduino-esp32 3.20017 for ESP32, 3.1.3 and 3.3.9 for the C6 builds): none of them
reads another global object. They store their arguments, allocate, or read a clock.

| global | constructor chain | what it does | source |
|---|---|---|---|
| `radio` (`new Module(...)`, 4 pins) | `Module(cs, irq, rst, gpio)` | stores the pins, `new ArduinoHal()`, which stores the address of RadioLib's default bus (`spi(&RADIOLIB_DEFAULT_SPI)`) and constants | RadioLib `src/Module.cpp:10`, `src/hal/Arduino/ArduinoHal.cpp:5`, `src/Hal.cpp:5` |
| `radio` (`new Module(..., SPI)` or `spi`) | `Module(cs, irq, rst, gpio, SPIClass&, SPISettings)` | stores the pins, `new ArduinoHal(spi, settings)`, which stores the bus's address and the settings | `src/Module.cpp:14`, `src/hal/Arduino/ArduinoHal.cpp:7` |
| `radio` (SX1262, SX1268) | `CustomSX126x(Module*)`, `SX1262`/`SX1268(Module*)`, `SX126x(Module*)`, `PhysicalLayer()` | sets members (chip type, step size, `mod`) | `src/helpers/radiolib/CustomSX126x.h:16`, RadioLib `src/modules/SX126x/SX1262.cpp:44`, `SX1268.cpp:44`, `SX126x.cpp:6`, `src/protocols/PhysicalLayer/PhysicalLayer.cpp:5` |
| `radio` (SX1276) | `CustomSX1276(Module*)`, `SX1276`, `SX1278`, `SX127x(Module*)`, `PhysicalLayer()` | sets members | `src/helpers/radiolib/CustomSX1276.h:13`, RadioLib `src/modules/SX127x/SX1276.cpp:4`, `SX1278.cpp:5`, `SX127x.cpp:5` |
| `radio_driver` | `CustomSX1262Wrapper`, `CustomSX1268Wrapper`, `CustomSX1276Wrapper(radio, board)`, `RadioLibWrapper(PhysicalLayer&, MainBoard&)` | stores the addresses of `radio` and `board`, zeroes counters | `src/helpers/radiolib/CustomSX1262Wrapper.h:13`, `CustomSX1268Wrapper.h:13`, `CustomSX1276Wrapper.h:12`, `RadioLibWrappers.h:34` |
| `fallback_clock` (nRF52) | `VolatileRTCClock()` | sets a fixed base time and reads `millis()`, which is `tick2ms(xTaskGetTickCount())` and 0 before the scheduler starts, in either order | `src/helpers/ArduinoHelpers.h:11`, Adafruit nRF52 core `cores/nRF5/delay.c:28` |
| `fallback_clock` (ESP32) | `ESP32RTCClock()` | nothing | `src/helpers/ESP32Board.h:210` |
| `rtc_clock` | `AutoDiscoverRTCClock(RTCClock&)` | stores the address of `fallback_clock` | `src/helpers/AutoDiscoverRTCClock.h:12` |
| `spi` (ESP32) | `SPIClass(uint8_t spi_bus)` | sets members, creates its parameter mutex (`xSemaphoreCreateMutex()`), which allocates | arduino-esp32 `libraries/SPI/src/SPI.cpp:34` (3.20017), `:38` (3.1.3, 3.3.9) |

What the order can change is therefore the order of the heap allocations
(`new Module`, `new ArduinoHal`, the mutex) relative to the variant's other
globals, so heap addresses. The builds bound the image differences to that: see
"D1: builds" and "D2: builds" (`nm` and `objdump`, `SOURCE_DATE_EPOCH` fixed, same
worktree).

### Two pull requests

- **D1, nRF52**: the globals of the 31 variants of cut C. No new `radio_init()`
  (cut C's stays as it is), no `P_LORA_SPI_HOST`. 31 `target.cpp` (deletions), 31
  `target.h` (one line each), the shared file.
- **D2, ESP32**: the globals and the weak `radio_init()` of the 22 variants,
  `P_LORA_SPI_HOST` for 5 of them. Stacked on D1. Globals and `radio_init()` go
  together because the body names `spi`, which must be in the same translation
  unit.

Each is one concern per platform, with one platform's toolchain and boards to
verify, and each is about the size of cut C.

### Alternatives considered

- **Weak globals, like cuts A and C.** Rejected: constructed twice where a variant
  keeps its own (measured above).
- **A header the variant includes in place of its block**
  (`#include <helpers/radiolib/RadioLibTargetGlobals.h>` in `target.cpp`). Keeps
  the translation unit and the initialization order, so the variant's object would
  stay the same. Rejected: the shared `radio_init()` of D2 lives in another
  translation unit and needs `spi`, which would stay file local in the variant,
  and `RadioLibTarget.cpp` cannot see whether a variant included the header, so a
  macro in `target.h` would be needed anyway. Two lines per variant instead of one,
  and the block is still written out in every variant's source.
- **Give `radio` external linkage and declare it in a shared header**, so that the
  34 variants with own code could take the shared globals as well. Rejected: it
  makes `radio` part of the interface every role sees, which cut A deliberately
  avoided, and it adds lines to 34 `target.cpp`.
- **Opt-in by build flag in `platformio.ini` instead of `target.h`.** Not chosen:
  equally one line, but a variant's environments are spread over several sections
  in some `platformio.ini`, and `target.h` is the one file per variant both
  translation units read.
- **A constexpr struct per board, or a generator** (options 2 and 3). Rejected by
  the decision above.

### Verification plan

- Host tests in `native_variant_target`: a first commit pins the variants' block
  and ESP32 body (copied verbatim into a test variant) with recording stand-ins:
  which `Module` constructor was called with which pins and which bus, that
  `radio_driver` wraps `radio` and `board`, that `rtc_clock` falls back to
  `fallback_clock`, and for ESP32 the order clock, RTC, `std_init(&spi)` or
  `std_init()`. The code commit runs the same cases against the shared definitions,
  in a native environment per rule (nRF52; ESP32 with and without `P_LORA_SCLK`,
  with `P_LORA_SPI_HOST`; SX1276 pins). Mutation tests on the shared code (wrong
  pin order, wrong bus, `SPI` passed where nothing was, wrong clock class, ESP32
  default not weak, guard without the opt-in).
- Builds from the start commit and from the code commit, in the same worktree: one
  environment per edited variant, the controls of each platform (variants that
  keep their block, with and without own `radio_init()`), the series' matrix of 18,
  the two C6 variants without `target.cpp`. Flash and RAM per environment, `nm` for
  the symbols that moved (`radio`, `spi` now local symbols of `RadioLibTarget.cpp`,
  `radio_init()` weak), and an image comparison of a sample built with
  `SOURCE_DATE_EPOCH` set (RadioLib writes `__DATE__`/`__TIME__` into the image
  otherwise), to show that what differs is the initializer and the placement.
- All 17 host test environments green on both commits.
- Hardware only with a board from `kb device claim`, otherwise recorded as without
  board.

### Upstream collisions

Checked on 21.10.2026 against meshcore-dev/MeshCore: the file lists of all 499
open pull requests (100 files each by GraphQL, the 4 with more through the REST
file list), and for every pull request that touches one of the 53 `target.cpp` or
`target.h`, the changed lines of its diff matched against the block and
`radio_init()`.

- **#1962** (nRF52 time across warm resets, updated 2026-10-12) changes
  `VolatileRTCClock fallback_clock;` to `NRF52RTCClock` in 19 of the 31 D1 files.
  D1 deletes that line there, so whichever lands second has to follow: after D1,
  #1962 changes one line of the shared code (the nRF52 fallback class) instead of
  19 files, and the 12 variants it did not touch get the same clock unless the
  shared rule keeps them apart (which would then be a new description macro,
  decided in #1962's rebase, not here).
- **#3213** (ESP32-C3 SPI host, updated 2026-08-14) changes `static SPIClass spi;`
  to `spi(FSPI)` in `tenstar_c3`. After D2 that is `P_LORA_SPI_HOST=FSPI` in its
  `target.h`.
- **#3638** (dual radio profiles, updated 2026-10-20) constructs `heltec_v4`'s
  radio with its own HAL. After D2, `heltec_v4` would have to go back to its own
  block (drop the macro), which still links.
- **#2682** lists `thinknode_m8` and `wio-tracker-l1-1w` with the whole block as
  added lines; for cuts B and C its head was identical to `upstream/dev` in both
  files (a merge, not a change). Not checked again for this cut.
- Every other open pull request on these files changes other lines (GPS, sensors,
  display; #1799, #1913, #1924, #1937, #1972, #2058, #2185, #2679, #2794, #2837,
  #2882, #2927, #3051, #3141, #3222, #3289, #3307, #3331, #3346, #3374, #3527,
  #3613, #3651). Those that change `target.cpp` lines next to the block conflict by
  context at most; those on `target.h` add lines elsewhere in the file.
- New board pull requests carry their own block and keep linking: the shared
  globals exist only where a `target.h` asks for them.

### D1: host tests

`native_variant_target_globals` (new) builds `RadioLibTarget.cpp` against the
target.h of an nRF52 variant (`test/mocks/radiolib_variant_globals`) with the real
`AutoDiscoverRTCClock` (its out-of-line members, which need the RTC chip libraries,
stood in by the test) and a `Module` stand-in that keeps its pins and tells its two
constructors apart. The variant stand-ins move into
`test/mocks/radiolib_variant/variant_stubs.h`.

The first commit (`6510e4da`) has the block of the 31 variants twice, copied
verbatim: once at file scope, where a variant's `target.cpp` has it, and once in a
namespace as the reference. Six cases run on both:

- the radio is built from a `Module`,
- with `P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY` in that order,
- with `SPI` passed (the constructor that leaves starting the bus to `std_init()`),
- `radio_driver` wraps the board,
- `rtc_clock` reads and sets the time through `fallback_clock`,
- `rtc_clock.tick()` advances `fallback_clock` by `millis()`, which is what
  `VolatileRTCClock` does.

and one more: cut C's shared `radio_init()` starts the RTC, then the radio that
`radio_driver` wraps, on `SPI`, once.

The code commit deletes the file-scope block from the test, as from the variants,
and defines the macro in the stand-in `target.h`; the cases are unchanged and now
run against the shared globals. All 18 host environments pass on both commits: 629
test cases each (616 on `e0d05fde`).

Mutations of the shared globals, each of which must fail:

| mutation | result |
|---|---|
| M1: `P_LORA_BUSY` and `P_LORA_DIO_1` swapped | 1 test fails |
| M2: no `SPI` argument (RadioLib starts the bus) | 1 test fails |
| M3: `rtc_clock` falls back to another clock | 2 tests fail |
| M4: defined without the opt-in | `native_variant_target` does not compile (pins and `board` undeclared, `rtc_clock` redefined) |
| M5: opt-in misspelt | `native_variant_target_globals` does not link (globals undefined) |
| M6: `radio_driver` built on another board | 1 test fails |

### D1: what the code commit changes

`a8dc76a6`: the globals in `RadioLibTarget.cpp` (with an `#error` for a platform
or chip whose description is not written yet), and in `variants/`: 31 `target.cpp`,
deletions only (215 lines, the block with the blank line after it, and the
`#if defined(P_LORA_SCLK)` around the radio in `heltec_t096`, `heltec_t1` and
`heltec_t114`); 31 `target.h`, one line each, additions only. No file uses CRLF.
`tools/refactoring/variant_target_globals.py` afterwards lists no nRF52 radio block
that could still be shared.

### D1: builds

The 53 environments of cut C were built from `6510e4da` (the test commit, firmware
sources as on `e0d05fde`) and from the code commit `a8dc76a6`, both in the same
worktree, with `SOURCE_DATE_EPOCH=1760000000` so that `__DATE__` and `__TIME__` are
the same on both sides.

- 52 build on both. `ThinkNode_M8_repeater` fails on both, as since cut A.
- **Every environment outside the 31 variants has the same image.** The nRF52,
  RP2040 and STM32 controls (`Mesh_pocket_repeater`, `muzi_base_uno_repeater`,
  `MeshTracker_X1_repeater`, `t1000e_repeater`, `PicoW_repeater`,
  `waveshare_rp2040_lora_repeater_bridge_rs232`, the two `wio-e5-mini`) are byte
  for byte identical. The 8 ESP32 ones (the five `Heltec_v3`, `RAK_3112`, the two
  C6) differ only in the 65 bytes of the image that hash the ELF file
  (`esp_app_desc_t.app_elf_sha256` and the checksum and SHA-256 appended to the
  image), which change with any byte of the ELF, debug information included
  (`RadioLibTarget.cpp` has new lines); checked by comparing the images byte by byte
  outside those fields.
- In the 36 environments of the 31 variants, RAM changes by -8 to +8 bytes and
  flash grows by 4 to 84 bytes. Measured with `nm` (all 36, symbols compared as
  name, size and binding):
  - `radio` becomes a local symbol (`b` instead of `B`), same size;
    `radio_driver`, `fallback_clock` and `rtc_clock` keep name, size and binding.
  - The variant's static initializer (`_GLOBAL__sub_I_board`; in `xiao_nrf52`,
    whose first global is the display, `_GLOBAL__sub_I_display`) loses 0xb8 to
    0xe4 bytes and `RadioLibTarget.cpp` gets one of 0xd8 or 0xdc bytes
    (`_GLOBAL__sub_I_radio_driver`): the construction moved, as designed.
  - `MicroNMEALocationProvider::loop()` (30 environments, an inline function
    compiled in `target.cpp`), and `NanoG2UltraSensorManager::loop()`,
    `ThinkNodeM1SensorManager::loop()` and `SolarSensorManager::loop()` (one each)
    grow by 28 bytes: the compiler now devirtualizes the call to the clock's
    `setCurrentTime()` speculatively, it compares the function pointer with
    `VolatileRTCClock::setCurrentTime` and runs that function inlined, otherwise
    makes the indirect call as before (`objdump`: the address of
    `VolatileRTCClock::setCurrentTime` is in the functions' literal pool after the
    change, not before). Same function called with the same arguments either way.
  - No other symbol changes. The RAM differences of 8 bytes are alignment: every
    data and bss symbol keeps its size, `radio` now sits in another object file's
    `.bss`.
- No hardware evidence (without board), for the reasons given in cut C; the pool
  boards of these variants are kept for other purposes. A board run would check
  that the radio starts (`Radio init failed` absent) and `get radio` answers.

<details>
<summary>All 53 environments</summary>

| Environment | Flash before | Flash after | Flash delta | RAM before | RAM after | RAM delta | image |
|---|---:|---:|---:|---:|---:|---:|---|
| `GAT562_30S_Mesh_Kit_repeater` | 411,568 | 411,604 | +36 | 35,932 | 35,932 | +0 | differs (hex) |
| `GAT562_Mesh_EVB_Pro_repeater` | 365,764 | 365,816 | +52 | 35,124 | 35,124 | +0 | differs (hex) |
| `GAT562_Mesh_Tracker_Pro_repeater` | 381,276 | 381,328 | +52 | 35,492 | 35,492 | +0 | differs (hex) |
| `GAT562_Mesh_Watch13_companion_radio_ble` | 400,552 | 400,572 | +20 | 147,888 | 147,888 | +0 | differs (hex) |
| `Heltec_mesh_solar_repeater` | 365,600 | 365,668 | +68 | 34,284 | 34,292 | +8 | differs (hex) |
| `Heltec_t096_repeater` | 414,464 | 414,532 | +68 | 35,564 | 35,572 | +8 | differs (hex) |
| `Heltec_t114_without_display_repeater` | 366,260 | 366,312 | +52 | 35,140 | 35,140 | +0 | differs (hex) |
| `Heltec_t1_repeater` | 413,508 | 413,560 | +52 | 35,204 | 35,204 | +0 | differs (hex) |
| `Heltec_tower_v2_repeater` | 309,528 | 309,596 | +68 | 33,292 | 33,292 | +0 | differs (hex) |
| `Heltec_v3_companion_radio_ble` | 1,671,657 | 1,671,657 | +0 | 194,844 | 194,844 | +0 | differs (bin) |
| `Heltec_v3_companion_radio_wifi` | 1,107,825 | 1,107,825 | +0 | 172,384 | 172,384 | +0 | differs (bin) |
| `Heltec_v3_repeater` | 1,175,025 | 1,175,025 | +0 | 62,512 | 62,512 | +0 | differs (bin) |
| `Heltec_v3_room_server` | 1,170,173 | 1,170,173 | +0 | 66,200 | 66,200 | +0 | differs (bin) |
| `Heltec_v3_sensor` | 1,181,561 | 1,181,561 | +0 | 60,600 | 60,600 | +0 | differs (bin) |
| `KeepteenLT1_repeater` | 322,028 | 322,096 | +68 | 33,436 | 33,436 | +0 | differs (hex) |
| `LilyGo_T-Echo-Lite_repeater` | 335,772 | 335,792 | +20 | 37,716 | 37,716 | +0 | differs (hex) |
| `LilyGo_T-Echo_Card_repeater` | 322,368 | 322,404 | +36 | 34,004 | 34,012 | +8 | differs (hex) |
| `LilyGo_T-Echo_repeater` | 350,096 | 350,100 | +4 | 38,484 | 38,492 | +8 | differs (hex) |
| `LilyGo_T_Impulse_Plus_repeater` | 366,508 | 366,560 | +52 | 35,236 | 35,236 | +0 | differs (hex) |
| `M5Stack_Unit_C6L_repeater` | 552,821 | 552,821 | +0 | 29,752 | 29,752 | +0 | differs (bin) |
| `MeshTracker_X1_repeater` | 301,152 | 301,152 | +0 | 32,956 | 32,956 | +0 | identical |
| `Mesh_pocket_repeater` | 326,840 | 326,840 | +0 | 36,820 | 36,820 | +0 | identical |
| `Meshtiny_companion_radio_usb` | 331,112 | 331,132 | +20 | 141,364 | 141,372 | +8 | differs (hex) |
| `Nano_G2_Ultra_repeater` | 319,500 | 319,568 | +68 | 33,332 | 33,332 | +0 | differs (hex) |
| `PicoW_repeater` | 492,668 | 492,668 | +0 | 85,368 | 85,368 | +0 | identical |
| `ProMicro_companion_radio_usb` | 359,796 | 359,816 | +20 | 99,468 | 99,468 | +0 | differs (hex) |
| `ProMicro_repeater` | 346,240 | 346,276 | +36 | 33,892 | 33,900 | +8 | differs (hex) |
| `ProMicro_room_server` | 339,288 | 339,340 | +52 | 37,588 | 37,596 | +8 | differs (hex) |
| `ProMicro_sensor` | 342,956 | 343,008 | +52 | 31,988 | 31,996 | +8 | differs (hex) |
| `R1Neo_repeater` | 368,916 | 368,984 | +68 | 34,892 | 34,892 | +0 | differs (hex) |
| `RAK_3112_repeater_bridge_espnow` | 1,176,333 | 1,176,333 | +0 | 64,072 | 64,072 | +0 | differs (bin) |
| `RAK_3401_repeater` | 411,040 | 411,092 | +52 | 35,636 | 35,636 | +0 | differs (hex) |
| `RAK_4631_companion_radio_ble` | 465,172 | 465,224 | +52 | 149,768 | 149,768 | +0 | differs (hex) |
| `RAK_4631_repeater` | 441,784 | 441,820 | +36 | 37,240 | 37,240 | +0 | differs (hex) |
| `RAK_4631_room_server` | 435,096 | 435,116 | +20 | 40,928 | 40,928 | +0 | differs (hex) |
| `RAK_4631_sensor` | 438,776 | 438,812 | +36 | 35,328 | 35,328 | +0 | differs (hex) |
| `RAK_WisMesh_Tag_repeater` | 369,076 | 369,128 | +52 | 34,916 | 34,908 | -8 | differs (hex) |
| `SenseCap_Solar_repeater` | 366,108 | 366,160 | +52 | 34,876 | 34,876 | +0 | differs (hex) |
| `ThinkNode_M1_repeater` | 306,180 | 306,264 | +84 | 32,980 | 32,972 | -8 | differs (hex) |
| `ThinkNode_M6_repeater` | 366,212 | 366,280 | +68 | 34,932 | 34,932 | +0 | differs (hex) |
| `ThinkNode_M8_repeater` | FAIL | FAIL | | FAIL | FAIL | | |
| `WioTrackerL1-1W_repeater` | 378,716 | 378,768 | +52 | 35,436 | 35,436 | +0 | differs (hex) |
| `WioTrackerL1_repeater` | 377,724 | 377,760 | +36 | 35,428 | 35,428 | +0 | differs (hex) |
| `Xiao_C6_repeater_` | 542,479 | 542,479 | +0 | 29,856 | 29,856 | +0 | differs (bin) |
| `Xiao_nrf52_repeater` | 361,800 | 361,836 | +36 | 34,596 | 34,604 | +8 | differs (hex) |
| `ikoka_handheld_nrf_e22_30dbm_repeater` | 358,296 | 358,316 | +20 | 34,468 | 34,476 | +8 | differs (hex) |
| `ikoka_nano_nrf_22dbm_repeater` | 361,176 | 361,212 | +36 | 34,556 | 34,564 | +8 | differs (hex) |
| `ikoka_stick_nrf_22dbm_repeater` | 373,768 | 373,804 | +36 | 34,708 | 34,716 | +8 | differs (hex) |
| `muzi_base_uno_repeater` | 301,236 | 301,236 | +0 | 32,932 | 32,932 | +0 | identical |
| `t1000e_repeater` | 294,712 | 294,712 | +0 | 32,876 | 32,876 | +0 | identical |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | 241,080 | 241,080 | +0 | 26,704 | 26,704 | +0 | identical |
| `wio-e5-mini_repeater` | 204,136 | 204,136 | +0 | 17,172 | 17,172 | +0 | identical |
| `wio-e5-mini_sensor` | 203,664 | 203,664 | +0 | 15,276 | 15,276 | +0 | identical |

</details>

## Upstream

Searched meshcore-dev/MeshCore on 20.10.2026: text queries over all pull requests
(open and closed) for `target.cpp`, `radio_new_identity`, `RadioNoiseListener`,
board abstraction, generic target, FEM and the board names; the file lists of all
497 open pull requests; and the diffs of the 120 that touch a `target.*`, the
radio wrappers or the four FEM boards.

- No pull request proposes a board abstraction, a generic target, or a shared
  `radio_new_identity()`. The only hit for `radio_new_identity` in text is the
  closed, stale #1164.
- **#2280** (ascon based PRNG, open, last updated 2026-08) removes the callers of
  `radio_new_identity()` from `examples/` and changes `RadioNoiseListener::random()`
  to drop the `random()` mix. If it lands, cut A's function has no caller left, and
  the test pinning the mix would fail by design. Whichever lands second has to
  follow the other.
- **#3496** (KISS modem AGC and FEM gain, open) edits the `fem_rxgain` lines of all
  four FEM boards and adds a virtual to `RadioLibWrapper` in a different place than
  this step's friend line. Relevant for cut B.
- **#3638** (dual radio profiles, open) rewrites a large part of `RadioLibWrapper`
  (members after `_radio`, not the class head this step touches) and 10
  `target.cpp`, among them `heltec_v4`, where it changes how the radio is
  constructed. Relevant for cut D.
- Deleting the copies in cut A conflicts by context with open pull requests that
  have `radio_new_identity` lines in their hunks (#1799, #1850, #1913, #2152, #2682,
  #2927, #3213, #3472, #3638). Each is a trivial rebase (drop the function).
  Four pull requests edit the declaration line in a `target.h` (#1885, #2152, #2568,
  #3100); cut A does not touch any `target.h`, so they do not conflict.
- New board pull requests (about 50) carry their own copy; with the weak definition
  they keep linking unchanged.
- Merged churn in `target.*` since 2026-06-01: 25 pull requests, at most 5 such files
  each, mostly new boards. No merged mass edit; the mass edits are open pull
  requests only (#1962 nRF52 RTC, 27 target files; #1799 GPS, 15; #2912 RadioLib
  7.7.1, 9 `radio_init()` bodies), which are the ones to watch for cuts C and D.

## Observations (not part of this step)

- `m5stack_unit_c6l` and `xiao_c6` have no `target.cpp`; their board file
  (`UnitC6LBoard.cpp`, `XiaoC6Board.cpp`) defines the target globals and functions,
  including a copy of `radio_new_identity()`. Those two copies are left for the cut
  that touches the board files, so this step edits `target.cpp` files only.
  `wio-tracker-l1-eink` builds the sources of `wio-tracker-l1`.
- All ESP-NOW environments fail to compile on `integration` (see "Builds").
- `Generic_E22_kiss_modem` sets neither `RADIO_CLASS` nor `WRAPPER_CLASS`, unlike
  every other environment of `generic-e22`.
