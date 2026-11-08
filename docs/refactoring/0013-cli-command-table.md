# 0013: A command table for the text CLI

Status: 13a (host harness and characterization) built, 13b (the table) not started.
Behaviour change: none. 13a adds tests and build environments only; no file under
`src/` is touched.

## Context

The repeater, room server and sensor are configured over a text CLI, on the serial
console and over the air from an admin. `CommonCLI::handleCommand()`
(`src/helpers/CommonCLI.cpp`) is one chain of about 45 `else if` branches that
compare the start of the command with `memcmp()`, plus `handleSetCmd()` (about 25
branches), `handleGetCmd()` (about 30) and `handleRegionCmd()` (a second parser for
the `region` family). Each branch reads its arguments by hand, writes the reply by
hand and decides for itself whether to save.

That is the same shape step 10 found in the companion protocol, and step 3 already
moved the radio settings out of it into a table
(`src/helpers/CommonRadioPrefs.cpp`). The goal of step 13 is to do the same for the
rest: one row per command, so that a new command cannot forget the length check, the
serial-only gate or the save call. Step 13a only builds the net under it: tests that
pin what the chain does today, so that 13b can be shown to change nothing.

## What 13a adds

Everything is under `test/test_common_cli/`, built by five new native environments
that compile the real `CommonCLI.cpp`, `RegionMap.cpp`, `TransportKeyStore.cpp` and
`CommonRadioPrefs.cpp` on the host. Only the edges are replaced:

- `CliBoard`, `CliClock`, `CliSensors`, `CliLocation`: mocks that count calls and
  return what a test sets.
- `CliCallbacks`: every role callback appends one line to a call log (with the value
  of `advert_interval` at the time for `savePrefs`, so the order of a change and its
  save is visible).
- `identity_stubs.cpp`: link time stand-ins for the key functions of
  `mesh::LocalIdentity`, which need board crypto libraries. They are deterministic and
  not cryptographic; no test asserts a cryptographic property.

`run(cmd, sender_timestamp)` hands the command a zero filled 512 byte buffer and a
160 byte reply buffer prefilled with `<untouched>` and guarded by canary bytes on both
sides, so a command that writes nothing, writes too much or leaves the reply
unterminated is visible.

One source file set is built in five environments with different defines, as
`native_repeater` and `native_repeater_full` do:

| Environment | Defines | What the variant adds |
|---|---|---|
| `native_common_cli` | `RP2040_PLATFORM` | the base: `powersaving on` answers "Board not supported", no `gps` |
| `native_common_cli_nrf52` | `NRF52_PLATFORM`, `NRF52_POWER_MANAGEMENT`, `ENV_INCLUDE_GPS=1`, `USE_LR2021` | `gps ...`, `pwrmgt.*`, `bootloader.ver`, `set extra.sf`, `powersaving on` works |
| `native_common_cli_esp32` | `ESP32` | `powersaving on` answers "on - After 2 minutes" |
| `native_common_cli_rs232` | `RP2040_PLATFORM`, `WITH_RS232_BRIDGE=1` | `bridge.*`, `bridge.baud`, `powersaving on` answers "Bridge not supported" |
| `native_common_cli_espnow` | `RP2040_PLATFORM`, `WITH_ESPNOW_BRIDGE=1` | `bridge.*`, `bridge.channel`, `bridge.secret` |

`[env:native]` ignores the folder, like the other special test folders.

## Inventory

Replies are exact. "Save" means `savePrefs()` (which also zeroes `advert_interval`
when it would be shorter than 60 seconds, see the list of quirks). "ts 0" means the
command only works on the serial console (`sender_timestamp == 0`); over the air it
falls through to the next branch and, in the end, to "Unknown command".

### Order of dispatch

1. `getRadioPrefs()->handleCommand()`: the radio settings table. If it handles the
   command and the radio prefs are dirty, save. Commands: radio, tx, rxdelay, txdelay,
   and so on (pinned in `test/mocks/RadioPrefsChecks.h`; here only the precedence).
