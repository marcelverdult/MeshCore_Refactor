# Sync upstream/dev into integration (12.11.2026)

Merge of `upstream/dev` bb870313 (24 commits since the last sync base b8ae75d2) into
`integration` 5e32b39d, branch `claude/sync-upstream-dev-20261112-cdc32f`. The merge is a
signed merge commit (no rebase, no squash). Nothing is pushed and nothing is merged into
`integration`.

## What upstream brought

- GPS: #3798 `GPS_BAUD_PROBE_LIST` as a build flag for any `NmeaGpsDriver` (the constructor
  default now reads it; enabled on Station G2 and G3), #3801 GPS guard shows `time wait`
  instead of `rej nofix` between fixes, #3813 and #3817 drop the guard's alert telemetry
  channel (`getAlertOffset()` and the `querySensors` code are gone), #3819 MicroNMEA no
  longer reads past its buffer (16 zeroed tail bytes).
- #3815 CLI `get advert.loc` and `set advert.loc <none|share|prefs>`, usable without a GPS;
  `gps advert` stays for GPS builds.
- #3807 stack-buffer overflow fix in CLI reply buffers (touches the three `MyMesh.cpp`).
- #3811 and #3822 mDNS name built without `String` (`examples/companion_radio/main.cpp`).
- #3784 repeater history queries and its revert #3812 (net effect in this range: none).
- 0147165b reverts the rak13302 tests: `test/test_rak3401_board_startup/`,
  `test/test_rak3401_onboard_radio/` and `[env:native_rak3401_onboard_radio]` are gone.

## Probe and merge

`git merge-tree --write-tree --name-only HEAD upstream/dev` listed 3 conflict files. The
merge commit is 66ab992a (signed, `gpgsig` present in the commit object).

## Conflicts and how they were solved

- `src/helpers/CommonCLI.cpp`: upstream put `advert.loc` into the old `else if` chains of
  `handleSetCmd` and `handleGetCmd`, which the fork replaced with the tables of step 13.
  The fork side stays. `advert.loc` is now `CommonCLIImpl::setAdvertLoc` / `getAdvertLoc`
  and one row in `kSetCmds` (`"advert.loc "`) and one in `kGetCmds` (`"advert.loc"`), placed
  before `guest.password`. Not gated by `ENV_INCLUDE_GPS`, as upstream. Replies are
  upstream's (`OK`, `Error, invalid policy`, `> none|prefs|share`). Neither name is a prefix
  of another row, so the order does not matter. Tests in the harness of 13a (`CliTest`):
  `SetAdvertLocTakesEachPolicyAndSaves`, `SetAdvertLocWithAnUnknownPolicyAnswersErrorAndChangesNothing`,
  `SetAdvertLocNeedsItsTrailingSpaceAndKeepsAdvertIntervalApart` in `test_cli_set.cpp`,
  `GetAdvertLocShowsThePolicy` in `test_cli_get.cpp`. They run in the five `native_common_cli*`
  environments (+4 cases each).
- `platformio.ini`: upstream shortened `test_ignore` to `test_kiss_modem`; the fork keeps its
  long list and drops only `test_rak3401_board_startup`. The merge removed
  `[env:native_rak3401_onboard_radio]`.
- `.github/workflows/run-unit-tests.yml`: the fork's `pio test` line without
  `-e native_rak3401_onboard_radio` (27 environments).

Silent collisions, checked and fine:

- E2a (`VariantTarget.cpp`, `SENSORS_TARGET_GLOBALS`): the shared `NmeaGpsDriver gps_driver(Serial1);`
  now picks up `GPS_BAUD_PROBE_LIST` through the constructor default of #3798. Station G2 and
  G3 use the shared block and define the flag, so they get the probe list without a line of
  their own. Measured on the built images: `NMEA_GPS_PROBE_BAUDS` is a symbol in
  `Station_G2_repeater`, and the byte triple 9600, 115200, 38400 is in the firmware of
  `Station_G2_repeater` and `Station_G3_ESP32_repeater`, not in `Heltec_v3_repeater`.
- `variants/promicro/target.cpp` (group B, own `target.cpp`): auto-merged to the plain
  `NmeaGpsDriver gps_driver(Serial1);`; upstream dropped the local probe-list branch because
  the driver reads the flag itself. `docs/refactoring/0011-variant-target.md` says so now.
- `EnvironmentSensorManager.cpp`: upstream removed the guard alert telemetry; the fork's
  `GpsGuard`/GPS code from the 10.11. sync is not touched. `getAlertOffset`, `rej nofix`
  appear nowhere else in the tree (grep over `*.cpp`, `*.h`, `*.ini`; `docs/cli_commands.md`
  mentions both and was updated by upstream).

