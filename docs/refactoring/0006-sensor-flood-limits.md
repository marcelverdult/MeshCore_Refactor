# 0006: Align the sensor, part two: the flood hop limits

Status: implemented. Stacked on step 4f ([0007](0007-boot-sequence.md)), whose shared
`begin()` carries the one-time upgrade this change needs. Part one of the alignment
is [0005](0005-sensor-alignment.md).

This is a change of behaviour on the sensor, and it adds a version to `/prefs.json`
for every server role, so it is a branch and a note of its own.

## Context

`SensorMesh::allowPacketForward()` checked `flood_max` only. The repeater and the
room server also apply `flood.max.unscoped` and `flood.max.advert` through
`mesh::isFloodHopLimitExceeded()`. The sensor missed that change when it reached the
other roles; the history is in `architecture-review-v1.18.md` under finding F2.

Aligning the check is two lines. What makes it more is the state every existing
sensor is in.

## The upgrade hazard

Measured on the host, not assumed. Three tests in `test_sensor_role` hold it:

1. `AZeroHopLimitIsWrittenOutAsAZero`: a limit of 0 goes into `prefs.json` as an
   explicit `0`, not as an empty value.
2. `ASensorUpgradedOntoZeroHopLimitsKeepsForwarding` (before this change it asserted
   the opposite): `/prefs.json` is read through `loadSerial()`, which applies a stored
   value as it is, and since the limits are compared with `>=`, a 0 refuses every
   un-scoped flood and every advert at hop 0.
3. `AMissingKeyLeavesTheNewDefaultStanding`: a key absent from the file overwrites
   nothing, so only files that carry the key are affected.

The sensor set no default for either limit and never applied them, so every sensor
that has ever saved its settings stores `f_max_uns:0, f_max_adv:0`. On the board used
for the hardware check below, stock v1.17.1 reports exactly that. Giving the
constructor the other roles' values (64 and 8) is not enough: the stored zero beats
the default. A sensor with forwarding switched on would have stopped forwarding both
kinds of flood after the update, without any sign.

How far it reaches: forwarding is off by default on the sensor
(`_prefs.disable_fwd = true` in its constructor, as on the room server). So only
sensors where an operator switched forwarding on are affected, and those are exactly
the ones where it matters.

### Why the range check does not resolve it

This change was first parked to wait for Fix 9, the range checking of `/prefs.json`.
Once Fix 9 had landed, its `CommonCLI::sanitisePrefs()` turned out to clamp nineteen
fields but not `flood_max`, `flood_max_unscoped` or `flood_max_advert`. That is
consistent: 0 is a legal value for these limits ("do not forward"), so there is no
range to clamp it into. The reason for parking was an assumption about Fix 9 that
reading it would have refuted.

## What changes

- **The sensor applies both limits.** `allowPacketForward()` calls
  `mesh::isFloodHopLimitExceeded()` like the other roles, and the constructor sets
  `flood_max_unscoped` to 64 and `flood_max_advert` to 8. After it,
  `allowPacketForward()` is the same text in the room server and the sensor.
- **`/prefs.json` has a version.** `NodePrefs::prefs_version` is written as the first
  key, `ver`. A file without it reads as version 0. `NODE_PREFS_VERSION` is 1.
- **`ServerMesh::begin()` upgrades an older file once.** Right after `loadPrefs()`,
  and only when the stored version is older than `NODE_PREFS_VERSION`, it calls the
  new hook `upgradePrefs(from)`, records the current version, and saves at once if
  the hook changed a value. Otherwise booting stays read only, as before. A newer
  stored version, which a downgrade meets, is left as it is.
- **The sensor's upgrade step.** From version 0, a stored `flood_max_unscoped` of 0
  becomes 64 and a stored `flood_max_advert` of 0 becomes 8. A value other than 0 was
  set on purpose and is kept. The repeater and the room server do not override the
  hook: on those roles a stored 0 has always been applied, so it was a choice.

The version is what makes the lift run once. Without it, a 0 that an operator sets
on purpose after the update would be lifted again at every boot.

## Alternatives

- **Ship it and document it** ("run `set flood.max.unscoped 64` after the update").
  Rejected: a node whose operator misses the note stops forwarding without a sign.
- **Leave the sensor ignoring the two limits.** Rejected: it keeps the sensor apart
  from the other roles for good, and `allowPacketForward()` could not be shared.
- **A one-off marker for these two fields** instead of a version. Rejected: the next
  setting whose meaning changes would need another marker; a version takes any later
  step.
- **Upgrade inside `CommonCLI::loadPrefs()`.** Rejected: the CLI does not know the
  role, and only the role knows which stored values meant something else before.
- **Lift a stored 0 at every boot, without a version.** Rejected: it takes away a
  setting the other roles offer.

## Compatibility

- **Older firmware ignores `ver`.** `ConfigSerializer::loadSerial()` only takes the
  keys its `structure()` names. Shown on the board for stock v1.17.1 and for a v1.18
  build without the version (below).
- **A downgrade that saves drops the key**, and the next upgrade runs the step again.
  For the sensor that is harmless: the older firmware never applied these limits, so
  a 0 it stored was never chosen either.
- **The file of every server role gains one key.** The pinned `prefs.json` of
  `test_common_node_prefs` changes by exactly `ver:1,` in front.

## Verification

### Tests first