2. `board->handleCommand()`: variant hook. If it handles the command and the prefs are
   dirty, save.
3. The chain below, top to bottom. Order matters wherever one command is a prefix of
   another.

### Top level commands

| Command | Arguments | Reply | Effect | Notes |
|---|---|---|---|---|
| `poweroff`, `shutdown` | none | none | `board->powerOff()` | prefix match |
| `reboot` | none | none | `board->reboot()` | prefix match |
| `clkreboot` | none | none | clock set to 1715770351, then `reboot()` | |
| `advert.zerohop` | none | `OK - zerohop advert sent` | `sendSelfAdvertisement(1500, false)` | needs end of string or a space after the word; before `advert` |
| `advert` | none | `OK - Advert sent` | `sendSelfAdvertisement(1500, true)` | prefix match: `advertfoo` also sends |
| `clock sync` | none | `OK - clock set: HH:MM - D/M/Y UTC` or `ERR: clock cannot go backwards` | clock set to sender time + 1, only if it is ahead | over the air only in practice (ts must exceed the clock); before `clock` |
| `start ota` | none | whatever the board writes, or `Error` | `board->startOTAUpdate(name, reply)` | before `clock` |
| `clock` | none | `HH:MM - D/M/Y UTC` | none | prefix match: `clockxyz` also answers |
| `time` | seconds | `OK - clock set: ...` or `(ERR: clock cannot go backwards)` | clock set if ahead | note the brackets, unlike `clock sync` |
| `neighbors` | none | role formats the reply | `formatNeighborsReply` | |
| `neighbor.remove` | hex key | `OK` or `ERR: bad pubkey` | `removeNeighbor(key, len)` | accepts 1 to 32 byte keys; odd length or more than 64 characters is an error |
| `tempradio` | freq,bw,sf,cr,mins | `OK - temp params for N mins` or `Error, invalid params` | `applyTempRadioParams` | limits: freq 150 to 2500, bw 7 to 500, sf 5 to 12, cr 5 to 8, mins above 0 |
| `password` | text | `password now: <text>` | admin password set (15 chars), save | echoes the password |
| `clear stats` | none | `(OK - stats reset)` | `clearStats()` | |
| `get`, `set` | see below | | | routed to `handleGetCmd`, `handleSetCmd` |
| `erase` | none | `File system erase: OK` or `Err` | `formatFileSystem()` | ts 0, whole word only |
| `ver` | none | `<version> (Build: <date>)` | none | prefix match |
| `board` | none | manufacturer name | none | prefix match |
| `power` | none | `batt:N mV usb:.. solar_chg:.. ext:.. charger:..` | none | whole word only; each of usb and solar shows `yes`, `no` or `n/a` |
| `sensor get <key>` | key | `> <value>` or `null` | none | |
| `sensor set <key> [value]` | key, value | `ok` or `can't find custom var` | `setSettingValue` (no save) | missing value is sent as `null`; extra words dropped |
| `sensor list [n]` | start index | `N vars\nname=value\n...` | none | pages with `... next:N`; `no custom var` when empty or past the end |
| `region ...` | see below | | | |
| `gps on`, `gps off` | none | `ok` or `gps toggle not found` | sets the sensor setting and `gps_enabled`, save | GPS builds only |
| `gps sync` | none | `ok` or `gps provider not found` | `syncTime()` | GPS builds only |
| `gps diag` | none | `req:0/1 <provider text>` or `gps provider not found` | none | GPS builds only; whole word only |
| `gps setloc` | none | `ok` | prefs lat/lon from the sensor position, save | GPS builds only |
| `gps advert [none\|share\|prefs]` | policy | `> policy`, `ok`, or `error` | `advert_loc_policy`, save | GPS builds only |
| `gps` | none | `on, active, fix, N sats`, `off`, or `Can't find GPS` | none | GPS builds only; also the catch all for any other `gps...` |
| `powersaving on` | none | by platform: `on - Immediate effect` (nRF52), `on - After 2 minutes` (ESP32), `Bridge not supported`, `Board not supported` | `powersaving_enabled = 1`, save, on the first two | |
| `powersaving off` | none | `off` | `powersaving_enabled = 0`, save | all platforms |
| `powersaving` | none | `on` or `off` | none | |
| `log start`, `log stop`, `log erase` | none | `   logging on`, `   logging off`, `   log erased` | `setLoggingOn`, `eraseLogFile` | three leading spaces |
| `log` | none | `   EOF` | `dumpLogFile()` | ts 0; prefix match |
| `stats-packets`, `stats-radio`, `stats-core` | none | role formats the reply | `format...Reply` | ts 0; end of string or a space after the word |
| anything else | | `Unknown command` | none | |

