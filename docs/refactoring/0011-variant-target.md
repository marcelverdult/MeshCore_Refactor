# 0011: One generic variant target built from a board description

Status: cut A (one shared `radio_new_identity()`) and cut B (one shared FEM RX gain
handler) implemented, each its own pull request, B stacked on A. Cuts C to E are
planned and need their own review before code. Behaviour change: none.

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
| C | a shared default `radio_init()` for the largest groups (28 + 18 + 5 + 4 + 3 of 89) | about 58 `target.cpp` | planned |
| D | the radio, wrapper and RTC globals from the board's build flags | about 87 `target.cpp` | planned (description: build flags, decided) |
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
