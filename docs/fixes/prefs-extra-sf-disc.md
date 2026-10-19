# extra_sf and the discovery timestamp were never persisted

## Context

`NodePrefs` holds two fields that no loader and no saver ever touched:

| Field | Written by | Read by |
|---|---|---|
| `discovery_mod_timestamp` | `src/helpers/CommonCLI.cpp:914`, whenever the node is modified | `examples/simple_repeater/MyMesh.cpp:806` and `examples/simple_sensor/SensorMesh.cpp:730`, to answer "has this node changed since" |
| `extra_sf[4]` | `src/helpers/CommonCLI.cpp:687`, the `extra.sf` command | `:826`, to print the value back |

Neither appears in `NodePrefs::structure()` or in any of its sub objects
(`src/helpers/CommonCLI.h:209-225` on `upstream/dev` 428a95c6), so both are lost
on every reboot.

For `discovery_mod_timestamp` that is a regression rather than an oversight. The
binary loader still reads it, at offset 162 (`src/helpers/CommonCLI.cpp:97`), so
the field survived as long as a node booted from `/com_prefs`. Since 1.17 every
node boots from `/prefs.json`, which means every server role has been reporting
itself as never modified since then.

`extra_sf` was never persisted in either format.

## Decision

Both fields are serialised:

- `discovery_mod_timestamp` as `disc_mod`, at the top level next to the advert
  intervals, which is where it belongs: it is a property of the node, not of its
  radio.
- `extra_sf` as `extra_sf` inside the `radio` object, hex encoded through the
  `def(key, void*, len)` overload. That overload exists for exactly this, a
  fixed size byte array, and the companion already uses it for its default scope
  key (`examples/companion_radio/NodePrefs.h:159`).

The constructor zeroes `extra_sf`. It is a plain array member with no
initialiser, and now that it is written out and read back it needs a defined
starting value.

### Why both keys are written on every board

`extra.sf` is compiled in only for `USE_LR2021` boards and implemented only by
the repeater, so most builds can never set the field. The keys are written
anyway, which keeps `/prefs.json` portable between roles and boards. That is
also how the bridge settings are handled: `BridgePrefs::structure()` writes its
six keys unconditionally although the feature sits behind `WITH_BRIDGE`.

## Alternatives considered

- **Four separate `uint8` keys, or a comma separated string.** Rejected. The
  hex blob is the overload the serializer already offers for a fixed size array,
  and it is what the one precedent in the tree uses.
- **Guard the keys with `#if defined(USE_LR2021)`.** Rejected. It would make the
  shape of `/prefs.json` depend on the board, which is exactly what the
  downgrade test relies on not happening, and it would differ from how the
  bridge keys are treated.
- **Also apply the side detectors at boot.** Out of scope here and reported
  separately, see the follow-up below. It is a behaviour change in the repeater
  boot path, which by the rules of this series belongs in its own branch.
- **Sanitise the two new fields in the range check.** Not done. A timestamp has
  no meaningful range, and the spread factors are validated where they are
  applied, against the primary spread factor
  (`src/helpers/radiolib/CustomLR2021Wrapper.h:37-39`), so a limit in the loader
  would have to be recomputed whenever `sf` changes.

## Compatibility

- **Upgrade.** A `/prefs.json` without the two keys loads as before: the
  serializer leaves a field untouched when its key is absent, so both keep
  whatever the firmware put there.
- **Downgrade.** A firmware that does not know the keys skips them, the way it
  skips any unknown key.
- The file grows by one number and one eight character string.

## Verification

### Host tests

New environment `native_node_prefs` covers the serialisation of the real
`NodePrefs` against stream doubles. `NodePrefs` lives in `CommonCLI.h`, whose
filesystem types only exist for a platform, so the environment selects the nRF52
branch and takes the types from a mock filesystem.

Six of the nine cases are red on `upstream/dev` 428a95c6 and green with the
change: both save cases, both load cases, the round trip, and the case that
mixes the new keys with unknown ones. The other three pin what must not change:

| Case | Pins |
|---|---|
| `ExtraSfStartsZeroed` | a fresh `NodePrefs` starts from a defined `extra_sf` |
| `LoadWithoutTheNewKeysLeavesThemAlone` | the upgrade path, and that the keys which are present still arrive |
| `UnknownKeysAreSkipped` | the downgrade path, from the reading side |

`ExtraSfStartsZeroed` is listed as pinning rather than as red, because before
the change the array is uninitialised: the result there is undefined, not
reliably a failure.

`pio test -e native -e native_kiss_modem -e native_node_prefs` reports 62 of 62.

### Mutations

Five deliberate errors, each had to turn the suite red:

| Mutation | Caught by |
|---|---|
| `extra_sf` not serialised | both `extra_sf` cases, the round trip, the unknown key case |
| `disc_mod` not serialised | both timestamp cases, the round trip, the unknown key case |
| blob one byte short | the save and load of `extra_sf`, the unknown key case |
| constructor fills `extra_sf` with ones | `ExtraSfStartsZeroed` |
| timestamp under a different JSON key | both timestamp cases, the unknown key case |

### Builds

All 20 environments of the matrix build on this commit, and so does
`upstream/dev` 428a95c6 in its own worktree with the same toolchain.
`Heltec_v3_terminal_chat` was built as well, to show the one role with its own
prefs structure is untouched.

