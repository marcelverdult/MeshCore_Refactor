# Sync upstream/dev into integration (07.11.2026)

Merge of `upstream/dev` 90a1aac1 (15 commits since the last sync base 26079dea) into
`integration` b40cef1f, branch `claude/sync-upstream-dev-20261107-cf5b5f`. The merge
commit is a7d5a792, the test follow-up 27382247.

## What upstream brought

- #3774 (`b44f010b`, `f0c6d471`) RP2350 support: six `Pico2W_*` environments, the PicoW
  companions now match the other variants (`MAX_CONTACTS=350`, `MAX_GROUP_CHANNELS=40`,
  WiFi in the BLE environment), and `platform-raspberrypi` is pinned to 4e22a0dc
  (framework-arduinopico 5.6.0).
- #3496 KISS modem protocol v2 (`5527a26f`, `9235d761`, `c387fc22`, `720f0f15`,
  `dc539d2d`, `f4f394c0`, `5a6027d3`): capability bits, a configurable AGC reset
  interval (default 32 s), FEM RX/TX gain through the board's `radio.fem.*` commands,
  `SetRxBoostedGain`/`GetRxBoostedGain` (0x20/0x21), FEM replies in arrival order, and
  the Wio Tracker L1 1W no longer offers TX gain control. New host test
  `test/test_kiss_modem/test_hw_controls.cpp`.
- The driver side of #3496: `RadioLibWrapper::supportsRxBoostedGain()`, the LR11x0 chips
  keep the boosted gain flag only when the chip accepted the call, LR2021 keeps a boost
  level per RX path (low and high frequency, cutoff 1500 MHz), and the SX126x AGC reset
  always reapplies the runtime gain (`a94f510f`).
- #3099 (`bb687046`) USB CDC flags for the M5Stack Unit C6L KISS modem environment.
- The boards' `radio.fem.*` handlers write to `_prefs` only when a store exists (KISS
  attaches none): Heltec T096, Tracker V2, V4, V4 R8, Station G3, Wio Tracker L1 1W.

## Conflicts and their resolution

Ten files, each resolved on its own. The rule was: keep the integration side where a
refactoring step already bundles the logic, and build the upstream purpose into it.

| File | Conflict | Resolution | Why |
|---|---|---|---|
| `CustomLLCC68Wrapper.h` | upstream passes the gain state to `sx126xResetAGC()` and adds `supportsRxBoostedGain()` | integration side plus the new override | the reset call is already `sx126xResetAGC(chip(), getRxBoostedGainMode())` in `CustomSX126xWrapper<Chip>`; only the override was missing |
| `CustomLR1110Wrapper.h`, `CustomLR1121Wrapper.h` | upstream adds `supportsRxBoostedGain()` to each | integration side; one override in `CustomLR11x0Wrapper<Chip>` | both chips share that template |
| `CustomLR1110.h`, `CustomLR1121.h` | upstream keeps the gain flag only on success, in each class | integration side; the same change once in `CustomLR11x0<Base,Traits>` | both chips share that template |
| `CustomLR2021.h` | upstream adds per-path boost levels and a restore after a refused call; integration moved the RX activity logic into `RxActivityTracker` | hand merge: `RxActivityTracker` kept, upstream's `isHighFreqPath()`, `setRxBoostedGainMode(uint8_t)` and `getRxBoostedGainMode()` taken, the two level members default to RadioLib's `RADIOLIB_LR2021_RX_BOOST_LF`/`_HF` | the level members mirror RadioLib's `gainModeLf`/`gainModeHf` defaults |
| `HeltecT096`, `HeltecTrackerV2`, `HeltecV4`, `HeltecV4R8` `Board.cpp` | upstream adds `if (_prefs)` before each `setByKey` | integration side (`FemRxGain<BOARD>`) | `FemRxGain` already null-guards `_prefs` (step 11B); `FemRxGainWithoutStore` in `test_fem_rxgain` pins it |

Merged without conflict and checked by reading the diff: `RadioLibWrappers.h`,
`SX126xReset.h`, `LR11x0Reset.h`, `CustomSX1262Wrapper.h`, `CustomSX1268Wrapper.h`,
`CustomLR2021Wrapper.h`, `examples/kiss_modem/{KissModem.cpp,KissModem.h,main.cpp}`,
`StationG3Board.cpp` and `WioTrackerL1Board.cpp` (these two still carry their own copy of
the FEM handlers; upstream only added the guard), the rpi_picow and C6L `platformio.ini`.

## Test changes

Upstream changed one pinned behaviour on purpose, so one test follows it:

- `BoostedGainFlagIsStoredBeforeTheChipAnswers` pinned "store first". It now pins "the
  flag follows only a call the chip accepted" (`CustomLR11x0`).
- The RadioLib stand-in (`test/test_radio_chips/stubs/RadioLib.h`) gets the three LR2021
  constants and the level setter. The values were compared with the real RadioLib 7.8.1
  header: `RADIOLIB_LR2021_RX_BOOST_LF` 0x00, `_HF` 0x04, and
  `LR2021::setRxBoostedGainMode(uint8_t level)`.
