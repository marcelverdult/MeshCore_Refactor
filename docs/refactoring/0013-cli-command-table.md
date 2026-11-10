# 0013: A command table for the text CLI

Status: 13a (host harness and characterization), 13b (the table), 13c (the three
memory risks), 13d-a (a host harness for the companion's own text CLI) and 13d-b (the
tables for it) are merged. 13d-c (three corrections) is built and ready for review.
Behaviour change: none in 13a, 13b, 13d-a and 13d-b. 13a and 13d-a add tests and build
environments only; 13b replaces the `else if` chains in `src/helpers/CommonCLI.cpp` with
const tables, adds three tests and leaves the 13a tests unchanged. 13c changes the reply
to input that was undefined before. 13d-b moves the companion's 38 commands onto tables.
13d-c is the first step of the series that changes behaviour on purpose: three handlers
of the companion's text CLI, each announced below.

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
| `test_cli_table.cpp` | 3 | added in 13b: `log start` on the serial console, `clock` from a remote sender, `get loop.detect` needs the whole key |

Counts are `TEST_F` bodies; the number of cases a run reports differs per environment
because of the variant specific tests (the numbers are the 13a ones; 13b adds the
three tests of `test_cli_table.cpp` to each, so a run reports 179, 199, 179, 186 and 187):

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
  `??: %s` fallback. They are fixed in 13c.
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

## 13b: the tables

`CommonCLI::handleCommand()`, `handleSetCmd()`, `handleGetCmd()` and
`handleRegionCmd()` keep their signatures. Their `else if` chains are now four const
tables and a short search loop. `grep -cE "memcmp|strcmp" src/helpers/CommonCLI.cpp`
counts 124 before and 19 after; what remains are the search loop, the `loop.detect`
and `gps advert` value checks, the `region def` pre-parse and the region sub command
match. The file got longer (1067 to 1444 lines) because every handler is now its own
function with its own signature.

### Shape

```cpp
struct CliCmd { const char* name; uint8_t len; uint8_t match; uint8_t flags; CliFn fn; };
// match: CLI_PREFIX, CLI_WORD (end of line or a space after the name), CLI_EXACT (end of line)
// flags: CLI_SERIAL (only when sender_timestamp == 0)
CLI_CMD("advert.zerohop", CLI_WORD, CLI_ANY, CommonCLIImpl::advertZerohop),
```

- `kTopCmds` (42 rows in a GPS build), `kSetCmds`, `kGetCmds` are searched top to
  bottom, first match wins. The order is the order of the old chain and is part of the
  behaviour; a comment above the tables says so.
- `CLI_CMD` computes `len` from the string literal, so a name and its length cannot
  drift apart. The names of `time `, `set` keys and the other commands that need an
  argument include the trailing space, as the old `memcmp()` literals did.
- A serial-only row that does not apply to a remote sender is skipped and the search
  continues, which is what `sender_timestamp == 0 && memcmp(...)` did in the chain.
- A handler gets `CommonCLI&` and a `CliCall` (sender timestamp, the text searched,
  the text after the matched name, the reply buffer). The handlers are static members of
  `struct CommonCLIImpl`, which is a friend of `CommonCLI` (one line in the header), so
  they reach the private members without any new public API.
- `set` and `get` are still reached through the top level rows `set ` and `get `,
  which call `handleSetCmd` and `handleGetCmd`; those search their own table on the text
  after the four characters and write the old fallback (`unknown config: ...`, `??: ...`).
- `region` has `kRegionCmds`: name, minimum and maximum number of words, handler.
  Rows with the same name and different word counts (`home`, `default`) replace the
  old `n >= 3` and `n == 2` branches. `region` alone and `region def ...` are handled
  before the table, because `def` has to run before `parseTextParts()` changes the buffer.
- The `#if` guards (`ENV_INCLUDE_GPS`, `WITH_BRIDGE`, `WITH_RS232_BRIDGE`,
  `WITH_ESPNOW_BRIDGE`, `USE_LR2021`, `NRF52_*`, `ESP32`) sit around the handlers and
  around the rows, in the same places as before. The five `native_common_cli*`
  environments build all of those variants.
- The tables are `const` and hold only pointers and small integers, so they are
  placed in flash. There is no heap, no RTTI, no `std::function` and no virtual call.

### Alternatives and why not

| Alternative | Why not |
|---|---|
| Pointers to `CommonCLI` member functions | Needs about 120 new private declarations in `CommonCLI.h`, which every role includes. Pointer to member is also two words on ARM. |
| A friend struct of static functions (chosen) | One line in the header; handlers are plain function pointers. |
| Non capturing lambdas in the table | Lambdas defined at namespace scope have no friend access, so they would need public accessors. Each lambda is also an anonymous type, which makes the rows harder to read in a diff. |
| An X macro that generates the table and the dispatcher | Smaller source, but the rows are no longer greppable by name, and the `#if` guards become hard to read inside macro lists. |
| One table for all four levels | Matching differs (`set` keys end in a space, `get` keys do not, `region` matches the second word and a word count). A single row type for all would need a mode field per row and hide that. |
| A hash or sorted table with binary search | The old behaviour depends on order (prefixes), and a sorted table would change which of two overlapping names wins. About 130 rows are cheap to scan. |
| Matching with a `strncmp` instead of `memcmp` | `memcmp` on a fixed length is what the chain did; keeping it keeps the behaviour for lines that end early. |

### Behaviour kept on purpose

Everything in "Quirks pinned as is" is unchanged, including prefix matching without a
word end check, the order dependent pairs, `set flood.max 300` storing 44 and the two
clock error texts. No 13a test file was edited.

### Not fixed (still open)

13b leaves these as they were. The handlers contain the same code as before.

- `tempradio` and `set extra.sf` copy the command into the 132 byte `tmp` with `strcpy()`
  and no length check.
- `gps` with no sub command calls `strcmp()` on `getSettingByKey("gps")` without a
  check for NULL.
- `get <key>` with an argument of 157 or more characters can run past the 160 byte reply
  in the `??: %s` fallback.

Each of these now has its own handler, so a later step can add the length or NULL check
in one place. That changes behaviour (a refused or shortened command), so it should be a
separate, announced step.

### Verification of 13b

- **Host tests.** The 23 native environments of `.github/workflows/run-unit-tests.yml`
  all pass. With the 13a tests unchanged the run reported 1752 cases, 1 skipped, 1751
  passed, 0 failed (the same total as the 13a note). After adding the three tests below
  the run reports 1767 cases, 1 skipped, 1766 passed, 0 failed. The five
  `native_common_cli*` environments report 179, 199, 179, 186 and 187 cases (176, 196,
  176, 183, 184 plus three), all passed.
- **Same tests against both implementations.** The three new tests also pass on the old
  chain (`native_common_cli` and `native_common_cli_nrf52` on the code of `integration`
  `592b7472`: 378 cases passed).
