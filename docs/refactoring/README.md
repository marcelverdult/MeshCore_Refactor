# Refactoring series: make MeshCore easier to extend

This folder documents a series of small, behaviour-preserving refactors of the
MeshCore firmware. Each step is its own pull request against `dev`, can be
reviewed on its own, and comes with the evidence that it changes nothing a user,
an app or the mesh can observe.

## Why

Adding a feature to MeshCore today touches far more files than the feature
itself, because the same logic exists in several copies (per role, per board,
per radio chip) and settings can only reach the code that needs them through
hand-written forwarding. A few measured examples from v1.18.0:

- Making CAD a separate setting (`7c8e0924`) touched 15 files.
- The remote LNA toggle (`71136671`) touched 162 variant files; moving it into
  the radio wrappers 23 days later (`9a95e25e`) touched 156 again.
- The radio and GPS settings existed twice, once in the server roles' NodePrefs
  and once in the companion's NodePrefs, and the companion copy needed three
  separate fixes in this release cycle that the other copy did not.

The full analysis, with file and line references for every finding, is in
[architecture-review-v1.18.md](architecture-review-v1.18.md).

## Rules every step follows

1. **No behaviour change.** On-air packets, companion protocol frames, CLI
   replies and the files on flash stay byte for byte the same. Where a step
   would need a behaviour change, that is split out and proposed separately.
2. **Tests first.** Before code moves, characterization tests pin down the
   current behaviour on the host (`pio test -e native`). The same tests must
   pass unchanged before and after the step. The tests are checked against
   deliberately broken code (mutation tests) to show they would catch a
   regression.
3. **Builds and hardware.** Every step is built for a matrix covering every
   role (repeater, room server, sensor, companion BLE/USB/WiFi, bridges) and
   every platform (ESP32, nRF52, RP2040, STM32), with flash and RAM compared to
   `dev`. Steps that touch persisted data are run on real boards, including an
   upgrade from the previous firmware and a downgrade back to it.
4. **Source compatible.** Existing code, including downstream forks, keeps
   compiling. Where a public header changes in an incompatible way, the step
   says so explicitly.
5. **One concern per step**, documented in a design note in this folder: what
   changes, why, which alternatives were rejected, and how it was verified.

## Steps

| # | Step | Design note | Status |
|---|------|-------------|--------|
| 1 | Share the radio and GPS settings between the server roles' and the companion's NodePrefs | [0001](0001-shared-radio-gps-prefs.md) | ready for review |
| 2 | Let the core read its tuning values from the radio settings instead of a getter per role | [0002](0002-core-reads-radio-prefs.md) | ready for review |
| 3 | Drive the radio settings (JSON, CLI, key/value store) from one table | [0003](0003-radio-settings-table.md) | ready for review |
| 4 | Share the underpinnings of repeater, room server and sensor in one base class (steps 4a to 4g; the boot sequence and the forwarding checks are the last two) | [0004](0004-shared-server-base.md), [0007](0007-boot-sequence.md), [0008](0008-forward-policy.md) | ready for review |
| 9 | One RadioLib adapter per chip family: the receive detection the seven chip classes shared as copies (9a), one template for the SX126x chip classes SX1262, SX1268 and LLCC68 (9b), one wrapper template per chip family instead of a cast in every method (9c), one template for the LR11x0 chip classes LR1110 and LR1121 (9d) | [0009](0009-radio-chip-adapter.md) | 9a to 9d ready for review |
| 10 | A command table for the companion protocol: a host harness that pins every frame (10a), one row per command code instead of the if chain (10b), then a bounded frame reader in the handlers instead of hand counted offsets (10c); 10d proposed in the note | [0010](0010-companion-command-table.md) | 10a to 10c ready for review |
| 11 | One generic variant target built from a board description, cut A: one shared `radio_new_identity()`; cut B: one FEM RX gain handler for four Heltec boards; cut C: one default `radio_init()` for 31 nRF52 boards; cut D: the radio globals of 31 nRF52 boards (D1) and the radio globals and `radio_init()` of 22 ESP32 boards (D2) from the board description, opt in through `RADIOLIB_TARGET_GLOBALS`; cut E1: the display and user button of 48 boards, opt in through `DISPLAY_TARGET_GLOBALS` (E2, GPS and sensors, not built, see the note) | [0011](0011-variant-target.md) | cuts A to D and E1 ready for review |

Steps are numbered after their design note, so the numbers have gaps: 5 to 8
are notes of fixes and of step 4. See the review for the reasoning behind the
order.

## Verifying a step yourself

```sh
pio test -e native                 # host tests, including the characterization tests
pio run -e RAK_4631_repeater       # any firmware environment
```
