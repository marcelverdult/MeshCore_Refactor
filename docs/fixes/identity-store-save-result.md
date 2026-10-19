# IdentityStore::save reported success for a failed write

## Context

`IdentityStore` is the only place that persists a node's keypair. Each role
loads it at boot and, when nothing is there, generates a new one and saves it:

| Role | Generates and saves at boot |
|---|---|
| Repeater | `examples/simple_repeater/main.cpp:94` |
| Room server | `examples/simple_room_server/main.cpp:75` |
| Sensor | `examples/simple_sensor/main.cpp:101` |
| Companion | `examples/companion_radio/MyMesh.cpp:955` |
| KISS modem | `examples/kiss_modem/main.cpp:56` |
| Secure chat | `examples/simple_secure_chat/main.cpp:327` |

Both overloads of `save()` ended in `return true` as long as the file could be
opened. The two argument form computed the result of `id.writeTo(file)`, logged
it and then threw it away (`src/helpers/IdentityStore.cpp:58-61` on
`upstream/dev` 428a95c6). The form that also stores a display name did not even
look at the result of its two writes.

A node whose write fails therefore keeps running under a keypair that never
reached the medium, and comes back with a different public key after the next
boot. Everything that identifies the node to the mesh changes with it.

## Decision

Return what actually happened. In the two argument form that is the result of
`writeTo()`; in the form with a display name both writes count, because the
name is written after the keypair and a medium that fills up in between leaves
a file with an identity and no name.

The form with a display name also gets the debug line the other form already
had, so a failure is visible in both.

### Why the result of a failed write is safe to report

The caller contract already expects an honest result. `CMD_IMPORT_PRIVATE_KEY`
in `examples/companion_radio/MyMesh.cpp:1544` adopts the imported key and
answers `OK` when `save()` reports success, and answers
`ERR_CODE_FILE_IO_ERROR` when it does not. That error path existed before this
change and was unreachable.

All nine call sites were checked for what a `false` now triggers:

| Call site | Reads the result | Effect of a false |
|---|---|---|
| `companion_radio/MyMesh.cpp:1544` (import private key) | yes | answers `ERR_CODE_FILE_IO_ERROR`, keeps the old key. The path already existed |
| `companion_radio/MyMesh.cpp:955` (boot) | no | unchanged |
| `companion_radio/MyMesh.cpp:2245` (`rebuild` command) | no | unchanged |
| `simple_repeater/main.cpp:94` (boot) | no | unchanged |
| `simple_room_server/main.cpp:75` (boot) | no | unchanged |
| `simple_sensor/main.cpp:101` (boot) | no | unchanged |
| `kiss_modem/main.cpp:56` (boot) | no | unchanged |
| `simple_secure_chat/main.cpp:327` (boot) | no | unchanged |
| `simple_repeater/MyMesh.cpp:1189`, `simple_room_server/MyMesh.cpp:858`, `simple_sensor/SensorMesh.cpp:896` (`saveIdentity`) | no | `void` wrappers behind `CommonCLI.h:265`, so they cannot propagate it |

No boot path reads the result, so a failing write adds no error cascade at
startup. That was the condition for this change.

The overload with a display name has no caller in this repository. It is part
of the public interface of the class and carried the same defect, so it is
fixed in the same place rather than left as the one that still lies.

## Alternatives considered

- **Remove the half written file when the write fails.** Not done. A truncated
  `.id` file fails to load (`readFrom()` comes up short), so the node behaves
  as if no file existed, and the next `save()` removes the file first anyway on
  the platforms that take that branch. Deleting it would be a second behaviour
  change in a fix whose point is to stop reporting a false success.
- **Retry the write inside `save()`.** Not done. The caller knows whether a
  retry is useful; `save()` does not. The companion already decides this.
- **Only fix the two argument form, the one with a caller.** Not done. Both
  overloads carry the identical defect in the identical place.
- **Leave the display name write unchecked.** Not done. It is the second half
  of the same file; a file with a keypair and no name is not a saved identity.

## Compatibility

- `/prefs.json`, the identity file format and the success path are unchanged.
  Nothing differs while the filesystem accepts the write.
- The only behaviour change on a failing write is in
  `CMD_IMPORT_PRIVATE_KEY`, which now takes the error path that was written for
  it.

## Verification

### Host tests

New environment `native_identity_store` runs `test/test_identity_store` against
a mock filesystem (`test/mocks/Adafruit_LittleFS.h`) whose file opens and then
runs out of write budget, which is how a full or worn flash behaves.
`Print::write()` loops over the single byte write, so an exhausted budget
returns a short count, exactly as a partial write does on the real driver. The
mock serialisation of `mesh::Identity` and `mesh::LocalIdentity` is copied from
`src/Identity.cpp`, so a short write is a failure in the double as well.

The environment defines `NRF52_PLATFORM`, so the covered branch of `save()` is
the one the nRF52 and STM32 firmware take (`remove()` then
`open(..., FILE_O_WRITE)`). The ESP32 and RP2040 branches differ only in the
`open()` call and are not covered by a host test.

Four of the nine tests are red on `upstream/dev` 428a95c6 and green with the
change, each with `Actual: true, Expected: false`:

