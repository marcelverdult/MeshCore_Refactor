# 0001: Share the radio and GPS settings between both NodePrefs

Status: ready for review. Behaviour change: none.

## Context

MeshCore has two classes called `NodePrefs`:

- `src/helpers/CommonCLI.h` (now `src/helpers/CommonNodePrefs.h`): the settings of
  the repeater, room server and sensor, edited through `CommonCLI`.
- `examples/companion_radio/NodePrefs.h`: the settings of the companion radio.

Both persist themselves as `/prefs.json` through `ConfigSerializer`, and both
declared the same 17 radio and 3 GPS fields. Each also had a private `RadioPrefs`
class that implemented the 34 pure virtual getters and setters of
`CommonRadioPrefs` by forwarding to its parent, and a private `GPSPrefs` class.
The companion copies were marked "COPIED from CommonCLI (for now)".

Copies drift. In this release cycle the companion copy needed three fixes for
defects the server copy did not have:

- `6dad3d5a`: `setSpreadFactor()` evaluated `_parent->sf;` without assigning it.
- `e3d0f9fc`: `isCadEnabled()` returned `false` and `setCadEnabled()` did nothing.
- `5d82ed35`: `setIntThresh()`, `setAgcResetInt()`, `setFloodTxDelay()`,
  `setDirectTxDelay()` did nothing, `getFloodTxDelay()` returned a constant.

Adding one radio setting meant touching both classes, both forwarding classes and
the abstract interface.

## Decision

1. `RadioPrefsFields` (in `CommonRadioPrefs.h`) and `GPSPrefsFields` (in the new
   `CommonGPSPrefs.h`) declare the radio and GPS fields once, with type, default
   and unit. The fields are ordered by size so the structs contain no padding.
2. Both `NodePrefs` classes inherit these two structs instead of declaring the
   fields themselves.
3. `CommonRadioPrefs` becomes the one concrete binding of a `RadioPrefsFields`:
   it owns the `radio` JSON object, the radio CLI commands and the key/value
   access boards use. It is constructed with the fields it binds to. The 34 pure
   virtuals and both forwarding classes are gone.
4. `CommonGPSPrefs` is the binding for the `gps` JSON object and replaces the two
   identical private `GPSPrefs` classes.

The step is split into two commits so each can be reviewed on its own:

- `refactor(prefs): move server NodePrefs into its own header and pin it with host tests`
  moves the server `NodePrefs` verbatim out of `CommonCLI.h` (which pulls in
  `Mesh.h`, `IdentityStore.h`, `ClientACL.h` and `RegionMap.h` and therefore
  cannot be compiled on the host) and adds the characterization tests.
- `refactor(prefs): share the radio and GPS settings between server and companion NodePrefs`
  is the change itself; the tests from the first commit pass unchanged.

## Alternatives considered

- **Hold the groups as members** (`prefs.radio.freq`). Rejected: it would change
  every access in roles, UI, bridges, boards and downstream forks. Inheriting
  keeps `prefs.freq` working everywhere.
- **A template binding over the parent class** (`RadioPrefsBinding<NodePrefs>`).
  Rejected: it removes the duplicated code but keeps the duplicated fields, and
  the binding would rely on matching member names rather than on a type.
- **Keep `CommonRadioPrefs` abstract** and add one concrete subclass. Rejected:
  with one data layout left there is nothing to vary, so the 34 virtuals would
  stay pure forwarding.

## Compatibility

- `/prefs.json`: unchanged byte for byte (same keys, same order, same number
  formatting). The `def()` order, not the field order, defines the file.
- CLI: unchanged replies and side effects.
- Legacy binary prefs (`/com_prefs`, `/new_prefs`) are read field by field, so
  the changed in-memory layout does not matter to them. No code copies a
  `NodePrefs` as a raw block.
- Source compatibility: every `#include <helpers/CommonCLI.h>` keeps working
  (it includes `CommonNodePrefs.h`), and every field keeps its name and type.
  `CommonRadioPrefs`, introduced in this release cycle, is now a concrete class;
  code that subclassed it to implement the pure virtuals has to drop that
  subclass and construct it from a `RadioPrefsFields`.

## Verification

### Host tests

`test/test_common_node_prefs` (new) and `test/test_companion_node_prefs` (until
now only a disabled case) run the same checks against both classes, shared
through `test/mocks/RadioPrefsChecks.h`:

- the exact `/prefs.json` text for a fully populated `NodePrefs`,
- loading that text restores every persisted field,
- every radio CLI command: reply text, the field it writes, that an accepted
  command marks the prefs dirty and a rejected one does not, and the TX power /
  RX gain push to the radio driver (`test/mocks/target.h` records the calls),
- FEM gain access through the `custom` key/value store.

The expected values were captured from the code before the change. The tests
pass on both commits.

The tests were checked against deliberately broken code (mutation tests), each
of which must fail:

