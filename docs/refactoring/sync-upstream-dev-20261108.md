# Sync upstream/dev into integration (08.11.2026)

Merge of `upstream/dev` 4deb88df (6 commits since the last sync base 90a1aac1) into
`integration` d899a08d, branch `claude/sync-upstream-dev-20261108-c70387`. The merge commit
is 33b92405.

## What upstream brought

- #3782 (`9fd02ff3`, merged as `4deb88df`): `MyMesh::onContactOverwrite()` and
  `MyMesh::onContactsFull()` check `_serial` for null before they call `isConnected()`.
- #3637 (`665ece4c`, merged as `f9b1945b`): the MeshTracker X1 variant takes MicroNMEA from
  its base `lib_deps`, so every environment of the variant inherits it. This is Marcel's own
  upstream PR.
- #3780 (`c2e9cba0`, merged as `056d264c`): PR backtesting in `CONTRIBUTING.md`, 18 lines,
  documentation only.

## Probe and merge

- Probe: `git merge-tree --write-tree --name-only integration upstream/dev` exited 0 and
  listed no conflict file. Result tree 37ac0296.
- The real merge (signed, no rebase, no squash) has the same tree, 37ac0296. Against
  `integration` it changes two files: `CONTRIBUTING.md` (+18) and
  `examples/companion_radio/MyMesh.cpp` (+2/-2).
- There was no conflict to resolve. Step 10d is untouched: the merge changes two hunks in
  `MyMesh.cpp` (lines 314 and 322, the two callbacks), and `ILLEGAL_ARG` appears 24 times in
  that file before and after.
- `variants/meshtracker_x1/platformio.ini` does not change in the merge. `integration`
  already has upstream's version of the file (see the collision check).

## Checks

All on the merge result in this worktree (clean tree, HEAD 33b92405).

- Native: all 27 `native*` environments, run one by one with `pio test -e <env>`. 942 test
  cases, 941 succeeded, 0 failed, 1 skipped (the skipped case is in `native_radio_chips`,
  262 cases). Every environment returned exit code 0. `integration` had 941 succeeded and
  1 skipped, so the counts are the same. No test needed an adjustment.
- Firmware: 15 environments, built on the sync tree and on a base tree made from
  `integration` d899a08d, both with `SOURCE_DATE_EPOCH=1760000000`. All 15 build on both
  sides. Sizes per environment: `docs/refactoring/sync-build-sizes-20261108.tsv`. The list
  `sync-build-envs.txt` got three environments that were missing.

Why these 15 and not the 211 environments of the matrix list: the sync changes code in one
translation unit, `examples/companion_radio/MyMesh.cpp`, and the MeshTracker X1
`platformio.ini` change is already on `integration`. So the set is every environment of the
`meshtracker_x1` variant (5), one companion build per platform and per serial interface
(nRF52 BLE and USB, ESP32 BLE, USB and WiFi, RP2040 USB and BLE, STM32WL USB), and two
repeater controls (`RAK_4631_repeater`, `Heltec_v3_repeater`).

Flash and RAM against `integration`:

| Environment | Flash sync | Flash base | Delta | RAM delta |
|---|---|---|---|---|
| `MeshTracker_X1_repeater` | 302496 | 302496 | +0 | +0 |
| `MeshTracker_X1_room_server` | 295012 | 295012 | +0 | +0 |
| `MeshTracker_X1_companion_radio_usb` | 323692 | 323692 | +0 | +0 |
| `MeshTracker_X1_companion_radio_ble` | 329340 | 329324 | +16 | +0 |
| `MeshTracker_X1_kiss_modem` | 245348 | 245348 | +0 | +0 |
| `RAK_4631_companion_radio_ble` | 473304 | 473288 | +16 | +0 |
| `RAK_4631_companion_radio_usb` | 464280 | 464280 | +0 | +0 |
| `Heltec_v3_companion_radio_ble` | 1674553 | 1674545 | +8 | +0 |
| `Heltec_v3_companion_radio_usb` | 661029 | 661021 | +8 | +0 |
| `Heltec_v3_companion_radio_wifi` | 1110649 | 1110641 | +8 | +0 |
| `PicoW_companion_radio_usb` | 529496 | 529488 | +8 | +0 |
| `PicoW_companion_radio_ble` | 700800 | 700792 | +8 | +0 |
| `wio-e5-mini_companion_radio_usb` | 213320 | 213312 | +8 | +0 |
| `RAK_4631_repeater` | 446172 | 446172 | +0 | +0 |
| `Heltec_v3_repeater` | 1175481 | 1175481 | +0 | +0 |