### `set <key> <value>`

| Key | Reply | Effect | Notes |
|---|---|---|---|
| `allow.read.only` | `OK` | on if the value starts with `on`, save | |
| `flood.advert.interval` | `OK` or `Error: interval range is 3-168 hours` | hours, `updateFloodAdvertTimer`, save | 0 allowed (off); 1 and 2 are errors |
| `advert.interval` | `OK` or `Error: interval range is 60-240 minutes` | stored as minutes / 2, `updateAdvertTimer`, save | 0 allowed; see quirk on zeroing |
| `guest.password` | `OK` | 15 characters, save | no echo |
| `prv.key` | `OK, reboot to apply! New pubkey: <hex>` or `Error, bad key` | `saveIdentity(new id)` | accepted over the air; valid keys only |
| `name` | `OK` or `Error, bad chars` | 31 characters, save | longer names are cut without an error |
| `repeat` | `OK - repeat is now OFF` or `... ON` | `disable_fwd` is on only for a value starting `off`, save | |
| `lat`, `lon` | `OK` | `atof`, save | no validation: text gives 0 |
| `flood.max.unscoped`, `flood.max.advert`, `flood.max` | `OK` or `Error, max 64` | `uint8_t`, save | the cast happens before the check, see quirks |
| `flood.max.type <type> <hops\|off>` | `OK`, `Error, unknown packet type`, `Error, hops 0..64 or off`, or `Error, type list full` | edits `flood_max_types` (stored as `f_max_pt`, for example `6:4,9:3`, sorted by type), save only on `OK` | type by name (`req` to `raw`, see `RoutingPolicy.h`) or number 0 to 15; hops 0 to 64 or `off` to remove; the type is checked first; needs the space after the key; `type list full` cannot be reached |
| `owner.info` | `OK` | `\|` becomes newline, cut at 119 characters, save | |
| `loop.detect` | `OK` or `Error, must be: off, minimal, moderate, or strict` | mode, save | prefix of the word is enough |
| `freq` | `OK - reboot to apply` | `atof`, save | ts 0 |
| `bridge.enabled` | `OK` | `setBridgeState`, save | bridge builds |
| `bridge.delay` | `OK` or `Error: delay must be between 0-10000 ms` | save | bridge builds |
| `bridge.source` | `OK` | `rx` or anything else, save | bridge builds |
| `bridge.baud` | `OK` or `Error: baud rate must be between 9600-N` | `restartBridge`, save | RS232 build |
| `bridge.channel` | `OK` or `Error: channel must be between 1-14` | `restartBridge`, save | ESP-NOW build |
| `bridge.secret` | `OK` | `restartBridge`, save | ESP-NOW build |
| `adc.multiplier` | `OK - multiplier set to X` / `OK - using default board multiplier` / `Error: unsupported` | `setAdcMultiplier`, save | on failure the pref is reset to 0 |
| `extra.sf` | `OK - extra SFs set` or `Invalid extra SF config` | `configSideDetectors`, save | `USE_LR2021` builds only |
| anything else | `unknown config: <text>` | none | echo cut at 142 characters |