- New: `LR2021Gain` (defaults of both paths, the level kept per path, the restore after a
  refused call) and `supportsRxBoostedGain()` for all eight wrappers
  (`test_radio_chip_wrappers`).

## Checks

- Native: all 27 `native*` environments, 939 test cases, 0 failed, 1 skipped. This
  includes `native_kiss_modem` (43 cases, with the new FEM and gain tests from upstream).
- Firmware, sync tree: 208 environments (`docs/refactoring/sync-build-envs.txt`), 205
  build, 3 fail. All 205 `firmware.elf` files are newer than the newest tracked source
  file (`CustomLLCC68Wrapper.h`, 07.11. 23:23).
- Firmware, `integration` b40cef1f: the same list without the six `Pico2W_*`
  environments (they do not exist there), 202 environments, 199 build, 3 fail.
- Sizes per environment: `docs/refactoring/sync-build-sizes-20261107.tsv`.

Red environments, built on both trees with the same result (proven by measurement):

| Environment | Sync | integration | Limit or error |
|---|---|---|---|
| `ThinkNode_M5_companion_radio_serial` | `'D7'` and `'D6'` not declared (command line define, compiling `main.cpp`) | same errors | compile error |
| `Station_G2_companion_radio_ble` | 1323569 bytes flash | 1323541 | 1310720 |
| `Station_G3_ESP32_companion_radio_ble` | 1325213 bytes flash | 1325181 | 1310720 |

The first two were red at the last sync too. `Station_G3_ESP32_companion_radio_ble` is new
in this list; whether it was part of the last matrix is not known (that list was not
kept), so only the measurement above is certain: it is over the limit on `integration`
as well. The sync adds 28 and 32 bytes to the two oversize ESP32 builds.

Flash and RAM against `integration` (190 environments built on both trees outside the
RP2040 family):

- 152 non-KISS environments: flash +0 to +320 bytes (32 identical), RAM unchanged. Most
  sit between +4 and +64. The MeshTracker X1 environments are the high end (+208 to +320).
- 37 KISS modem environments: flash +1408 to +3152 bytes, RAM -8 or 0. That is the
  protocol v2 code in `KissModem.cpp`.
- `M5Stack_Unit_C6L_kiss_modem`: flash -5626, RAM +96. Upstream added
  `ARDUINO_USB_CDC_ON_BOOT`, `ARDUINO_USB_MODE` and `ENABLE_USB_INTERFACE` to that
  environment (`bb687046`).
- RP2040 (nine environments): `integration` does not pin `platform-raspberrypi` (the
  line only carries a comment). On 08.11. it resolved to platform 1.20.0 with
  framework-arduinopico 6.1.0, while the sync pins 5.6.0. Against that unpinned
  baseline the sync grows by 32 to 65 KB of flash in every RP2040 environment, which
  comes from the framework version. To compare the code alone, the baseline tree was
  rebuilt with the same pin (a temporary edit of its `platformio.ini`, not committed):

  | Environment | Flash sync vs base | RAM sync vs base |
  |---|---|---|
  | `PicoW_repeater`, `PicoW_room_server`, `waveshare_rp2040_lora_repeater`, `RAK_11310_repeater` | +8 | 0 |
  | `PicoW_kiss_modem`, `waveshare_rp2040_lora_kiss_modem` | +1472 | -4 |
  | `PicoW_companion_radio_usb` | +16 | +49080 |
  | `PicoW_terminal_chat` | +56 | +49536 |
  | `PicoW_companion_radio_ble` | +36644 | +52416 |

  The RAM growth of the three companion and chat builds matches upstream's change of
  `MAX_CONTACTS` from 100 to 350 and `MAX_GROUP_CHANNELS` from 8 to 40 in
  `variants/rpi_picow/platformio.ini`. The BLE flash growth matches the added WiFi
  interface. Both are read from the diff, not isolated by a separate build.
- `Pico2W_*` (six environments) build; there is no baseline for them. Flash 466564 to
  684820 bytes, RAM 73504 to 177160 bytes.

## Open risks

- No board run, which includes the Pico W and Pico 2 W builds.
- The KISS modem v2 code (`KissModem.cpp`, `main.cpp`, the order of FEM replies, the
  radio gain request) runs on the host only through upstream's `test_hw_controls` and
  `test_tx_backpressure`. `main.cpp` itself has no host test.
- `integration` builds its RP2040 firmware with whatever `platform-raspberrypi` is
  newest on the day; the sync takes upstream's pin.
- The small flash growth of the non-KISS environments (+4 to +64 bytes, +208 to +320 for
  MeshTracker X1) is not isolated. A new virtual method and the unconditional gain call
  in the SX126x AGC reset are the likely cause. NOT VERIFIED.
- The baseline build directories were deleted by PlatformIO when the pin was added, so
  the `integration` sizes come from the build logs of this run. The tree was clean at
  b40cef1f until that edit.
- SX1276 still accepts packets without a payload CRC (see the note of 04.11.).
- No LR2021 firmware environment exists; LR2021 is covered by the host tests only.
- `StationG3Board.cpp` and `WioTrackerL1Board.cpp` keep their own FEM handlers instead
  of `FemRxGain`. Not part of this sync, noted for a later step.