Second check on the artifacts, independent of the sizes:

- Image comparison (`firmware.hex` or `firmware.bin`, sha256). Byte-identical on both sides:
  `MeshTracker_X1_repeater`, `MeshTracker_X1_room_server`, `MeshTracker_X1_kiss_modem`,
  `RAK_4631_repeater`. `Heltec_v3_repeater` has the same size and differs in 65 bytes: 32 at
  offsets 177 to 208 and 33 at the end of the image (1175872 to 1175904). Those match the
  ESP-IDF app descriptor ELF hash and the checksum plus SHA-256 trailer; the layout is read
  from memory, not checked against the IDF headers. Both ELFs contain their own tree path
  (one hit each), which is the likely cause. Not isolated by a build in the same path.
- Disassembly of `RAK_4631_companion_radio_usb` (`arm-none-eabi-objdump`):
  `MyMesh::onContactOverwrite()` has one extra `cbz r0, ...` on the sync side, the base has
  none. That is the null check of `_serial` from #3782. Checked on this one environment.
- All 15 `firmware.elf` files of the sync tree (oldest 08.11. 20:35:18) are newer than the
  files the merge wrote (`MyMesh.cpp`, `CONTRIBUTING.md`, 08.11. 20:31:12).

## Duplicates and collisions

Read only; no branch was changed or deleted. All 62 local and remote branches were compared
against the three files upstream changed (`CONTRIBUTING.md`, `MyMesh.cpp`, the MeshTracker X1
`platformio.ini`), each from its merge base with `integration`.

- #3637 and the fork: `integration` has the same change as 3bc2e5f2 (19.10.). The patch id
  of 3bc2e5f2 and of upstream's 665ece4c is the same (7948b13e), the file is identical on
  `integration` and `upstream/dev`, and the tip of
  `upstream-candidate/meshtracker-x1-micronmea-dep` is 665ece4c, an ancestor of
  `upstream/dev`. So the PR went in as submitted and the merge adds nothing for that file.
  The candidate branch is now redundant.
- #3782 and the fork: no branch changes `onContactOverwrite()` or `onContactsFull()`. The
  only other branch that touches `MyMesh.cpp` is `upstream-candidate/companion-channel-txt-min-len`,
  in `handleCmdFrame` (the `CMD_SEND_CHANNEL_TXT_MSG` branch). `git merge-tree` of that
  branch against `upstream/dev` exits 0.
- Upstream's `handleCmdFrame` already has the `len >= 7` check for `CMD_SEND_CHANNEL_TXT_MSG`
  (line 1178 on `upstream/dev`). `integration` covers the same case through the minimum
  length 7 in `CMD_TABLE` (line 1043). Both were there before this sync.
- #3782 guards two callbacks only. `MyMesh.cpp` has 70 `_serial->` uses and 2 `_serial &&`
  checks after the merge. The other uses, for example the one at line 332, still assume an
  attached serial interface, as on upstream. Not part of this sync.

## Open risks

- No board run, none of the 15 firmware images was flashed.
- The two guards have no host test. The host harness does not call the callbacks with a null
  `_serial`. The effect is shown by the disassembly of one nRF52 build only. NOT VERIFIED at
  runtime.
- Not built, NOT VERIFIED: the other 196 environments of the matrix list
  (`sync-build-envs.txt`, 211 in total; the repo has 649 `[env:]` sections). This includes the
  companion environments with a display (`ui-new` and the other `ui-orig` builds), the other
  nRF52, ESP32, RP2040 and STM32 companions, and the three environments that were red on the
  last sync (`ThinkNode_M5_companion_radio_serial`, `Station_G2_companion_radio_ble`,
  `Station_G3_ESP32_companion_radio_ble`). The reason for the small set is above; for the
  environments not built it is an inference, not a measurement.
- The flash growth of +8 to +16 bytes in the companion builds is read as the two added null
  checks. It is not isolated by building with only one of the two.
- The build path as the reason for the differing hash fields in `Heltec_v3_repeater` (see
  above) is not proven.
- SX1276 still accepts packets without a payload CRC (see the note of 04.11.).