| Test | On dev | With the fix |
|---|---|---|
| `IdentityStoreSave.ReportsFailureWhenNoByteCanBeWritten` | red | green |
| `IdentityStoreSave.ReportsFailureWhenThePrivateKeyIsCutOff` | red | green |
| `IdentityStoreSaveWithName.ReportsFailureWhenTheKeypairIsCutOff` | red | green |
| `IdentityStoreSaveWithName.ReportsFailureWhenOnlyTheNameIsCutOff` | red | green |

The other five pin behaviour that must not change: the two success paths, the
two "file cannot be opened" paths and a save and load round trip.

All suites of this branch: `pio test -e native -e native_kiss_modem -e
native_identity_store` reports 63 of 63. `native_core` and `native_ofast` do
not exist on `upstream/dev`; they run on `integration` after the merge.

### Mutations

Five deliberate errors in the changed code, each had to turn the suite red:

| Mutation | Caught by |
|---|---|
| Two argument form returns `true` again (the original defect) | `ReportsFailureWhenNoByteCanBeWritten`, `ReportsFailureWhenThePrivateKeyIsCutOff` |
| Form with display name returns `true` again | `ReportsFailureWhenTheKeypairIsCutOff`, `ReportsFailureWhenOnlyTheNameIsCutOff` |
| Result of the display name write not checked | `ReportsFailureWhenOnlyTheNameIsCutOff` |
| Result of `writeTo()` not read in the two argument form | `ReportsFailureWhenNoByteCanBeWritten`, `ReportsFailureWhenThePrivateKeyIsCutOff` |
| The two writes joined with OR instead of AND | `ReportsSuccessWhenKeypairAndNameFit`, `ReportsFailureWhenOnlyTheNameIsCutOff` |

### Builds

All 21 environments build on this commit, and so does `upstream/dev` 428a95c6 in
its own worktree with the same toolchain. The 18 of the review matrix are joined
by three builds this change reaches through a call site of their own:
`LilyGo_T-Echo_room_server`, the build the board run used,
`LilyGo_T-Echo_kiss_modem` (`examples/kiss_modem/main.cpp:56`) and
`Heltec_v3_terminal_chat` (`examples/simple_secure_chat/main.cpp:327`), the one
role that keeps its own prefs structure and still uses `IdentityStore`.

| Environment | Build | RAM against dev | Flash against dev |
|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | OK | +0 | +4 |
| `Heltec_v3_companion_radio_wifi` | OK | +0 | +4 |
| `Heltec_v3_repeater` | OK | +0 | +4 |
| `Heltec_v3_room_server` | OK | +0 | +4 |
| `Heltec_v3_sensor` | OK | +0 | +4 |
| `LilyGo_T-Echo_kiss_modem` | OK | +0 | +0 |
| `LilyGo_T-Echo_room_server` | OK | +0 | +0 |
| `PicoW_repeater` | OK | +0 | +8 |
| `ProMicro_companion_radio_usb` | OK | +0 | +16 |
| `ProMicro_repeater` | OK | +0 | +16 |
| `ProMicro_room_server` | OK | +0 | +0 |
| `ProMicro_sensor` | OK | +0 | +0 |
| `RAK_3112_repeater_bridge_espnow` | OK | +0 | +4 |
| `RAK_4631_companion_radio_ble` | OK | +0 | +0 |
| `RAK_4631_repeater` | OK | +0 | +0 |
| `RAK_4631_room_server` | OK | +0 | +0 |
| `RAK_4631_sensor` | OK | +0 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | OK | +0 | +8 |
| `wio-e5-mini_repeater` | OK | +0 | +0 |
| `wio-e5-mini_sensor` | OK | +0 | +0 |
| `Heltec_v3_terminal_chat` | OK | +0 | +4 |

RAM is unchanged in all 21 environments. Flash grows by 0 to 16 bytes: the
function returns a value it used to throw away, and the second overload gained a
debug line. The cost does not follow the chip family, it follows what the
compiler did with the old constant return. Both ProMicro server roles come out
at 0 while the ProMicro repeater and the USB companion pay 16, all four being
nRF52840.

### Hardware

techo-b43b, `LilyGo_T-Echo_room_server`, image marked `save-result-b160f39a`
and the marker confirmed in the image before flashing
(`unzip -p firmware.zip '*.bin' | grep -c -a save-result-b160f39a` = 1) and
reported by `ver` on the board.

The node boots on the changed firmware and keeps its settings and its identity
across a reboot, so the success path is intact on real hardware.

The path this change alters, a write that fails, was deliberately **not**
provoked on the board. Reaching it means filling or wearing out the flash, and
the only CLI command that writes an identity is `set prv.key`, which would put
the identity of a node in the test rack at risk for no gain: on healthy
hardware the success path returns true both before and after this change, so
there is nothing to compare. That path is covered by the host tests, where the
mock filesystem lets a file open and then refuses the write.

## Follow-ups

- `src/helpers/IdentityStore.cpp:85` compares a signed `int` with
  `sizeof(tmp)-1`. Left untouched; it is not part of this defect and the
  firmware builds suppress the warning.