| Environment | Build | RAM against dev | Flash against dev |
|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | OK | +0 | +0 |
| `Heltec_v3_companion_radio_wifi` | OK | +0 | +0 |
| `Heltec_v3_repeater` | OK | +0 | +188 |
| `Heltec_v3_room_server` | OK | +0 | +152 |
| `Heltec_v3_sensor` | OK | +0 | +176 |
| `LilyGo_T-Echo_kiss_modem` | OK | +0 | +0 |
| `LilyGo_T-Echo_room_server` | OK | +0 | +224 |
| `PicoW_repeater` | OK | +0 | +184 |
| `ProMicro_companion_radio_usb` | OK | +0 | +0 |
| `ProMicro_repeater` | OK | +0 | +208 |
| `ProMicro_room_server` | OK | +0 | +224 |
| `ProMicro_sensor` | OK | +0 | +208 |
| `RAK_3112_repeater_bridge_espnow` | OK | +0 | +192 |
| `RAK_4631_companion_radio_ble` | OK | +0 | +0 |
| `RAK_4631_repeater` | OK | +0 | +208 |
| `RAK_4631_room_server` | OK | +0 | +208 |
| `RAK_4631_sensor` | OK | +0 | +208 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | OK | +0 | +184 |
| `wio-e5-mini_repeater` | OK | +0 | +184 |
| `wio-e5-mini_sensor` | OK | +0 | +176 |
| `Heltec_v3_terminal_chat` | OK | +0 | +0 |

RAM is unchanged in all 21. Flash splits cleanly along which builds use this
`NodePrefs`, which is a useful check that the change lands exactly where it
should:

- The 15 server role builds, every repeater, room server and sensor, grow by 152
  to 224 bytes. That is the hex encoding and decoding of the blob plus the
  second key.
- The other 6 are byte identical to dev: the four companion builds, which have
  their own `NodePrefs` in `examples/companion_radio/NodePrefs.h`,
  `LilyGo_T-Echo_kiss_modem`, which adds only `examples/kiss_modem/` to its
  sources, and `Heltec_v3_terminal_chat`, whose
  `examples/simple_secure_chat` keeps a `NodePrefs` struct of its own.

So no build that cannot reach these fields pays for them.

### Hardware

techo-b43b as a room server, both images marked and each marker confirmed in the
image before flashing and reported by `ver` on the board. Ten settings were read
back at each step.

**Upgrade.** Values set on `dev-428a95c6`, which writes a `/prefs.json` without
the new keys, then rebooted, then the new build flashed onto the same file:

| | `dev-428a95c6` | `extra-sf-disc-wt` |
|---|---|---|
| `get af` | `> 3.0` | `> 3.0` |
| `get dutycycle` | `> 25.0%` | `> 25.0%` |
| `get txdelay` | `> 0.6999999` | `> 0.6999999` |
| `get direct.txdelay` | `> 0.3999999` | `> 0.3999999` |
| `get flood.max` | `> 32` | `> 32` |
| `get multi.acks` | `> 1` | `> 1` |
| `get allow.read.only` | `> on` | `> on` |
| `get name` | `> T-Echo Sensor` | `> T-Echo Sensor` |
| `get radio` | `> 869.6179809,62.5,8,8` | `> 869.6179809,62.5,8,8` |
| `get tx` | `> 22` | `> 22` |

**Downgrade.** Then other values set on the new build and rebooted, so the file
was rewritten with both new keys, and `dev-428a95c6` flashed back onto it:

| | `extra-sf-disc-wt` | `dev-428a95c6` |
|---|---|---|
| `get af` | `> 4.0` | `> 4.0` |
| `get dutycycle` | `> 20.0%` | `> 20.0%` |
| `get txdelay` | `> 0.8` | `> 0.8` |
| `get direct.txdelay` | `> 0.5` | `> 0.5` |
| `get flood.max` | `> 48` | `> 48` |
| `get multi.acks` | `> 0` | `> 0` |
| `get allow.read.only` | `> off` | `> off` |
| `get name` | `> T-Echo Sensor` | `> T-Echo Sensor` |
| `get radio` | `> 869.6179809,62.5,8,8` | `> 869.6179809,62.5,8,8` |
| `get tx` | `> 22` | `> 22` |

Identical in both directions, `ver` being the only difference.

**What the board does not show.** Neither new field can be observed through the
CLI on this hardware, so the round trip of the values themselves rests on the
host tests:

- `extra.sf` is compiled in only for `USE_LR2021` boards, and `USE_LR2021`
  appears in two variants in the tree, `meshnology_w12` and `meshtracker_x1`.
  Neither was available, and a T-Echo is an SX1262 board.
- `discovery_mod_timestamp` has no CLI reader at all.

What the board does show is the part that only hardware can show: that a file
written by one firmware is read without complaint by the other, in both
directions, with every readable setting intact.

## Follow-ups

- **The side detectors are never applied at boot.**
  `configSideDetectors()` is called from one place only, the `extra.sf` command
  (`src/helpers/CommonCLI.cpp:686`, through
  `examples/simple_repeater/MyMesh.cpp:1087` into the radio driver). No boot path
  calls it, so a persisted `extra_sf` is not put back on the radio after a
  restart. Persisting it is still right, because `get extra.sf` now answers
  truthfully and the value is there to be applied, but the apply is missing and
  wants its own change: it is a behaviour change in the repeater boot path, and
  it cannot be verified on hardware without an LR2021 board.
- The stored spread factors can go stale. `CustomLR2021Wrapper` validates them
  against the primary `sf` when they are applied, so a file that was valid for
  one `sf` can hold values that are refused after `sf` changes.