The forwarding cases and the three measurements of the hazard were written against
the old code. The upgrade cases live in `test/mocks/BootSequenceChecks.h`, so the
repeater, the room server and the sensor all run them; a suite that defines
`BOOT_ROLE_LIFTS_UNVERSIONED_ZERO_HOP_LIMITS` (the sensor's) gets the lift cases, the
others get "a stored 0 is kept and the file is not rewritten".

With the version field in place but no upgrade, 7 tests failed, 14 cases across the
environments, where expected: the sensor lift, the version after boot and after save,
the pinned `prefs.json`. With the upgrade, all pass.

| suite | new or changed cases |
|---|---|
| `BootSequenceChecks.h`, all three roles | a fresh node is at the current version and writes nothing; a stored file gets the version with the next save; an older file that needs nothing is not rewritten; the role is asked to upgrade only an older file; zeros stored at the current version are kept; a newer version is left as it is |
| same, sensor only | unversioned zeros are lifted to 64 and 8; the lift is saved with the version; only a 0 is lifted; a 0 set after the upgrade survives the next boot |
| same, repeater and room server | unversioned zeros are kept, file untouched |
| `test_sensor_role` | both limits applied; an upgraded sensor goes on forwarding floods and adverts; the step acts only from version 0 |
| `test_common_node_prefs` | the pinned file with `ver` in front; a v1.18.0 file reads as version 0; the version is read back |

`pio test -e native -e native_kiss_modem -e native_core -e native_ofast -e native_server -e native_sensor -e native_repeater -e native_repeater_full`:
251 of 251 cases pass. Merged onto the `integration` of 20 October (2e456f74, which
also carries Fix 8, Fix 9 and issue 10) in a trial merge without conflicts, all 13
environments of the CI test run: 307 of 307.

### Mutation tests

Each against the committed change, the tree restored by copy after every run and
checked clean at the end. `pio test -e native -e native_sensor -e native_server -e native_repeater`.

| # | mutation | caught by |
|---|---|---|
| M1 | no save after the hook changed a value | the lift is saved with the version (sensor) |
| M2 | upgrade also at the current version (`<=`) | the role is asked to upgrade only an older file (all roles) |
| M3 | current version not recorded | fresh node at current version, version after save, a 0 set after the upgrade survives, lift saved with version |
| M4 | save after every upgrade, changed or not | boot writes nothing on an empty file system, an older file is not rewritten, the repeater's and room server's file untouched (14 cases) |
| M5 | a newer version is lowered (`!=`) | a newer version is left as it is; the role is asked only for an older file |
| M6 | the sensor lifts any value, not only 0 | only a 0 is lifted, and four boot cases that see an unexpected save |
| M7 | the sensor's step also runs from version 1 | the step acts only from version 0 |
| M8 | the sensor reports no change | the lift is saved; the step acts only from version 0 |
| M9 | `ver` not persisted | the pinned file, the version read back, and six boot cases |

All nine are caught, each by a different set of tests. In the first round M2 and M7
survived: each was masked by the other guard (`begin()` only asks for older versions,
the sensor's step only acts from version 0). Neither was an equivalent mutant; a test
for each guard on its own closed both.

### Build matrix

The 18 environments of the series, built on this change and on its base (step 4f,
`78e7c02f`) with the same toolchain: 36 of 36 succeed.

| | RAM | flash |
|---|---|---|
| sensors (Heltec v3, RAK 4631, wio-e5-mini, ProMicro) | +0 | +168 to +208 |
| repeaters and room servers (all others) | +0 | +72 to +128 |
| companions (four environments) | +0 | +0 |

The companion numbers are equal sizes, not a byte comparison: `ServerMesh` is not part
of a companion image (shown for 4a to 4e in 0004). Why RAM stays the same with one
more byte in `NodePrefs` was not examined (padding is the likely reason, unverified).

GCC, which this machine does not have: the fork's CI ran the host suites and fourteen
firmware builds on this change, 15 of 15 checks green.

### Hardware

`techo-b43b`, a T-Echo running stock simple_sensor v1.17.1, with marked images
(`FIRMWARE_BUILD_DATE`) and the diagnostic patch from `tools/refactoring` for the
I2C bus that a soft reset can leave hanging (ledger issue 14). Values read with
`tools/refactoring/scripts/server_gets.txt`, 31 lines.

| step | image | result |
|---|---|---|
| 0 | stock v1.17.1 | `flood.max.unscoped` 0, `flood.max.advert` 0, forwarding off; `set flood.max 64` (unchanged value) to make sure the file is written |
| 1 | this change (`fm-3b58ebd7`) | the two limits read 64 and 8; the other 28 settings identical to step 0 |
| 2 | same, after `reboot` | identical to step 1 |
| 3 | same, `set flood.max.unscoped 0`, `reboot` | the 0 stays, so the version reached the flash and the step did not run again; everything else identical |
| 4 | step 4f without this change (`fmbase-78e7c02f`) | all 30 settings identical to step 3: a v1.18 build without the version reads the file |
| 5 | stock v1.17.1 | all 30 settings identical to step 3: the downgrade reads the file with `ver` in it |
| 6 | stock v1.17.1, `set flood.max.advert 0` | all 31 lines identical to step 0, the board is back where it started |

The first try at step 5 met the hanging I2C bus: after the DFU the stock image stayed
silent. Step 4 was the remedy (an image that frees the bus), then the stock image a
second time. Which is why step 4 exists at all; it is useful as a data point anyway.

Forwarding itself was not exercised on the air: the board is not to forward in the
real mesh. That an upgraded sensor goes on forwarding is shown by the host tests; the
board shows the values it forwards with.

## What was not verified

- The upgrade on any board other than the T-Echo, and on a sensor with forwarding
  switched on (the step does not read `disable_fwd`, and the host tests cover both).
- Whether the ESP32 and RP2040 file systems behave like the nRF52's here; the code
  path is the same `ConfigSerializer` on all of them.