### `get <key>`

All replies start with `> `. Keys: `allow.read.only` (on or off),
`flood.advert.interval`, `advert.interval` (minutes), `guest.password`, `prv.key`
(ts 0, uppercase hex), `name`, `repeat`, `lat`, `lon`, `flood.max.type`
(`> grpdata:4 trace:3`, `> none` when empty, unnamed types as numbers), `flood.max.advert`,
`flood.max.unscoped`, `flood.max`, `owner.info` (newline shown as `|`, cut at 159
characters), `loop.detect`, `public.key` (uppercase hex), `role`, `bridge.type`
(`rs232`, `espnow` or `none`), `bridge.enabled|delay|source|baud|channel|secret`
(bridge builds), `bootloader.ver` (nRF52: version or `> unknown`; others
`Error: unsupported`), `adc.multiplier` (`> %.3f` or `Error: unsupported`),
`pwrmgt.support|source|bootreason|bootmv` (without `NRF52_POWER_MANAGEMENT`:
`> unsupported` or `ERROR: Power management not supported`; bootreason answers on
all builds), `extra.sf` (`7,9,11` without the `> `, or `No extra SF configured`).
Anything else answers `??: <text>`. Floats print through `StrHelper::ftoa`, which shows
single precision noise (`869.6179809` for 869.618).

### `region <sub>`

| Sub | Reply | Effect |
|---|---|---|
| (none) | the tree, one line per region, one space of indent per level, `^` marks home, ` F` marks flood allowed, in creation order | none |
| `put <name> [parent]` | `OK - (flood allowed)`, `Err - unknown parent`, `Err - unable to put` | creates or moves a region and resets its flags; no save |
| `remove <name>` | `OK`, `Err - not found`, `Err - not empty` | exact name only; the wildcard and parents are "not empty" |
| `allowf`, `denyf <name>` | `OK` or `Err - unknown region` | flood flag; no save |
| `get <name>` | ` name (parent) F` | prefix match; the last of several partial matches wins |
| `home [name]` | ` home is X` / ` home is now X` / `Err - unknown region` | home region; no save |
| `default [name]` | ` default scope is X` / ` ... is now X`, or `Err - region table full` | creates the region if missing, `onDefaultRegionChanged`, `saveRegions` |
| `load` | empty | `startRegionsLoad()` |
| `save` | `OK` or `Err - save failed` | `discovery_mod_timestamp` = clock, `savePrefs`, `saveRegions` |
| `list allowed\|denied` | comma separated names, `-none-` | none |
| `def a b|c ...` | the tree, or `Err - empty def`, `empty name`, `empty jump`, `unknown jump: X`, `put failed: X` | chain of `put` calls, each name under the previous; no rollback |
| anything else | `Err - ??` | none |

## Test coverage

| File | Tests | Covers |
|---|---|---|
| `test_cli_commands.cpp` | 47 | dispatch order, board hook, prefix quirks, power, clock, advert, neighbors, tempradio, password, erase, ver, board, powersaving, log, stats, routing |
| `test_cli_set.cpp` | 49 | every `set` key, ranges, truncation, the zeroing in `savePrefs`, radio precedence, bridge and `extra.sf` variants |
| `test_cli_get.cpp` | 24 | every `get` key, prefix order, hex output, variants, fallback |
| `test_cli_region.cpp` | 34 | every `region` sub command, limits, errors, callbacks |
| `test_cli_sensor.cpp` | 39 | `sensor get\|set\|list` with paging, `gps ...` (GPS build) or "Unknown command" (others) |
| `test_cli_flood_type.cpp` | 19 | `set` and `get flood.max.type`: names and numbers, sorted list, `off`, range and type errors, damaged stored list, full list |
| `test_common_cli.cpp` | 1 | `main()` and a smoke test |

