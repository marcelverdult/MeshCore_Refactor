# /prefs.json was loaded without a range check

## Context

Server roles read their settings from `/prefs.json`:

```cpp
if (file) {
  _prefs->loadSerial(file);   // new Serial prefs
  file.close();
}
```

That was the whole of it (`src/helpers/CommonCLI.cpp:31-41` on `upstream/dev`
428a95c6). Whatever the file said became the running configuration, and the
file is plain text on a filesystem a user can write. The values reach the radio
setup directly: `freq`, `bw`, `sf`, `cr`, `tx_power_dbm`, the delay factors and
the bridge settings.

The binary loader right below it tells a different story. It ends with a block
headed `// sanitise bad pref values` and about two dozen `constrain()` calls
(`:108-137`), written in `01d84d5d` on 2025-03-05 with the commit line "Now
sanitise bad prefs values". The move to `/prefs.json` did not carry it over, so
the protection depended on which file a node happened to boot from. Since 1.17
that is always `/prefs.json`, so in practice it was gone.

## Decision

Both loaders call the same code. The block moved into
`CommonCLI::sanitisePrefs()`, the binary loader calls it where the block used to
sit, and the JSON path calls it after `loadSerial()`.

Values that are not finite numbers are handled one layer down, in
`ConfigSerializer`: a token that does not parse to a finite float or double
leaves the field alone. `loadPrefs()` also logs it when `loadSerial()` reports a
parse error; that result used to be discarded.

### Why non-finite values cannot be clamped

`constrain()` cannot catch a NaN. Measured on the host:

| token | `atof` | `constrain(v, 0, 9)` |
|---|---|---|
| `nan` | nan | **nan**, passed through |
| `-nan` | nan | **nan**, passed through |
| `inf` | inf | 9 |
| `1e40` | inf | 9 |
| `abc` | 0 | 0 |

Both comparisons inside `constrain()` are false for a NaN, so it returns the
NaN unchanged. Infinity is clamped, NaN is not.

Clamping a NaN to a bound has no sensible answer either. The firmware sets its
defaults before it reads the file (for the repeater at
`examples/simple_repeater/MyMesh.cpp:891` onwards, with `loadPrefs()` at
`:949`), so refusing the assignment keeps the build default, which is the right
value. Clamping to the nearest bound would put a node whose file says
`freq:nan` on 150 MHz.

The check reads the bits rather than comparing, because nRF52 builds use
`-Ofast`: `-ffast-math` lets float comparisons assume that NaN and infinity
never occur. For floats it runs after the narrowing, since a value that is
finite as a double can overflow to infinity as a float.

### The one limit that changed

`airtime_factor` is clamped to 0..99, not to the 0..9 of the old block.
`set dutycycle 1` stores `100/1 - 1 = 99`, a value `set af` itself refuses.
The limit of 9 predates that command by a year (`01d84d5d` 2025-03-05 against
`0aa0ec1f` 2026-03-07), so it was never adjusted for it, and keeping 9 would
throw away every duty cycle below 10 percent at the next boot. Verified on the
board: `set dutycycle 1` gives `get af` `> 99.0`.

`set tx` and `set multi.acks` have no check in the CLI at all. For those the
loader's limit is the only guard, which is its job, so they keep the values the
binary loader used.

## Alternatives considered

- **Derive every limit from the CLI checks.** Rejected. The CLI validates the
  value a user types while the file stores a scaled one: `advert_interval` is
  minutes divided by two and `agc_reset_interval` is seconds divided by four,
  so the mapping would have to be written by hand per field. It would also
  newly clamp values the binary loader accepted. The one place where CLI and
  loader genuinely disagree is bandwidth, and there the loader is right: the
  CLI takes anything from 7.0 while 7.8 kHz is the smallest bandwidth the
  radios offer.
- **Clamp a NaN to the nearest bound.** Rejected, see above.
- **Reject the whole file when it does not parse.** Not done. A partially
  parsed file is still applied, as before, and now sanitised. Refusing it would
  be a second behaviour change, and the right answer (keep defaults, or keep
  what parsed) is a product decision.
- **Leave `airtime_factor` out of the block.** Rejected. It would be the only
  float without a range check, and a file saying `af:1000000` would come
  through untouched.

## Compatibility

- The binary loader keeps its behaviour except for `airtime_factor`, which it
  now leaves alone up to 99 instead of cutting it to 9.
- On the `/prefs.json` path a value outside the limits is clamped at boot where
  it used to be taken as is. That is the point of the change.
- A file holding a non-finite number leaves that one field at its default
  instead of poisoning it. Every other key in the file is still applied.
- The `ConfigSerializer` change also covers the companion `NodePrefs`, which
  uses the same serializer.

## Verification

### Host tests

Five new cases in `test_config_serializer`. Three are red on `upstream/dev`
428a95c6 and green with the change: a file with `nan`, a file with `inf` and
`-inf`, and a float that overflows to infinity. Two pin behaviour that must not
change: finite values are stored, and garbage still parses as zero.