- **Mutation tests on the table.** 67 single edits of `CommonCLI.cpp`, each run against
  the matching `native_common_cli*` environment, then the file restored (checked with
  `diff`).

  | Group | Mutations | Result |
  |---|---|---|
  | Row removed (top, set, get, bridge, `extra.sf`, `gps`, region) | 16 | all caught |
  | Order swapped (`advert.zerohop`, `clock sync`, `log start`, `powersaving on`, `flood.max` in set and get, `advert.interval`, two `gps` pairs) | 12 | 8 caught, 4 alive (1 real gap, 3 equivalent, see below) |
  | Wrong handler (`ver`, `poweroff`, `stats-radio`, `set lat`, `get lat`, region `default`, `load`, `denyf`) | 8 | all caught |
  | Name or match length wrong (`reboot`, `neighbors`, `owner.info`, `time` without its space, `loop.detect`) | 5 | 4 caught, 1 alive (real gap) |
  | Match rule changed to PREFIX (`advert.zerohop`, `stats-packets`, `power`, `erase`, `gps diag`) | 5 | all caught |
  | Serial flag dropped or added (`log`, `stats-radio`, `clock`, `set freq`, `get prv.key`) | 5 | 4 caught, 1 alive (real gap) |
  | Search loop (serial check removed or inverted, WORD check removed, EXACT check removed, WORD accepts only end of line, compare one character less, argument offset, text of `Unknown command`) | 8 | all caught |
  | Region table (minimum words of `allowf` and `get`, `home` query minimum, `strcmp` prefix, early return for `region`, fallback text) | 6 | 5 caught, 1 alive (equivalent) |
  | Fallback texts of `set` and `get` | 2 | all caught |

  Of the 67 mutations, 60 were caught on the first run and 7 stayed alive:

  | Mutation | What it is | Handled by |
  |---|---|---|
  | `log` above `log start` | real gap: nobody ran `log start` on the serial console | new test `LogStartStopEraseOnTheSerialConsoleAreNotTheDump`; the mutation is now caught |
  | serial flag added to `clock` | real gap: no test sent `clock` from a remote sender | new test `ClockAnswersOverTheAirToo`; now caught |
  | `get loop.detect` matched on 4 characters | real gap: only the full key was tested | new test `GetKeyNeedsTheWholeNameNotJustItsFirstLetters`; now caught |
  | `set flood.max ` above `flood.max.advert ` | equivalent: the key ends in a space, so `flood.max.advert 5` never starts with `flood.max ` | not a defect, no test possible |
  | `set flood.max ` above `flood.max.unscoped ` | equivalent, same reason | same |
  | `get advert.interval` above `get flood.advert.interval` | equivalent: neither name starts with the other | same |
  | `region home` query row with minimum 1 word | equivalent: one word never reaches the table | same |

  These mutations were chosen by the author, as in 13a, so they cover what was tried.
- **Flash and RAM** (`pio run`, same toolchain state before and after, no other change):

  | Environment | Platform, role | Flash before | Flash after | Flash delta | RAM before -> after |
  |---|---|---|---|---|---|
  | `Xiao_nrf52_repeater` | nRF52, repeater | 364752 | 367008 | +2256 | 34692 -> 34692 (+0) |
  | `Xiao_nrf52_room_server` | nRF52, room server | 357184 | 359424 | +2240 | 38380 -> 38380 (+0) |
  | `RAK_4631_repeater` | nRF52, repeater | 448684 | 451676 | +2992 | 37448 -> 37448 (+0) |
  | `RAK_4631_sensor` | nRF52, sensor | 445148 | 448140 | +2992 | 35536 -> 35536 (+0) |
  | `heltec_v4_r8_repeater` | ESP32-S3/C6, repeater | 1178913 | 1182065 | +3152 | 62488 -> 62488 (+0) |
  | `heltec_v4_r8_room_server` | ESP32-S3/C6, room server | 1174109 | 1177261 | +3152 | 66176 -> 66176 (+0) |
  | `heltec_v4_r8_sensor` | ESP32-S3/C6, sensor | 1185481 | 1188613 | +3132 | 60576 -> 60576 (+0) |
  | `heltec_rc32_repeater_bridge_espnow` | ESP32-S3/C6, repeater+ESP-NOW bridge | 1181833 | 1185229 | +3396 | 64096 -> 64096 (+0) |
  | `wio-e5-mini_repeater` | STM32WL, repeater | 205944 | 207168 | +1224 | 17260 -> 17260 (+0) |
  | `wio-e5-mini_sensor` | STM32WL, sensor | 205456 | 206680 | +1224 | 15364 -> 15364 (+0) |
  | `waveshare_rp2040_lora_repeater` | RP2040, repeater | 270528 | 271600 | +1072 | 24904 -> 24904 (+0) |
  | `waveshare_rp2040_lora_room_server` | RP2040, room server | 266480 | 267552 | +1072 | 28608 -> 28608 (+0) |
  | `waveshare_rp2040_lora_repeater_bridge_rs232` | RP2040, repeater+RS232 bridge | 275036 | 276244 | +1208 | 26796 -> 26796 (+0) |

  RAM is unchanged in all 13 builds (the tables are const data in flash). Flash grows by
  1.1 to 3.4 KB; the growth was not broken down further. Each row holds a name, a length
  and a function pointer. The smallest margin is `wio-e5-mini_repeater` on STM32WL:
  89.8 percent of flash before, 90.3 after (1224 bytes, of 229376 available).
- **Hardware.** Not run on devices.

## 13c: the three memory risks

13a and 13b named three memory risks and left them alone, because neither step was
allowed to change behaviour. 13c fixes them. It is a behaviour change on input that was
undefined before, so it is its own step.

### What changed

All changes are in `src/helpers/CommonCLI.cpp`, 19 lines added and 2 changed.

- **`tmp` copies.** The commands that copy their argument into the 132 byte
  `CommonCLI::tmp` now check `strlen(arg) < sizeof(tmp)` first. There are three of them,
  not two as the earlier text said: `tempradio`, `sensor set` and, on LR2021 builds,
  `set extra.sf`. A new helper `fitsTmp()` does the check. A longer argument gets the
  failure answer the command already had: `Error, invalid params` for `tempradio`,
  `can't find custom var` for `sensor set`, `Invalid extra SF config` for `set extra.sf`.
  Nothing is copied, nothing is applied, nothing is saved. The longest argument that
  still works is 131 characters.
- **`gps` without a sub command** checks the result of `getSettingByKey("gps")` for NULL,
  as `gps` diagnostics (`gpsDiag`) already did. A missing setting counts as not active,
  so the reply is `on, deactivated, ...`.
- **`get <unknown key>`** now writes the `??: ` fallback with `snprintf(reply, 160, ...)`.
  The prefix takes 4 bytes and the terminator one, so a key of up to 155 characters is
  echoed in full and a longer one is cut at 155. Before, 156 characters already wrote one
  byte past the buffer, not 157 as the earlier text said (4 + 156 + 1 is 161 bytes). The arithmetic is checked by the tests from 156 characters up; the abort was seen with the whole loop, not with 156 alone.

Replies to normal input are unchanged. No existing test was edited.

### Why these answers

The commands keep their own error text, so a caller that matches on it does not see a new
string. For `sensor set` the text says the variable does not exist, which is true for a
name that long. A new error text would be a protocol change, and the reply to an input
that never worked does not justify one.

### Tests

`test/test_common_cli/test_cli_memory.cpp` is new. The fixture builds a second `CommonCLI`
in a buffer with 512 canary bytes behind it (`tmp` is the last member, so an overflow
lands in the canary), and runs commands of any length with guard bytes around the reply.
It pins the 131 and 132 character limit of `tempradio`, a working `sensor set` with a
100 character value, the longest key that `get` echoes in full, truncation at 155 for
keys of 156, 157, 200 and 400 characters, and the NULL case of `gps` (GPS builds only).