Counts are `TEST_F` bodies; the number of cases a run reports differs per environment
because of the variant specific tests:

| Environment | Cases |
|---|---|
| `native_common_cli` | 176 |
| `native_common_cli_nrf52` | 196 |
| `native_common_cli_esp32` | 176 |
| `native_common_cli_rs232` | 183 |
| `native_common_cli_espnow` | 184 |

## Quirks pinned as is

Each of these is asserted by a test with "pinned as is" in its name or comment. 13b
must keep them, or change them as a separate, announced step.

- Most commands match by prefix with no check for the end of the word: `rebootxyz`
  reboots, `advertfoo` sends an advert, `get namefoo` answers the name, `ver...`,
  `board...`, `clockXYZ`, `gpsx`, `region` + anything. Only `advert.zerohop`, `erase`,
  `power`, `gps diag`, `stats-*` check the end.
- Order decides: `advert.zerohop` before `advert`, `clock sync` and `start ota` before
  `clock`, `flood.max.advert` and `.unscoped` before `flood.max` (set and get),
  `log start|stop|erase` before `log`, `powersaving on|off` before `powersaving`,
  `gps on|off|sync|diag|setloc|advert` before `gps`.
- `savePrefs()` sets `advert_interval` to 0 when it would be below 60 seconds, so any
  save in a state with a short interval clears it.
- `set flood.max 300` stores 44: the value is cast to `uint8_t` before the check.
- `set flood.max.type` on a damaged stored list (for example `6:4,x:1,9:3`) keeps only
  the entries before the damage and writes that back, so the rest is lost on the next set.
- `get flood.max.type` matches by prefix and ignores arguments, like the other `get` keys.
- `set name` cuts at 31 characters and `set owner.info` at 119 without an error.
- `set prv.key` works over the air (only `get prv.key` is serial only).
- `neighbor.remove` with an empty key or `zz` is accepted.
- The two clock error texts differ in brackets (`time` has them, `clock sync` not).
- `region default <bad name>` answers "region table full" for any failure, and the
  result of `saveRegions` is ignored there.
- `region get Al` takes the last of several partial matches.
- `sensor listX` reads its index from after the X.
- `sensor set` with no value sends the text `null`.
- The call to `_acl` in the constructor is stored and never used: no command has an
  ACL side effect.

## Mutation tests

For each command group one line of `CommonCLI.cpp` was broken, the matching
environment run, and the file restored. Three batches, 48 mutations
that applied cleanly (35 on the first state, 13 added with `flood.max.type`; the
first 35 were run again after merging `integration` and gave the same result):

| Group | Mutations | Result |
|---|---|---|
| dispatch and ordering | `advert.zerohop` literal; `clock sync` literal | caught |
| clock | bracket text of the backwards error | caught |
| neighbors | reply text; hex length limit | caught (the limit by a stack overflow abort in the test binary) |
| password, clear stats, reboot | reply text; `clear stats` literal; `clkreboot` literal | caught |
| serial only gates | removed the gate on `erase`, `stats-core`, `set freq`, `set prv.key` | caught, one each |
| `savePrefs` | `<` to `<=` in the zeroing check | caught |
| set | `flood.max` range; `flood.max` stored value; `lat` stored in `lon`; `owner.info` limit; `adc.multiplier` no reset on failure | caught |
| tempradio | freq lower bound; cr upper bound; timeout must be positive | caught |
| get | `name` reply; `public.key` branch; `loop.detect` branch; fallback text | caught |
| powersaving | unsupported board text | caught |
| sensor | custom var text; page reserve | caught |
| region | `not empty`; `unknown parent`; `table full`; `save failed` | caught |
| gps (nRF52 env) | `advert share` stores `prefs`; `gps on` stores 0 | caught |
| bridge (RS232 env) | `bridge.type` text | caught |
| `flood.max.type` (CLI) | type error text; hops error text; set does not save; set key literal; get key literal; get reply cap | caught |
| `flood.max.type` (`RoutingPolicy.h`) | hop upper bound; `off` keyword; name table entry; empty list text; numeric type limit; sorted output | caught |
| `flood.max.type` (CLI) | text of `Error, type list full` | not caught: the branch cannot be reached |