| Mutation | Result |
|----------|--------|
| swap the `sf` and `cr` JSON keys | 2 tests fail |
| `setFloodTxDelay()` writes the direct delay | 2 tests fail |
| `setAirtimeFactor()` does not mark the prefs dirty | 2 tests fail |
| swap the GPS `en` and `int` JSON keys | 4 tests fail |

### Builds

18 environments covering every role and platform were built from `dev` (`428a95c6`)
and from each commit of this step, with the same toolchain:

- All 18 build on both commits.
- The first commit (move and tests) produces firmware of exactly the same flash
  and RAM size as `dev`, as expected for a verbatim move.
- The second commit makes every firmware smaller and leaves RAM unchanged
  (the forwarding classes and their 34 virtual methods are gone; the field
  order keeps the structs free of padding):

| Environment | Platform | Flash `dev` | Flash this step | Flash delta | RAM delta |
|---|---|---:|---:|---:|---:|
| `Heltec_v3_repeater` | ESP32-S3 | 1,174,449 | 1,173,485 | -964 | +0 |
| `Heltec_v3_room_server` | ESP32-S3 | 1,169,705 | 1,168,769 | -936 | +0 |
| `Heltec_v3_companion_radio_ble` | ESP32-S3 | 1,666,437 | 1,665,533 | -904 | +0 |
| `Heltec_v3_companion_radio_wifi` | ESP32-S3 | 1,102,677 | 1,101,745 | -932 | +0 |
| `Heltec_v3_sensor` | ESP32-S3 | 1,180,593 | 1,179,633 | -960 | +0 |
| `RAK_4631_repeater` | nRF52840 | 440,952 | 440,456 | -496 | +0 |
| `RAK_4631_room_server` | nRF52840 | 434,088 | 433,576 | -512 | +0 |
| `RAK_4631_companion_radio_ble` | nRF52840 | 462,548 | 462,068 | -480 | +0 |
| `RAK_4631_sensor` | nRF52840 | 437,544 | 437,016 | -528 | +0 |
| `PicoW_repeater` | RP2040 | 491,972 | 491,316 | -656 | +0 |
| `wio-e5-mini_repeater` | STM32WL | 203,552 | 203,000 | -552 | +0 |
| `wio-e5-mini_sensor` | STM32WL | 202,904 | 202,336 | -568 | +0 |
| `RAK_3112_repeater_bridge_espnow` | ESP32-S3 | 1,175,717 | 1,174,761 | -956 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | RP2040 | 240,368 | 239,720 | -648 | +0 |
| `ProMicro_repeater` | nRF52840 | 345,424 | 344,912 | -512 | +0 |
| `ProMicro_room_server` | nRF52840 | 338,456 | 337,960 | -496 | +0 |
| `ProMicro_companion_radio_usb` | nRF52840 | 357,300 | 356,820 | -480 | +0 |
| `ProMicro_sensor` | nRF52840 | 341,900 | 341,372 | -528 | +0 |

### Hardware

Run on two LilyGo T-Echo boards (nRF52840, SX1262), one per `NodePrefs` class,
starting from real data written by v1.17.1:

| Board | Firmware role | Settings class |
|-------|---------------|----------------|
| T-Echo A | room server (`LilyGo_T-Echo_room_server`) | server roles' `NodePrefs` |
| T-Echo B | companion radio over USB (`LilyGo_T-Echo_companion_radio_usb`) | companion `NodePrefs` |

Sequence on each board:

1. Flash `dev`. It migrates the v1.17.1 binary prefs to `/prefs.json`; all radio
   values read back as before.
2. With `dev`, change 11 to 15 settings through the CLI (room server: serial
   console; companion: `CMD_RUN_CLI_COMMAND` frames), reboot, read them back.
3. Flash this step. Read all settings again: identical to step 2.
4. With this step, change the settings to other values, reboot, read back: all
   changes persisted.
5. Flash `dev` again. Read all settings: identical to step 4.

Step 1 ran once per board. Steps 2 to 5 ran on the final revision of this step,
after an earlier run on a previous revision of it had passed the same way.

The room server reads 30 values (`get ...` for every radio setting plus name,
role, location, advert and flood settings, owner info); the companion reads 16
values plus the `APP_START` self info and the tuning parameters. Every
comparison was identical except for the version line. To prove which firmware
was running, the board images were built with `FIRMWARE_BUILD_DATE` set to the
commit (for example `ver` answered `v1.18.0 (Build: B-a59561ff)`); this define is
the only difference to the images in the build table.

Not covered on hardware: the GPS commands (the T-Echo room server has no GPS
toggle; the host tests cover the `gps` JSON object), the repeater and sensor
roles (same class as the room server, built only), the BLE and WiFi companions,
and the ESP32, RP2040 and STM32 platforms (built only).

## Follow-ups (not in this step)

- One table per settings group (name, type, range, JSON key, CLI key, roles), so
  a new setting is one row; the CLI could then answer "unsupported" for settings
  a role does not use instead of accepting them silently.
- Let the core read its settings from the groups instead of a virtual getter per
  setting in every role.
- Remove the `target.h` include from `CommonRadioPrefs.cpp` by giving it the radio
  driver through an interface.