## Docs

`0013-cli-command-table.md`: `advert.loc` row in the set table, `advert.loc` in the key list of
the get section, test counts for `test_cli_set.cpp` (52) and `test_cli_get.cpp` (25).

## Checks

Native, the environments of `run-unit-tests.yml`, one by one with `pio test -e <env>`, exit
code 0 each.

| | Environments | Cases | Succeeded | Skipped | Failed |
|---|---|---|---|---|---|
| before the merge (5e32b39d) | 28 | 2447 | 2446 | 1 (`native_radio_chips`) | 0 |
| after the merge | 27 | 2456 | 2455 | 1 (`native_radio_chips`) | 0 |

Difference: the reverted rak13302 tests (4 cases in `native`, 7 in the removed environment)
and the 20 new `advert.loc` cases (4 in each of the five `native_common_cli*`).
`native` 143 to 139, `native_common_cli` 185 to 189, `_esp32` 185 to 189, `_espnow` 193 to
197, `_nrf52` 198 to 202, `_rs232` 192 to 196; all other environments unchanged.

Firmware: 19 environments, `SOURCE_DATE_EPOCH=1760000000`, merged tree. Not the 211 of
`sync-build-envs.txt` (see open points). Chosen for what the merge touches: E2a variants with
and without the probe list (Station G2 and G3, Heltec V3, V4, V4 R8, tracker V2, T096,
ProMicro), all three mDNS code paths (WiFi companions), CommonCLI on every platform (nRF52,
ESP32, RP2040).

| Env | RAM | Flash | Result |
|---|---|---|---|
| Station_G2_repeater | 74968 | 1218873 | builds |
| Station_G2_companion_radio_wifi | 184800 | 1153665 | builds |
| Station_G2_companion_radio_ble | 184420 | 1333341 | link error, flash too big (max 1310720) |
| Station_G3_ESP32_repeater | 74976 | 1220477 | builds |
| Station_G3_ESP32_companion_radio_ble | 184428 | 1335009 | link error, flash too big |
| Heltec_v3_repeater | 62768 | 1186005 | builds |
| heltec_v4_repeater | 62740 | 1184149 | builds |
| heltec_v4_companion_radio_wifi | 174492 | 1145681 | builds |
| heltec_v4_r8_repeater | 62656 | 1187681 | builds |
| heltec_tracker_v2_repeater | 74608 | 1237145 | builds |
| Heltec_t096_repeater | 35828 | 432632 | builds |
| RAK_4631_repeater | 37664 | 468524 | builds |
| RAK_4631_companion_radio_ble | 150160 | 492368 | builds |
| ProMicro_repeater | 34156 | 364304 | builds |
| ThinkNode_M1_companion_radio_ble | 151680 | 384564 | builds |
| t1000e_repeater | 33116 | 311128 | builds |
| t1000e_companion_radio_usb | 141396 | 321076 | builds |
| MeshTracker_X1_repeater | 33196 | 318608 | builds |
| PicoW_repeater | 85484 | 532216 | builds |

The two BLE companions that fail are not caused by this merge. The same two environments
built the same way overflow on `integration` 5e32b39d (`Station_G3_ESP32_companion_radio_ble`
1334945, `Station_G2_companion_radio_ble` 1333265) and `Station_G3_ESP32_companion_radio_ble`
on `upstream/dev` bb870313 (1327237) as well. The merge adds 64 bytes (G3) and 76 bytes (G2)
over `integration`. Caveat: the libdeps of these builds were seeded from existing
`.pio/libdeps` directories to stay under the PlatformIO registry limit (no integrity file),
so absolute sizes may differ from a fresh CI build. Whether the images overflow with freshly
resolved libraries is NOT VERIFIED. The fork premium over `upstream/dev` for
`Station_G3_ESP32_companion_radio_ble` is +7772 bytes, in the range of the 10.11. sync.

## Open points, NOT VERIFIED

- Not built: 192 of the 211 environments of `sync-build-envs.txt`, no size comparison against
  `upstream/dev` except the one environment above.
- No board run, no image flashed: probe-list baud detection on Station G2/G3 with a Grove
  GPS at 115200, the `time wait` guard status, the mDNS name on a device.
- No host test for `get/set advert.loc` outside the CLI harness, none for the GPS guard,
  `NmeaGpsDriver` probe list or the MicroNMEA buffer tail (#3819).
