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

Planned next steps (each needs its own design note before code), in order of
expected benefit: one table per setting group for the radio CLI;
a shared base for repeater, room server and sensor; one generic variant target
built from a board description; one RadioLib adapter per chip family; a
companion frame codec with a command table. See the review for the reasoning.

## Verifying a step yourself

```sh
pio test -e native                 # host tests, including the characterization tests
pio run -e RAK_4631_repeater       # any firmware environment
```