Not caught: the `type list full` text (unreachable, see gaps); none of the other 47. Not applied: one planned mutation (the `Error: unsupported`
text of `set adc.multiplier`) because the string occurs twice and the script only
mutates unique matches; it was not retried by hand. These mutations were chosen by the
author of the tests, so they cover only what was tried; lines nobody mutated may
still be unpinned. The inventory above is the list to check 13b against.

## Verification

- All five `native_common_cli*` environments pass (table above).
- Every other native environment passes (32 environments in all, including `native`:
  139 cases).
- The branch contains `integration` at `f2e7c454` (merge commit, no conflicts), which
  brought `set|get flood.max.type` and the key `f_max_pt`. Before that merge the 13a
  tests also passed against that state in a throwaway worktree.
- `git diff integration...HEAD -- src` is empty.

## Gaps

Things the harness cannot or does not cover; they are real risks for 13b:

- **Memory hazards that cannot be tested** (they are undefined behaviour, a test would
  crash or corrupt the host): `tempradio` and `set extra.sf` copy the command into the
  132 byte `tmp` with `strcpy` and no length check; `gps` with no sub command calls
  `strcmp` on `getSettingByKey("gps")` without a check for NULL; `get` with an
  argument of 157 or more characters can run past the 160 byte reply with the
  `??: %s` fallback. A table in 13b should fix these, but as an announced change.
- **Absolute dates** of `clock` and `time` are not pinned: the host `RTClib` mock counts
  from 2000, the Adafruit library from 1970. The expected text is built from the same
  `DateTime` and format string, which pins the format and the arithmetic but not the
  calendar.
- **Missing name or value in `sensor list`**: the `?` and empty fallbacks cannot be
  reached through the mock, which hands out `c_str()` pointers.
- **`Error, type list full`** of `set flood.max.type` cannot be reached: the buffer holds the
  longest possible list (`FLOOD_MAX_TYPES_LEN`), so a set that passes the range checks
  always fits. `test_routing_policy` pins the parser and the list functions themselves;
  the CLI tests here pin the path through `CommonCLI`.
- **Radio settings** (`set radio`, `tx`, `rxdelay`, ...) are pinned in
  `test/mocks/RadioPrefsChecks.h`; only their precedence over the chain is tested here.
- **Variant `board->handleCommand()` hooks** of real boards are not run; the hook is
  tested with a mock.
- **Build combinations** that do not occur in the five environments, for example ESP32
  with a bridge together with `gps`, are not built.
- **The real callbacks** of repeater, room server and sensor (what `sendSelfAdvertisement`
  or `formatNeighborsReply` actually do) are tested elsewhere; here they are recorded.
- **Hardware**: serial console behaviour, over the air delivery and persistence are not
  touched by 13a.

## Next steps

13b replaces the chain with a table, one row per command: name, match rule (exact,
word, prefix), serial only flag, minimum arguments, handler. The match rules in the
inventory above are the part to preserve first. The five environments run the same
tests before and after.

The companion has its own text CLI, `MyMesh::handleCommand()`
(`examples/companion_radio/MyMesh.cpp`, from about line 2389): 38 commands in a chain
of `if` blocks that match whole words with `strcmp()` or a prefix with a space
(`set name `, `set wifi.ssid `). It starts with the same two hooks as `CommonCLI`
(the radio settings table, then `board.handleCommand()`). It is not part of this step.
The harness pattern carries over: mock board and callbacks, a reply buffer with
canaries, one source set built in several environments. Step 10a already runs
`MyMesh` on the host (`test/test_companion_frames`), so the dependencies exist. That
makes it a 13c candidate; it has not been scoped beyond counting the commands.