### Verification

- **CI list, 23 environments** (`pio test`, the list from `run-unit-tests.yml`):
  1799 cases, 1798 passed, 1 skipped, 0 failed. Base was 1767 cases, 1766 passed.
- **Red before the fix.** With the tests in place and the fix not yet written,
  `native_common_cli` failed the three `tmp` tests and then aborted with SIGABRT on the
  `get` test (stack smashing detected in the test's own reply buffer).
- **Mutation tests.** Each fix reverted on its own, with the environment that builds it:
  `tempradio` 2 tests red, `sensor set` 1 red, `set extra.sf` 1 red (nRF52 build),
  `gps` SIGSEGV (nRF52 build), `get` SIGABRT. A crash after a failed test is the
  corrupted memory, not a flaw in the harness.
- **Size**, measured with `pio run`, not flashed:

  | Environment | Flash before | Flash after | Delta | RAM |
  |---|---|---|---|---|
  | `wio-e5-mini_repeater` | 207168 | 207216 | +48 | 17260, unchanged |
  | `Heltec_v3_repeater` (ESP32) | 1180389 | 1180453 | +64 | 62600, unchanged |
  | `RAK_4631_repeater` (nRF52) | 451676 | 451724 | +48 | 37448, unchanged |

### Not verified

- **On devices.** No board was flashed.
- **The tests run on the host (64 bit).** Overflow sizes and the layout behind `tmp`
  differ on the MCUs. The tests detect the write by canary, so they show the bug is gone,
  not what it would have done on a given board.
- **The other roles' callers.** The 160 byte reply buffer is what every role passes, as
  the code already assumed; this step did not re-check each caller.

### Upstream

`upstream/dev` (4deb88df) has the same code in all three places: the `strcpy(tmp, ...)`
calls (`tempradio`, `sensor set`, `extra.sf`), `strcmp(getSettingByKey("gps"), "1")` and
`sprintf(reply, "??: %s", config)`. A fix there would be a separate pull request.

## 13d-a: a harness for the companion's text CLI

The companion app has a text CLI of its own, `MyMesh::handleCommand()`
(`examples/companion_radio/MyMesh.cpp`, about 250 lines). It is a chain of `if` blocks
with 38 commands. Whole words are compared with `strcmp()`, commands with an argument
with `memcmp()` and a trailing space (`set name `), and the `ui.*` commands by prefix
with no space and no check for the end of the word. 13d-b will replace the chain with
tables, as 13b did for `CommonCLI`. 13d-a comes first and pins the current behaviour with
host tests.

It adds tests, a mock and build environments, and changes no production code:
`git diff integration...HEAD -- src examples` is empty. A defect found while testing is
pinned as it is and listed under "Not fixed (13d-a)".

### Reaching a private function

`handleCommand()` is private and has three callers.

| Caller | Timestamp | Reply buffer | Driven here |
|---|---|---|---|
| `onCLICommandRecv()`: a text message from a contact with the remote CLI flag (`0x10`) | the sender's | 161 bytes: `BaseChatMesh.cpp` hands over `&temp[5]` of a `uint8_t temp[166]` | yes, `CliTest::run()` |
| `cmdRunCliCommand()`: the app's `CMD_RUN_CLI_COMMAND` frame (66), answered by `RESP_CODE_CLI_REPLY` (29) | 0 | `reply_buf[166]` | yes, `CliTest::runFrame()` |
| `checkCLIRescueCmd()`: the rescue console on the serial port | 0 | `reply_buf[166]` | the call into `handleCommand()` is the same as the frame's; the rescue console's own commands (`rebuild`, `erase`, `ls`, ...) are not covered |

Both driven callers append `Unknown command` when `handleCommand()` returns false. A
contact without the CLI flag does not reach `handleCommand()`: the message is queued for
the app and the reply buffer stays untouched (one test pins that).

What was added, all under `test/`:

- `Node::cliRemote()` in `CompanionHarness.h` calls the protected `onCLICommandRecv()`.
- `CliHarness.h` holds the `CliTest` fixture. `run(cmd, timestamp)` gives the command a
  161 byte reply buffer. It starts as an empty string, is filled with `Z` behind it, and
  has 16 guard bytes on both sides. A command that writes too much, or leaves the reply
  without a terminator, fails the test that sent it. `run()` also counts the saves as
  `open:/prefs.json` events of the file system mock. `reboot()` builds the node again on
  the same file system, so a test sees what was stored and not what the node still holds.
- `MockBoard` in `test/mocks/target.h` got a `handleCommand()` with a configurable
  answer and side effect that records the command text and the timestamp it was given, a
  `powerOff()` counter, and `dynamic_prefs`, the key value store the node hands to the
  board. A test uses it to make the node's preferences dirty from inside the hook.
- `test/mocks/WiFi.h` is a stand in for the Arduino WiFi library: `WiFi.status()` and
  `WiFi.localIP().toString()` return what a test sets.

### Build variants

38 commands are not all built in one environment. Three compile switches select them:
`PIN_BUZZER` (2 commands), `ENABLE_WIFI_INTERFACE` (9) and `DISPLAY_CLASS` (18). Before
13d-a, `native_companion` set none of them, so 29 of the 38 commands were not compiled
at all. One source set is now built in five environments, all listed in
`.github/workflows/run-unit-tests.yml`:

| Environment | Defines | Commands built | `CliTest` tests | Cases reported by `pio` |
|---|---|---|---|---|
| `native_companion` | `RP2040_PLATFORM` | 9 | 58 | 123 |
| `native_companion_buzzer` | plus `PIN_BUZZER=7` | 11 | 63 | 128 |
| `native_companion_wifi` | plus `ENABLE_WIFI_INTERFACE=1` | 18 | 80 | 144 |
| `native_companion_display` | plus `DISPLAY_CLASS=TestDisplay` | 27 | 74 | 138 |
| `native_companion_all` | all three | 38 | 104 | 168 |

The 123 cases of the base environment are 65 from before 13d-a and 58 new ones.

Why `native_companion_all` exists. `prefs.json` has a `comp` block that is always there
(it holds `buzz_q`), a `wifi` block only with `ENABLE_WIFI_INTERFACE` and a `ui` block
only with `DISPLAY_CLASS`. Only the all-three build has all three blocks in the file at
the same time, so only it can show that a command of one block leaves the others alone
across a restart. Three tests do that. The pairs of switches (buzzer with WiFi, buzzer
with display, WiFi with display) are not built on their own; the all-three build
contains each of them.

Two details of the environments:

- Each new environment repeats the whole `build_flags` list of `native_companion`
  instead of adding to it. With `extends`, `${this.__env__}` in the include path of the
  `base64` library would point into the parent's library folder.
- The display variant defines `DISPLAY_CLASS` only. `variant_display/target.h` cannot be
  used, because it conflicts with `test/mocks/target.h`, and `handleCommand()` never
  touches the display object.

`test_frame_sweep.cpp` (step 10) digests the file system, and `prefs.json` has the WiFi
and UI blocks only in the builds with those switches. Its golden digest therefore runs
only in the builds without WiFi and without a display (8 added lines with a comment).
The other 13d-a tests are not affected.

### Inventory of the 38 commands

Replies are exact; `%d` stands for the stored value. "Save" is `savePrefs()`, which
writes `prefs.json` once. Matching is `strcmp` (the whole command) unless marked.

#### Hooks and framing, before the commands

1. Leading spaces are skipped (only the space character, not a tab).
2. If the text is longer than 4 characters and the third character is `|`, the first
   three characters are copied to the start of the reply and removed from the command.
   Any two characters work as the prefix. No spaces are skipped after it.
3. The radio settings table (`getRadioPrefs()->handleCommand`) comes first, and its
   dirty flag causes a save.
4. `board.handleCommand()` comes next, with the command as it is after steps 1 and 2 and
   the sender's timestamp. If it answers, the node saves when its own preferences are
   dirty. A change the hook makes without answering is not saved by the node.
5. The commands, in source order.
6. `Unknown command` is appended by the caller. With a prefix the reply is the prefix
   followed by `Unknown command`.

#### Always built (9)

| Command | Match | Reply | Save |
|---|---|---|---|
| `reboot` | whole | none, calls `board.reboot()` | no |
| `poweroff`, `shutdown` | whole | none, calls `board.powerOff()` | no |
| `set name <name>` | prefix `set name ` | `OK`, or `Error, bad chars` | yes, on `OK` |
| `get name` | whole | `> <name>` | no |
| `set pin <n>` | prefix `set pin ` | `> pin is now %06d` | yes |
| `board` | whole | the board's manufacturer name | no |
| `ver` | whole | `<version> (Build: <date>)` | no |
| `get tz.offset` | whole | `> %.2f` for an odd number of quarter hours, else `> %.1f` | no |
| `set tz.offset <n>` | prefix `set tz.offset ` | `OK`, or `Error, must be from -12 to +14` | yes, on `OK` |

#### `PIN_BUZZER` (2)

| Command | Reply | Save |
|---|---|---|
| `get buzzer` | `> on` or `> off` | no |
| `set buzzer on\|off` | `OK`, or `Error: state must be on or off` | yes, on `OK` |

#### `ENABLE_WIFI_INTERFACE` (9)

| Command | Reply | Save |
|---|---|---|
| `set wifi.ssid <s>` | `> wifi.ssid is now <s> (set wifi.pwd too, then reboot)` | yes |
| `set wifi.pwd <p>` | `> wifi.pwd updated (reboot to apply)` | yes |
| `get wifi.pwd` | `> <password>` in clear text | no |
| `set wifi.clear` | `> wifi config cleared (reboot to apply)` | yes |
| `get wifi.ssid` | `> <ssid>`, or `> (not set)` | no |
| `set wifi.enabled <n>` | `> wifi.enabled is now %d (reboot to apply)` | yes |
| `get wifi.enabled` | `> %d` | no |
| `get wifi.status` | `> connected` for `WL_CONNECTED`, else `> disconnected` | no |
| `get wifi.ip` | `> <address>` when connected, else `> (not connected)` | no |

#### `DISPLAY_CLASS` (18, all matched by prefix with no space)

| Key (`ui.` + ...) | `set` reply | `get` reply |
|---|---|---|
| `show_clock` | `> now %d` | `> %d` |
| `auto_off_delay` | `> now %d seconds` | `> %d seconds` |
| `backlight_delay` | `> now %d seconds`, `> now always on (-1)`, or `> now disabled (<-1)` | `> %d seconds`, `> always on (-1)`, or `> disabled (<-1)` |
| `backlight_on_keys` | `> now %d` | `> %d` |
| `backlight_on_msgs` | `> now %d` | `> %d` |
| `auto_off_blank` | `> now %d` | `> %d` |
| `auto_off_keep_msgs` | `> now %d` | `> %d` |
| `max_partial` | `> now %d (applied at next screen wakeup)` | `> %d` |
| `nd_name` | `> ui.nd_name is now %d` | `> %d` |

Every `set ui.*` saves once; every `get ui.*` saves nothing.

### Test coverage

| File | Tests | Covers |
|---|---|---|
| `test_cli_core.cpp` | 58 | leading spaces, empty input, `Unknown command` without a save, the `xx\|` prefix and its limits, hook order (radio table, board hook, commands), the board hook's view of the command and its save rule, `reboot`, `poweroff`, `shutdown`, `set\|get name`, `set pin`, `board`, `ver`, `set\|get tz.offset`, the frame path, a contact without the CLI flag, and one test per compile switch that shows its commands are absent when it is off |
| `test_cli_buzzer.cpp` | 6 | default, set and get, error text and no save, restart, exact spelling, prefix and frame |
| `test_cli_wifi.cpp` | 23 | all nine commands, length cuts, the password in clear text, `wifi.clear` side effects, restart, exact spelling, prefix and frame, the WiFi block of `prefs.json` |
| `test_cli_ui.cpp` | 17 | defaults, all 18 commands, the three answers of `backlight_delay`, byte wrap, unsigned values, missing and glued values, prefix matching and its order, restart, prefix and frame |
| `test_cli_combined.cpp` | 3 | the three blocks in one `prefs.json`, restart with all of them, a command of one block leaves the others alone |

The three "absent" tests in `test_cli_core.cpp` are each built only where the switch is
off, which is why the totals per environment (58, 63, 80, 74, 104) are not the sum of the
file counts.

### Quirks pinned as is

Each of these is asserted by a test. 13d-b must keep them, or change them as a separate,
announced step.

- Only the space character is skipped in front of a command. A tab or a newline makes
  the command unknown.
- The prefix needs a total length above 4. `xx|a` has four characters and is a command
  called `xx|a`, so it is unknown; `xx|ab` is the prefix `xx|` followed by the command
  `ab`. Any two characters followed by `|` count as a prefix, so `|||get name` has the
  prefix `|||`.
- With a prefix, a command that writes nothing leaves just the prefix as the reply.
- A leading space is removed before the board hook sees the command, and the prefix too.
- `reboot`, `poweroff`, `shutdown`, `get name`, `board`, `ver`, `get tz.offset` and the
  WiFi `get` commands match the whole command. `rebootx` and `get name ` are unknown.
  Commands with an argument need the space after the key: `set name` alone is unknown.
- `set name` checks for the seven bad characters in the whole text, then cuts the name at
  31 characters without an error. A bad character after position 31 still refuses the
  name. The name is the rest of the line, spaces included, and an empty name is
  accepted.
- `set pin` has no range check: it reads with `atoi`, stores into an unsigned 32 bit
  field and prints with `%06d`. `1234567` and `99999999` are accepted, `-5` is stored as
  4294967291 and printed as `-00005`, `abc` gives `000000`, `12abc` gives `000012`.
  There is no `get pin`.
- `set tz.offset` reads with `atof`, so `1e1` is 10. The range check is `tz < -12 ||
  tz > 14`, which is false for NaN (see "Not fixed").
- `get tz.offset` shows two decimals only when the offset is an odd number of quarter
  hours (`5.75`, `0.25`, `-5.75`), and one decimal otherwise (`5.5`, `-12.0`).
- `set wifi.ssid` cuts at 32 characters and `set wifi.pwd` at 63, without an error. The
  reply to `set wifi.pwd` does not show the password; `get wifi.pwd` does.
- `set wifi.clear` saves even when both fields were already empty, and leaves
  `wifi.enabled` alone.
- `set wifi.enabled` stores 1 for any number except 0, and 0 for text that `atoi` reads
  as 0.
- `get wifi.ssid` answers `(not set)` for an empty stored name. The compile time
  `WIFI_SSID` fallback of `getWifiSSID()` is not built (see gaps).
- The `ui.*` commands match by prefix with no space and no check for the end of the
  word. `set ui.show_clock` alone stores 0 and saves. `set ui.show_clock1` and
  `set ui.show_clock 1` both store 1, `set ui.show_clockabc` stores 0 and
  `set ui.auto_off_delayXYZ` stores 0. `set ui.max_partial_please 4` is the
  `max_partial` command with the value `_please 4`, which reads as 0. `get` ignores
  whatever follows the key. No key is a prefix of another, so the order of the branches
  does not show.
- The byte sized `ui.*` fields keep the low byte of the number: 300 is stored as 44 and
  -1 as 255. `set ui.auto_off_delay -1` stores 4294967295 and prints `-1`.
- `set ui.backlight_delay` has three answers: zero or more prints seconds, exactly -1
  prints `always on (-1)`, anything below prints `disabled (<-1)`.
- The `ui.*` commands are not matched below their full key: `set ui.show`,
  `set ui.auto_off` and `get ui.` are unknown.

### Mutation tests

For each mutation one expression in `handleCommand()` was changed, the matching
`native_companion*` environment run with `CliTest.*`, and the file restored. A script
did this, one mutation at a time. Before the run, a dry run checked that each pattern
matches exactly once in `MyMesh.cpp`; after the run `git status` was clean and
`git diff HEAD -- src examples` empty.

32 mutations were applied. All 32 turned at least one test red. The number of failed
cases that `pio` reported equals the number of distinct failing tests named in the log
for every one of them. These mutations were chosen by the author of the tests, so they
cover only what was tried; lines nobody mutated may still be unpinned.

| # | Mutation | Environment | Red tests |
|---|---|---|---|
| M01 | the prefix is not removed before the hooks and the commands | `native_companion` | 7 |
| M02 | a change by the radio settings table is not saved | `native_companion` | 2 |
| M03 | a change by the board hook is not saved | `native_companion` | 1 |
| M04 | `reboot` calls `powerOff()` | `native_companion` | 3 |
| M05 | `shutdown` is no longer handled | `native_companion` | 1 |
| M06 | `set name` does not save | `native_companion` | 6 |
| M07 | `set name` no longer checks for bad characters | `native_companion` | 2 |
| M08 | `set tz.offset` upper bound 14 becomes 15 | `native_companion` | 1 |
| M09 | `set tz.offset` lower bound -12 becomes -13 | `native_companion` | 1 |
| M10 | `get tz.offset` quarter hour test `& 1` becomes `& 3` | `native_companion` | 2 |
| M11 | `set pin` reply without zero padding | `native_companion` | 3 |
| M12 | the prefix needs 4 characters instead of more than 4 | `native_companion` | 1 |
| M13 | the prefix reflects two bytes instead of three | `native_companion` | 9 |
| M14 | leading spaces are not skipped | `native_companion` | 3 |
| M15 | the board hook gets timestamp 0 | `native_companion` | 1 |
| M16 | an unknown command is reported as handled | `native_companion` | 17 |
| M17 | `ver` reply format | `native_companion` | 2 |
| M18 | `get name` matches by prefix | `native_companion` | 2 |
| M19 | `set buzzer` stores the inverse | `native_companion_buzzer` | 4 |
| M20 | `get buzzer` answers are swapped | `native_companion_buzzer` | 5 |
| M21 | `set buzzer` does not save | `native_companion_buzzer` | 3 |
| M22 | `set wifi.enabled` stores the number, not 0 or 1 | `native_companion_wifi` | 1 |
| M23 | `get wifi.status` answers are swapped | `native_companion_wifi` | 1 |
| M24 | `set wifi.clear` leaves the password | `native_companion_wifi` | 1 |
| M25 | `set wifi.pwd` is cut at the size of the SSID buffer | `native_companion_wifi` | 1 |
| M26 | `get wifi.ip` shows the address when not connected | `native_companion_wifi` | 1 |
| M27 | `set ui.show_clock` key length 17 becomes 18 | `native_companion_display` | 6 |
| M28 | `set ui.backlight_delay` boundary `>= 0` becomes `> 0` | `native_companion_display` | 2 |
| M29 | `get ui.nd_name` reads `ui_max_partial` | `native_companion_display` | 4 |
| M30 | `set ui.auto_off_keep_msgs` writes `ui_auto_off_blank` | `native_companion_display` | 5 |
| M31 | `set ui.max_partial` does not save | `native_companion_display` | 3 |
| M32 | `set ui.show_clock` stores the number, not 0 or 1 | `native_companion_display` | 1 |

### Verification of 13d-a

- **CI list, 27 environments** (`pio test`, the list from `run-unit-tests.yml`, exit code 0):
  2435 cases, 1 skipped, 2434 passed, 0 failed. The baseline, measured first on a clean
  copy of `integration` (`9605acd9`, 23 environments), was 1799 cases, 1 skipped, 1798
  passed. The difference of 636 is the 58 new cases in `native_companion` plus the
  128, 144, 138 and 168 cases of the four new environments.
- **The five companion environments** pass with the counts in the table under "Build
  variants". The shared mock `test/mocks/target.h` changed, and the 22 environments
  outside the companion family still pass.
- **Mutation tests:** 32 of 32 caught (table above).
- **No production change:** `git diff integration...HEAD -- src examples` is empty. The
  branch changes tests, mocks, `platformio.ini` (native environments only) and the CI
  workflow: 12 files.
- **Not built:** no firmware environment (`pio run`). Nothing in `src` or `examples`
  changed, so the firmware is the same; that was not measured.

### Not fixed (13d-a)

Three findings. Each behaviour is pinned as it is, with a test that says so; changing
any of them is a separate step.

1. **`set tz.offset nan` is accepted and saved.** `atof("nan")` is NaN, the check
   `tz < -12 || tz > 14` is false for NaN, so the command answers `OK` and stores the
   text `tz_offset:nan`. The radio settings refuse NaN and infinity (`spellsNanOrInf` in
   `CommonRadioPrefs.cpp`); this command does not. Test:
   `SetTzOffsetAcceptsNaNBecauseNoComparisonWithItIsTrue`.
2. **`get wifi.pwd` answers the WiFi password in clear text.** It does so for a contact
   with the remote CLI flag as well, over the air. `set wifi.pwd` hides it in its own
   reply. Test: `GetPwdAnswersThePasswordInClearText`.
3. **`set pin` has no range check.** A negative number is stored as a large unsigned
   value and printed with a minus sign, and a number above 999999 is stored although a
   BLE pin has six digits. Test: `SetPinHasNoRangeCheck`.

The tests found no overflow of the reply buffer in `handleCommand()` itself. The
longest reply, `set wifi.ssid` with a 32 character name and a prefix, is about 90 bytes,
well below the 161 bytes, and the guard bytes in every `run()` call would show a write
beyond them. The one unbounded copy, `board`, is listed under the gaps.

### Gaps and not verified

- **No board was flashed**, and the tests run on the host (64 bit). The guard bytes show
  that a write stays inside the reply buffer; they do not show what a write beyond it
  would do on a board.
- **The rescue console's own commands** (`rebuild`, `erase`, `ls`, ...) in
  `checkCLIRescueCmd()` are not covered.
- **`board` with a long manufacturer name.** The branch copies the name with `strcpy()`
  and no length check. The name comes from the board class and the mock returns
  `Mock`, so the case is not tested.
- **Pairs of switches** (buzzer with WiFi, buzzer with display, WiFi with display) are
  not built on their own. The three switches guard separate `#ifdef` blocks and share
  only the preferences file, which the all-three build covers.
- **The compile time `WIFI_SSID` fallback** is not built; `get wifi.ssid` is tested for
  a stored name and for the empty case (`(not set)`).
- **Variant defaults of the UI settings** (`UI_SHOW_CLOCK`, `AUTO_OFF_MILLIS`,
  `BACKLIGHT_BTN`, `UI_SCREENOFF_BLANK`, `EINK_MAX_PARTIAL_REFRESH`) differ between real
  boards. The tests pin the defaults of the test build.
- **Variant `board->handleCommand()` hooks** of real boards are not run; the hook is a
  mock.
- **The frame sweep golden digest** of step 10 is switched off in the WiFi, display and
  all-three builds (see Build variants).
- **The mutation list** is the author's choice (see above).

## 13d-b: the tables

`MyMesh::handleCommand()` keeps its signature and the order of its first steps: skip
leading spaces, reflect the `xx|` prefix, the radio settings table, the board hook. The
38 commands behind them, an `if` chain of 253 lines with 40 lines that compare text, are
now three const tables and a search loop. `handleCommand()` is 23 lines. The file grew
from 2884 to 3028 lines, because every command is its own function. Outside
`MyMesh.cpp` the only change is one line in `MyMesh.h`: `friend struct MyMeshCli;`.

### Shape

```cpp
struct CliCmd { const char* name; uint8_t len; uint8_t match; CliFn fn; };
// match: CLI_PREFIX (memcmp of the first len characters), CLI_EXACT (strcmp, the whole text)
CLI_CMD("wifi.clear", CLI_EXACT, MyMeshCli::setWifiClear),
```

- `kTopCmds` has 7 rows: `reboot`, `poweroff`, `shutdown`, `set `, `get `, `board`,
  `ver`. `kSetCmds` and `kGetCmds` have 17 rows each when all three compile switches are
  on, and 3 and 2 rows in the base build. The two shared commands `poweroff` and
  `shutdown` are two rows with one handler.
- The match rule is the one the chain used for that command. `CLI_PREFIX` is `memcmp()`
  with the length of the literal, `CLI_EXACT` is `strcmp()`. The names of commands with
  an argument include the space (`name `, `pin `, `wifi.ssid `), the `ui.` keys have none,
  as before. `CLI_CMD` computes `len` from the literal.
- `set ` and `get ` are top level rows, as in 13b. Their handlers search the text after
  the four characters in their own table and return its answer. This is the only place a
  handler can return `false`, which makes the caller append `Unknown command`. Because
  the first match wins and `set ` and `get ` are not the start of any other top level
  name, `set foo` ends there and is unknown, as it was.
- A handler gets `MyMesh&` and a `CliCall` (the text after the matched name, the reply
  buffer). The handlers are static members of `struct MyMeshCli`, a friend of `MyMesh`,
  so the header gets no new public API and no list of private declarations.
- The compile switches sit around the handlers and around the rows, in the same places:
  `PIN_BUZZER` (1 row in `set`, 1 in `get`), `ENABLE_WIFI_INTERFACE` (4 and 5),
  `DISPLAY_CLASS` (9 and 9).
- The tables are `const` and hold pointers and small integers, so they live in flash.
  No heap, no RTTI, no `std::function`, no virtual call.

### Order of the rows

The chain was searched top to bottom, so order is part of the behaviour wherever one name
is the start of another. In this chain it is not: no name is the start of another name in
the same table, and no exact name is the start of a prefix name. A one-off script (not
committed) checked every pair of rows in the three tables with all switches on: no pair
can match the same text. The 13d-a note already says the same about the `ui.` keys. A comment above the
tables states the rule for the next row that is added: if it overlaps, the longer name
goes above.

The chain compared every `memcmp()` row against 8 to 25 characters of its name, even for
a short command. The tables compare the four characters of `set ` or `get ` first. A text
that does not start with `set ` or `get ` is therefore compared over fewer bytes than
before. A text that does is compared over the same number.

### Alternatives and why not

| Alternative | Why not |
|---|---|
| Friend struct of static functions (chosen) | One line in `MyMesh.h`; handlers are plain function pointers; the same shape as 13b. |
| Pointers to `MyMesh` member functions, as `CMD_TABLE` of step 10 does for the frame commands | Needs about 38 private declarations in `MyMesh.h`. Possible here because only the companion includes the header, but the handlers are small and the friend struct keeps the header unchanged. |
| One flat table with the full text (`set name `, `get name`, ...) | Closest to the chain and one table less, but it repeats `set ` and `get ` 34 times and compares them in every row. The two level shape is the one 13b uses. |
| A generic handler for the 18 `ui.*` commands (field pointer, format string) | The fields have three types (`uint8_t`, `uint32_t`, `int32_t`), the replies differ from command to command, and `backlight_delay` has three answers. A generic handler would need a field per row and would hide those differences. |
| Non capturing lambdas in the rows | Lambdas at namespace scope have no friend access, and each is its own type. |
| A `CLI_WORD` rule (end of line or a space after the name) | No command of the chain used it. Adding it would change which texts match. |

### Behaviour kept on purpose

Everything in "Quirks pinned as is" under 13d-a is unchanged: prefix matching of the
`ui.*` keys without a space and without an end of word check, `set pin` without a range
check, `set tz.offset nan`, the `get wifi.pwd` clear text answer, the whole word match of
the other `get` commands, and the `strcpy()` in `board`. The handlers contain the same
statements as the branches they replace, with `m._prefs` for `_prefs`, `m.savePrefs()`
for `savePrefs()` and `k.arg` for `&command[n]`. No 13d-a test file was edited.

### Verification of 13d-b

- **13d-a tests, unchanged.** `git diff integration -- test/ platformio.ini .github/` is
  empty. The five companion environments pass with the same number of cases as before
  the change:

  | Environment | Cases before | Cases after |
  |---|---|---|
  | `native_companion` | 123 | 123 |
  | `native_companion_buzzer` | 128 | 128 |
  | `native_companion_wifi` | 144 | 144 |
  | `native_companion_display` | 138 | 138 |
  | `native_companion_all` | 168 | 168 |

- **CI list, 27 environments** (`pio test`, the list from `run-unit-tests.yml`, exit code
  0): 2435 cases, 1 skipped, 2434 passed, 0 failed. The 13d-a run had the same totals.
- **Mutation tests.** 24 single edits of `MyMesh.cpp`, each run against the environment
  that builds the row, then the file restored with `git checkout`; `git status` was
  clean afterwards. 22 turned at least one test red, 2 stayed green. The number of
  failed cases that `pio` reported equals the number of distinct failing tests named in
  the log for every one.

  | # | Mutation | Environment | Red tests |
  |---|---|---|---|
  | T01 | rows `set ` and `get ` swapped in `kTopCmds` | `native_companion_all` | 0 (equivalent) |
  | T02 | `set` key `name ` becomes `name` (prefix one shorter) | `native_companion` | 12 |
  | T03 | `ver` row removed | `native_companion` | 2 |
  | T04 | search loop compares every row with `strcmp()` | `native_companion` | 31 |
  | T05 | `get name` matched by prefix | `native_companion` | 2 |
  | T06 | `get buzzer` row under `ENABLE_WIFI_INTERFACE` | `native_companion_buzzer` | 5 |
  | T07 | `set wifi.clear` row under `DISPLAY_CLASS` | `native_companion_wifi` | 2 |
  | T08 | `shutdown` row removed, `poweroff` stays | `native_companion` | 1 |
  | T09 | `poweroff` row removed, `shutdown` stays | `native_companion` | 3 |
  | T10 | `reboot` row calls the power off handler | `native_companion` | 3 |
  | T11 | `set ` row one character shorter | `native_companion` | 24 |
  | T12 | `get ` row calls the `set` table | `native_companion` | 28 |
  | T13 | `set ui.show_clock` key with a trailing space | `native_companion_display` | 3 |
  | T14 | rows `ui.max_partial` and `ui.nd_name` swapped in `kSetCmds` | `native_companion_display` | 0 (equivalent) |
  | T15 | `set wifi.clear` matched by prefix | `native_companion_wifi` | 1 |
  | T16 | `set wifi.pwd` row calls the SSID handler | `native_companion_wifi` | 6 |
  | T17 | argument starts one character late | `native_companion` | 33 |
  | T18 | unknown `set` or `get` key reported as handled | `native_companion` | 9 |
  | T19 | `get tz.offset` matched by prefix | `native_companion` | 1 |
  | T20 | `set ui.nd_name` row removed | `native_companion_display` | 7 |
  | T21 | `get wifi.ip` row removed | `native_companion_wifi` | 1 |
  | T22 | `board` matched by prefix | `native_companion` | 1 |
  | T23 | `set pin` row calls the `set tz.offset` handler | `native_companion` | 4 |
  | T24 | `set buzzer` row removed | `native_companion_buzzer` | 4 |

  The two green mutations are swaps of rows that cannot match the same text (see "Order
  of the rows"), so the program behaves the same and no test can tell. They are not test
  gaps. The other 22 were caught on the first run. The
  author chose the mutations, as in 13a and 13d-a, so they cover what was tried.
- **Flash and RAM** (`pio run`, `integration` `93d6a811` in a separate worktree against
  this branch, same toolchain, no other change):

  | Environment | Platform, switches set (from the build's defines) | Flash before | Flash after | Flash delta | RAM before and after |
  |---|---|---|---|---|---|
  | `Heltec_t114_without_display_companion_radio_ble` | nRF52, `DISPLAY_CLASS` (null driver) | 396936 | 397672 | +736 | 147804, +0 |
  | `Heltec_t114_companion_radio_ble` | nRF52, `DISPLAY_CLASS` | 422560 | 423280 | +720 | 147892, +0 |
  | `RAK_4631_companion_radio_ble` | nRF52, `DISPLAY_CLASS` | 473256 | 473976 | +720 | 149928, +0 |
  | `WioTrackerL1_companion_radio_ble` | nRF52, `DISPLAY_CLASS` and `PIN_BUZZER` | 411280 | 411856 | +576 | 148624, +0 |
  | `heltec_v4_companion_radio_wifi` | ESP32-S3, `DISPLAY_CLASS` and `ENABLE_WIFI_INTERFACE` | 1111137 | 1112093 | +956 | 172388, +0 |
  | `heltec_v4_r8_companion_radio_wifi` | ESP32-S3, `DISPLAY_CLASS` and `ENABLE_WIFI_INTERFACE` | 1114657 | 1115601 | +944 | 172312, +0 |
  | `heltec_v4_r8_tft_companion_radio_wifi` | ESP32-S3, all three | 1118733 | 1119757 | +1024 | 172464, +0 |

  RAM is unchanged in all seven builds. Flash grows by 576 to 1024 bytes. The smallest
  margin is the nRF52 `RAK_4631_companion_radio_ble`: 66.40 percent of flash before,
  66.50 after. The growth was not broken down further. The compiler printed no warning
  for `MyMesh.cpp` in the three builds that were checked for it before and after.
- **Hardware.** No board was flashed.

### Not fixed (still open)

13d-b changes none of these; the handlers contain the same statements as before.

- The three findings under "Not fixed (13d-a)": `set tz.offset nan`, the clear text
  `get wifi.pwd`, `set pin` without a range check.
- The unbounded `strcpy()` of `board.getManufacturerName()` in the `board` command. It now
  has its own handler (`MyMeshCli::boardName`), so a later length check is a change in
  one place.
- The rescue console's own commands in `checkCLIRescueCmd()`. Its call into
  `handleCommand()` goes through the tables now; its chain of `rebuild`, `erase`, `ls`
  and the others is untouched.

### Gaps and not verified

- **No hardware run.** The tests run on the host (64 bit), and no board was flashed. Size
  was measured on seven firmware builds, not on every companion variant. All seven
  builds set `DISPLAY_CLASS`, so the size of a build without the display commands was not
  measured.
- **Stack use** of the handlers and of `cliRun()` was not measured. A handler call adds a
  `MyMesh&` and a two pointer `CliCall` on the stack.
- **Pairs of switches** are not built on their own in the host tests, as before. The
  firmware builds above cover the pairs buzzer with display and WiFi with display and the
  all-three case; buzzer with WiFi alone is not built anywhere.
- **Mutations** are the author's choice (see above).

## 13d-c: three corrections

13d-a pinned three odd answers of the companion's text CLI and one unbounded copy. Marcel
decided on 2026-11-10: fix `set tz.offset nan`, `set pin` and the `board` copy, one
commit each; leave `get wifi.pwd` as it is. Each correction is a change in one handler of
`struct MyMeshCli` (`examples/companion_radio/MyMesh.cpp`). The tables and the matching
are untouched. Only the three handlers changed, so every other command answers as before.

### 1. `set tz.offset`: NaN is refused

`atof("nan")` is NaN and every comparison with NaN is false, so `tz < -12 || tz > 14`
let it through and the text `nan` was stored. The check now reads `!(tz >= -12 && tz <=
14)`.

| Input | Before | Now |
| --- | --- | --- |
| `set tz.offset nan`, `NaN`, `-nan`, `nan(1)` | `OK`, saved as `nan` | `Error, must be from -12 to +14`, nothing saved, offset unchanged |
| `-12`, `14`, `5.5`, `1e1`, `abc` (reads as 0) | `OK`, saved | same |
| `-12.01`, `14.01`, `1e3`, `inf`, `-inf` | error | same |

`UITask.cpp` casts `tz_offset * 3600` to `int32_t`, which is undefined for NaN. What a
board did with a stored NaN was not measured.

### 2. `set pin`: 0 or six digits

The pin is read with `strtol` into a `long` and checked before it goes into the unsigned
`ble_pin`. The rule is the one of the app's `cmdSetDevicePin()`: 0, or 100000 to 999999.
Anything else answers `Error, pin must be 0 or 6 digits`, saves nothing and leaves
`ble_pin` alone. The reply on success is unchanged (`> pin is now %06d`).

| Input | Before | Now |
| --- | --- | --- |
| `0`, `100000`, `999999`, `123456` | accepted | same |
| `1`, `12`, `12345`, `99999` | accepted, padded (`000012`) | refused |
| `1000000`, `1234567`, `99999999` | accepted | refused |
| `-1`, `-5`, `-100000` | stored as a large unsigned value | refused |
| `4294967296`, 20 digit numbers, 20 digit negatives | `atoi` overflow, undefined | refused |
| `abc`, empty | pin 0, saved | same (kept on purpose) |
| `123456abc` | 123456 | same |
| `12abc` | 000012 | refused (reads as 12) |

Kept on purpose: text without a leading number reads as 0 and is accepted as pin 0, as it
was with `atoi`. Text after the number is ignored. Tests cover 0, 99999, 100000, 999999,
1000000, -1 and numbers beyond `long`.

### 3. `board`: bounded copy

`strcpy(k.reply, board.getManufacturerName())` is now `StrHelper::strncpy(k.reply, name,
40)`: at most 39 characters plus the terminator. The app's device info frame copies the
same name with `strzcpy(..., 40)`.

- **Smallest reply buffer.** The remote CLI (`BaseChatMesh::onContactDataRecv`) hands over
  `&temp[5]` of a `uint8_t temp[166]`, which is 161 bytes. The app path uses
  `reply_buf[166]`. A prefix adds at most 3 characters. A limit of 40 fits both.
- **Names in the tree.** A script over `variants/`, `boards/`, `src/` and `examples/` found
  80 definitions of `getManufacturerName()`. 76 return a string literal; the longest is
  `Ikoka Handheld E22 30dBm (Xiao_nrf52)` with 37 characters
  (`variants/ikoka_handheld_nrf/IkokaNrf52Board.h`). The other four are
  `MANUFACTURER_STRING` (Ikoka nano and stick: six `-D` values in `platformio.ini`, 33 to
  34 characters, the longest `Ikoka Stick-E22-22dBm (Xiao_nrf52)`), `MUZI_BASE_MFR_NAME`
  (`Muzi Base Uno`, `Muzi Base Duo`) and the choice between four names in
  `HeltecV4Board.cpp` (`Heltec V4.3 TFT`, `Heltec V4 TFT`, `Heltec V4.3 OLED`,
  `Heltec V4 OLED`, at most 16 characters). No name is longer than 37, so the answer is
  byte for byte the same on every real board.
- **Test.** `MockBoard` has a settable `manufacturer` (a test-only change in
  `test/mocks/target.h`, reset by `reset()`). A name of 37 and of 39 characters is kept
  whole, a name of 40 and of 500 characters is cut at 39, also behind a prefix, and the
  guard bytes behind the 161 byte reply buffer stay intact.

### Test changes

`git diff integration -- test/ platformio.ini .github/` shows only these:

- `test/mocks/target.h`: `MockBoard::manufacturer`, reset in `reset()`.
- `test_cli_core.cpp`: `SetTzOffsetAcceptsNaNBecauseNoComparisonWithItIsTrue` became
  `SetTzOffsetRefusesNaNLikeAnyOtherValueOutsideTheRange`.
- `test_cli_core.cpp`: `SetPinHasNoRangeCheck` became
  `SetPinAcceptsZeroAndSixDigitsAndRefusesTheRest`; `SetPinReadsWhatAtoiReads` became
  `SetPinReadsTheLeadingNumber`; `SetPinStoresAndSavesAndPadsToSixDigits` uses 100000
  instead of 12.
- `test_cli_core.cpp`: two new tests for `board`.

`platformio.ini` and `.github/` are unchanged.

### Verification of 13d-c

- **Five companion environments**, `pio test -e native_companion -e native_companion_buzzer
  -e native_companion_wifi -e native_companion_display -e native_companion_all`: 701 cases
  before (123, 128, 144, 138, 168), 711 after (125, 130, 146, 140, 170). The 10 new cases
  are the two `board` tests in each of the five environments. The other tests keep their
  count.
- **All 27 environments** of `.github/workflows/run-unit-tests.yml`: 2445 cases, 1
  skipped, 2444 passed, 0 failed (13d-b: 2435, 1 skipped, 2434 passed).
- **Size**, `RAK_4631_companion_radio_ble`, same toolchain, `integration` (`b26d742d`)
  against this branch:

  | | Flash | RAM |
  | --- | --- | --- |
  | before | 473976 | 149928 |
  | after | 474088 (+112) | 149928 (+0) |

- **Mutations**, run in `native_companion` after each commit and reverted afterwards
  (`git status` clean). A run that aborts counts as red.

  | Mutation | Result |
  | --- | --- |
  | tz: old condition `tz < -12 \|\| tz > 14` | red: NaN test |
  | tz: lower bound `-11` | red: 2 tests |
  | tz: upper bound `tz < 14` | red: 2 tests |
  | pin: old `atoi`, no range check | red: 2 tests |
  | pin: lower bound 99999 | red |
  | pin: upper bound 1000000 | red |
  | pin: `>` instead of `>=` at 100000 | red: 2 tests |
  | pin: `<` instead of `<=` at 999999 | red |
  | pin: 0 not accepted | red: 3 tests |
  | pin: cast to `uint32_t` before the check | red (negative numbers wrap) |
  | board: old `strcpy` | red: the run aborts with SIGABRT |
  | board: limit 41 | red |
  | board: limit 39 | red: 2 tests |
  | board: limit 200 | red: the run aborts |
  | board: `strncpy(..., 39)` without a terminator | red: 2 tests |

  The author chose the mutations, as in the earlier steps. The first attempt at the pin
  mutations hit `cmdSetDevicePin()` (the app path, which has the same condition) instead
  of the CLI handler; those runs were thrown away and repeated with a longer anchor.

### Not fixed (13d-c)

- **`get wifi.pwd` answers the WiFi password in clear text.** Marcel decided to leave it.
  Open point: whether the companion apps use the command is not checked, and masking it
  could break them. It is reachable from the app and, over the air, only for a contact
  with the remote CLI flag. Test: `GetPwdAnswersThePasswordInClearText`.
- The rescue console's own commands in `checkCLIRescueCmd()` (`rebuild`, `erase`, `ls`,
  ...) have no tests.

### Gaps and not verified

- **No board was flashed.** The size comes from one build, `RAK_4631_companion_radio_ble`.
- **The effect of a stored NaN on a board** was not measured; the cast is undefined in C++.
- **How the BLE stack treats an out-of-range pin** was not checked. The change refuses
  such values before they get there.
- **Whether the apps send `set pin` with fewer than six digits** was not checked. A
  five-digit pin that worked before is now refused.
- **Name lengths** come from a script over source text. Names built at run time are not
  covered; the four non-literal cases above were read by hand.

## Next steps

13d-c is done. What is left:

- **`get wifi.pwd`:** find out whether the apps use it, then decide between leaving it,
  masking it and closing it for the remote CLI.
- **Later:** the rescue console's own commands in `checkCLIRescueCmd()` (`rebuild`,
  `erase`, `ls`, ...) have no tests yet.
