# 0013: A command table for the text CLI

Status: 13a (host harness and characterization) and 13b (the table) built.
Behaviour change: none. 13a adds tests and build environments only; 13b replaces
the `else if` chains in `src/helpers/CommonCLI.cpp` with const tables, adds three
tests and leaves the 13a tests unchanged.

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

## Next steps

The companion has its own text CLI, `MyMesh::handleCommand()`
(`examples/companion_radio/MyMesh.cpp`, from about line 2389): 38 commands in a chain
of `if` blocks that match whole words with `strcmp()` or a prefix with a space
(`set name `, `set wifi.ssid `). It starts with the same two hooks as `CommonCLI`
(the radio settings table, then `board.handleCommand()`). It is not part of this step.
The harness pattern carries over: mock board and callbacks, a reply buffer with
canaries, one source set built in several environments. Step 10a already runs
`MyMesh` on the host (`test/test_companion_frames`), so the dependencies exist. That
makes it a later candidate (13d or higher); it has not been scoped beyond counting the commands.
