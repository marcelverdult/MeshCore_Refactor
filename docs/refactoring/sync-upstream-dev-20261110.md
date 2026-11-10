# Sync upstream/dev into integration (10.11.2026)

Merge of `upstream/dev` d495d2ed (50 commits since the last sync base 4deb88df) into
`integration` bc1d7710, branch `claude/sync-upstream-dev-20261110-b58e97`. The merge is a
signed merge commit (no rebase, no squash). Nothing is pushed and nothing is merged into
`integration`.

## What upstream brought

- #3753 (`62ae611a`), the GPS rework by fdlamotte, with the two sub-PRs from
  AlexandrTyshchenko (baud autodetect, GPS guard). New `GpsDriver`, `NmeaGpsDriver`,
  `AirohaGpsDriver`, `RakI2cGpsDriver`, `RakLocationProvider`, `GpsGuard`. `CommonGpsPrefs`
  becomes an abstract class with `CommonGpsPrefs.cpp` (`handleCommand`, `apply`,
  `applyGuardReference`, `onClockSetManually`). New prefs `gps_period`, `gps_fence`,
  `gps_guard`; new JSON keys `period`, `fence`, `guard` (the last two only with `GPS_GUARD`).
  `gps_active` is replaced by `isGpsOn()` and `hasValidFix()`. The `gps` CLI is rewritten.
- #3785: companion `adc.multiplier` command, stored in the prefs. Also `ui.gps_action`
  (from `6eea5e71`).
- #3648 LilyGO T3 STM32, #3762 Nucleo-WL55JC1, enable `simple_room_server` on STM32.
- #3623 Wio Tracker L1 E-Ink repeater, #3724 and `1df6262d` e-ink refresh hooks,
  `220f886b` INA219 sensor.
- #3613 RAK13302 auto onboard sleep (`rak3401::sleepOnboardRadio()`), `c2f65e5f`
  DFRobot LoRaWAN ESP32-S3, #3725 ESP-NOW builds without a LoRa radio.

## Probe and merge

- Probe: `git merge-tree --write-tree --name-only integration upstream/dev` exited 1 and
  listed 41 conflict files.
- Decision (Marcel, AskUserQuestion): take upstream's GPS design. The fork's GPS prefs
  variants of step 11 and the `EnvironmentSensorManager` GPS code give way to #3753; the
  fork keeps everything else.

## Conflict resolutions

Rule used everywhere: upstream's behavior stays, and the series structure (command tables,
shared prefs, shared radio setup) stays.

- `src/helpers/CommonGpsPrefs.h`: macOS is case insensitive, so the fork's `CommonGPSPrefs.h`
  and upstream's `CommonGpsPrefs.h` are one path. One file now. It keeps `GPSPrefsFields`
  (with the new `gps_period`, `gps_fence`, `gps_guard`), upstream's abstract class, and a new
  `FieldGpsPrefs` that binds the abstract class to `GPSPrefsFields&` and, optionally, to
  `node_lat` and `node_lon` for the guard reference. `CommonGpsPrefs.cpp` is upstream's.
- `src/helpers/CommonNodePrefs.h`: member `FieldGpsPrefs gps`, `getGpsPrefs()`, dirty flag
  includes `gps`.
- `src/helpers/CommonCLI.h`, `CommonCLI.cpp`: the fork's command tables stay. Added
  `onClockSetManually` after `setCurrentTime` in `clockSync` and `time`, `applyGuardReference`
  in `setLat`, `setLon`, `gps setloc`. The old `gps` handlers (on, off, sync, diag, status)
  are gone. One ungated row `gps` calls `getGpsPrefs()->handleCommand()`. `gps setloc` and
  `gps advert` reply "OK" or "Error, invalid policy", as upstream does.
- `src/helpers/ServerMesh.cpp`: `applyGpsPrefs()` sets `gps_interval` when the sensor has it,
  then calls `getGpsPrefs()->apply(sensors)`. Upstream's `applyGpsPrefs()` bodies in the
  repeater, room server and sensor `MyMesh` live here.