`pio test -e native -e native_kiss_modem` reports 59 of 59. `native_core` and
`native_ofast` do not exist on `upstream/dev`; they run on `integration` after
the merge.

`sanitisePrefs()` itself has **no host test**: no native environment on this
branch compiles `CommonCLI.cpp`, and building that scaffolding would duplicate
what the refactoring steps already added on `integration`. It is covered by the
build of every role and by the board run below, which is the check the task
asked for.

### Mutations

Five deliberate errors in the parser check, each had to turn the suite red:

| Mutation | Caught by |
|---|---|
| float path stores without checking | all three non-finite cases |
| double path stores without checking | the `nan` and the `inf` case |
| float path checks before the narrowing | the overflow case |
| bit comparison inverted | all five cases |
| doubles use the float mask | the `nan` and the `inf` case |

### Builds

All 20 environments build on this commit, and so does `upstream/dev` 428a95c6
in its own worktree with the same toolchain. The 18 of the review matrix are
joined by `LilyGo_T-Echo_room_server`, the build the board run used, and
`LilyGo_T-Echo_kiss_modem`.

| Environment | Build | RAM against dev | Flash against dev |
|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | OK | +0 | +28 |
| `Heltec_v3_companion_radio_wifi` | OK | +0 | +24 |
| `Heltec_v3_repeater` | OK | +0 | +44 |
| `Heltec_v3_room_server` | OK | +0 | +40 |
| `Heltec_v3_sensor` | OK | +0 | +40 |
| `LilyGo_T-Echo_kiss_modem` | OK | +0 | +0 |
| `LilyGo_T-Echo_room_server` | OK | +0 | +48 |
| `PicoW_repeater` | OK | +0 | +40 |
| `ProMicro_companion_radio_usb` | OK | +0 | +32 |
| `ProMicro_repeater` | OK | +0 | +48 |
| `ProMicro_room_server` | OK | +0 | +48 |
| `ProMicro_sensor` | OK | +0 | +48 |
| `RAK_3112_repeater_bridge_espnow` | OK | +0 | +44 |
| `RAK_4631_companion_radio_ble` | OK | +0 | +16 |
| `RAK_4631_repeater` | OK | +0 | +48 |
| `RAK_4631_room_server` | OK | +0 | +48 |
| `RAK_4631_sensor` | OK | +0 | +48 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | OK | +0 | +40 |
| `wio-e5-mini_repeater` | OK | +0 | +32 |
| `wio-e5-mini_sensor` | OK | +0 | +40 |

RAM is unchanged in all 20. Flash grows by 0 to 48 bytes, and the spread lines
up with what each build actually compiles, which is a useful check that the
change lands where it should:

- The server roles pay the most (40 to 48 bytes): they get both the bit check in
  the serializer and the second call site of the sanitising block.
- The companion builds pay 16 to 32 bytes. They have their own `NodePrefs` and
  never call `CommonCLI::sanitisePrefs()`, so only the serializer change
  reaches them.
- `LilyGo_T-Echo_kiss_modem` pays nothing. Its `build_src_filter` adds only
  `examples/kiss_modem/`, so it compiles neither loader and references neither
  change.

The sanitising block itself only moved; it was already linked into every build
that has `CommonCLI.cpp`.

### Hardware

techo-b43b, `LilyGo_T-Echo_room_server`, both images marked and each marker
confirmed in the image before flashing and reported by `ver` on the board.

The out of range values were produced with the CLI itself, through the two
setters upstream does not check, and through the NaN hole in `set af` that this
branch does not touch:

| reading | `dev-428a95c6` | `range-check-bc5708f7` |
|---|---|---|
| `get multi.acks` after `set multi.acks 5`, reboot | `> 5` | `> 1` |
| `get af` after `set af nan`, reboot | `> 0` (the field holds a NaN) | `> 1.0` (build default) |
| `get dutycycle` | `> 0.0%` | `> 50.0%` |
| `get tx` | `> 22` | `> 22` |
| `get txdelay` | `> 0.8` | `> 0.8` |

Same board, same file: the out of range value is clamped, the NaN falls back to
the default the firmware set before reading, and the two values that were
inside their limits are untouched.

## Follow-ups

- `isFinite` now exists twice in the tree with the same shape, here and in
  `CommonRadioPrefs.cpp` from the airtime factor fix. Worth unifying once both
  are on `integration`.
- `extra_sf` is not in the block. Its values are validated where they are
  applied (`CustomLR2021Wrapper.h:37-39`, against the primary spread factor),
  and that check depends on `sf`, so a loader side limit would have to be
  recomputed whenever `sf` changes.
- CLI and loader still disagree about bandwidth (7.0 against 7.8) and bridge
  channel (1..14 against 0..14). Both are harmless, and unifying them is a
  change to the CLI, not to the loader.