- `examples/simple_repeater/MyMesh.h`, `examples/simple_room_server/MyMesh.cpp` and `.h`,
  `examples/simple_sensor/SensorMesh.h`: fork side kept, upstream's content is in `ServerMesh`.
- `examples/companion_radio/NodePrefs.h`: fork side, with the renamed include and member.
  `examples/companion_radio/MyMesh.cpp`: fork tables, plus rows for `adc.multiplier`
  (set and get) and `ui.gps_action` (under `DISPLAY_CLASS`).
- `platformio.ini`: fork `test_ignore` kept plus `test_rak3401_board_startup`; upstream's
  `[env:native_rak3401_onboard_radio]` taken; `CommonGpsPrefs.cpp` added to the 11
  `build_src_filter` lists that already have `CommonRadioPrefs.cpp`.
- `.github/workflows/run-unit-tests.yml`: fork's `pio test` line plus
  `-e native_rak3401_onboard_radio`.
- 31 `variants/*/target.cpp`: radio setup stays in shared code (step 11), upstream's GPS lines
  are adopted (`NmeaGpsDriver gps_driver(...)`, `MicroNMEALocationProvider nmea(gps_driver, ...)`,
  `isGpsOn()`/`hasValidFix()` loops). Eight had real content conflicts: `heltec_mesh_solar`,
  `heltec_tower_v2`, `heltec_tracker`, `meshadventurer`, `meshtracker_x1`, `nano_g2_ultra`
  (its own `NanoG2UltraGpsDriver`), `t1000-e`, `thinknode_m1`. `thinknode_m1` follows upstream,
  which removed the hardware switch logic.
- `variants/rak3401/target.cpp`: my first bulk resolution dropped upstream's
  `OnboardRadioIO` block and `rak3401::sleepOnboardRadio()`, which broke the link. Restored
  from `upstream/dev`. An audit of upstream-added lines in all merged variant files found no
  other loss.

One divergence from upstream outside the conflict files: `src/helpers/sensors/LocationProvider.h`
gets `#include <Arduino.h>`, because `getLastFixAge()` uses `millis()` and the native envs
need the declaration.

## Test changes

- Goldens: `gps:{en:1,int:900,period:600,adv_loc:1}`, `adc_mult:0`, `gps_act:1` in
  `test_common_node_prefs`, `test_companion_node_prefs`, `test_companion_frames/test_cli_ui.cpp`.
  `frame_sweep.golden`: only the 37 `files` hash lines changed (regenerated with
  `COMPANION_SWEEP_UPDATE=1`).
- `test/mocks/target.h`: new `MockLocation` that logs `location.setGpsPeriod N` and
  `location.setGpsOn 0|1`; `sendSentence` stub. `BootSequenceChecks.h` expects those events
  instead of `sensors.setSettingValue gps=...`. `RadioPrefsChecks.h`, `esp32_idf_stubs.h`
  (`GPIO_NUM_NC`), `CliHarness.h` (`isActive`), `identity_stubs.cpp`, `test_cli_memory.cpp`
  adapted.
- `test_cli_sensor.cpp`: the old GPS status, on, off, sync and diag tests are replaced by
  tests for `gps on`, `gps off`, `gps period`, status without a provider, and an unknown
  `gps` word. The non-GPS test only checks that `setloc` and `advert` answer "Unknown command".

## Checks

Native: all 28 `native*` environments from `run-unit-tests.yml`, run one by one with
`pio test -e <env>`, exit code 0 each. Total 2447 test cases, 2446 succeeded, 1 skipped
(`native_radio_chips`), 0 failed.

| Env | Cases | Env | Cases |
|---|---|---|---|
| native | 143 | native_server | 72 |
| native_kiss_modem | 43 | native_sensor | 55 |
| native_core | 13 | native_repeater | 42 |
| native_ofast | 15 | native_repeater_full | 51 |
| native_chat | 8 | native_region | 8 |
| native_identity_store | 19 | native_radio_chips | 262 |
| native_node_prefs | 8 | native_variant_target | 15 |
| native_radio_wrappers | 11 | native_variant_target_override | 2 |
| native_companion | 125 | native_esp32_rtc_backup | 9 |
| native_companion_buzzer | 130 | native_common_cli | 185 |
| native_companion_wifi | 146 | native_common_cli_nrf52 | 198 |
| native_companion_display | 140 | native_common_cli_esp32 | 185 |
| native_companion_all | 170 | native_common_cli_rs232 | 192 |
| native_rak3401_onboard_radio | 7 | native_common_cli_espnow | 193 |

(The 08.11. sync ran 27 environments and 942 cases; the new environment and the new GPS
tests account for the difference.)

Firmware: 109 environments, built on the sync tree and on `upstream/dev` d495d2ed (worktree),
both with `SOURCE_DATE_EPOCH=1760000000`. Per touched variant: its first repeater (or first)
environment and its first companion BLE or USB environment. Sizes:
`docs/refactoring/sync-build-sizes-20261110.tsv`.

- Sync tree: all 109 build.
- `upstream/dev`: one fails, `Heltec_mesh_solar_companion_radio_ble` (library error in
  `meshsolar/src/utils/logger.h:18`). It builds on the sync tree, so no flash delta exists for it.
- Flash against `upstream/dev` (108 comparable): repeaters +3248 to +6472 (median +6084),
  RAM +96 to +120; companions +3028 to +8108 (median +3416), RAM +8 to +32. No environment has
  delta 0. Largest: `Tbeam_SX1276_companion_radio_ble` +8108, `Tbeam_SX1262_companion_radio_ble`
  +8020, `T_Beam_S3_Supreme_SX1262_companion_radio_ble` +8000, `ThinkNode_M7_companion_radio_ble`
  +7864, `LilyGo_TDeck_companion_radio_usb` +7856. Smallest: `LilyGo_T3_STM32_repeater` +3248.
- The premium is not new. Measured on two environments: `Heltec_v3_repeater` merge base
  4deb88df 1175341 against `integration` 1180453 (+5112), and now sync 1185705 against
  `upstream/dev` 1180837 (+4868). `RAK_3401_repeater`: merge base 410912, `integration`
  417220 (+6308). For the other environments this is an inference, NOT VERIFIED.
- `RAK_3401_repeater` (433796 flash, 35908 RAM) and `RAK_3401_companion_radio_usb`
  (448144, 101460) were rebuilt after the `sleepOnboardRadio()` fix.

## Cut E2 (`0011-variant-target.md`) after #3753

E2 planned to move the GPS and sensor lines of 74 variants (groups of 22, 14, 9, 4 and 25)
into shared code. #3753 rewrites those lines first. Counted in `variants/*/target.cpp` on the
merge result:

- The old line `MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);`
  appears in 0 files. It is now two lines: `NmeaGpsDriver gps_driver(...)` (46 files) and
  `MicroNMEALocationProvider nmea(gps_driver, &rtc_clock)` (48 declarations).
- 39 of the 46 drivers are `NmeaGpsDriver gps_driver(Serial1);` with no arguments, 6 pass
  pins or baud lists, 1 other form.
- Not the generic shape: `rak3401`, `rak4631`, `gat562_30s_mesh_kit` (`RakI2cGpsDriver` with
  `RakLocationProvider`), `nano_g2_ultra` (own driver header), the Airoha variants.
- Sensor managers no longer keep `gps_active` or their own serial setup.

So the 39 plain lines are the part of E2 that stays mechanical. The 7 files whose driver takes
arguments, plus the RAK and Airoha variants, differ per board and need their own decision. A
per-variant matching against the original 74 lines was NOT done.

## Other findings

- `docs/refactoring/0001-shared-radio-gps-prefs.md` still names `CommonGPSPrefs`. Left as is.

## Open risks

- No board run; no image was flashed.
- `RakLocationProvider`, `GpsGuard` and `GPS_GUARD` paths have no host test. NOT VERIFIED at
  runtime.
- `thinknode_m1` follows upstream: the hardware switch logic is gone. Not checked on a board.
- Not built, NOT VERIFIED: the other 102 environments of the 211 in `sync-build-envs.txt`.
- The size cause beyond the two measured environments is an inference.
