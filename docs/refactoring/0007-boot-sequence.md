# 0007: The boot sequence moves into the shared base

Status: implemented. This is step 4f, the last of the series 4a to 4f described in
[0004](0004-shared-server-base.md).

Of everything the three server roles carried in triplicate, `begin()` was the
largest body left. It is a move, not a change: the order of every call is kept, and
the tests that pin it passed before the move and after it without being touched.

## Context

`begin()` is the boot sequence of a node: it starts the dispatcher, loads what the
node stored, establishes the default scope, gives the radio and the board the
settings it holds, and starts the advert timers. Before this step each role had its
own copy, 53 lines in the repeater and 44 in the room server and in the sensor.

After the sensor alignment ([0005](0005-sensor-alignment.md)) closed the RX boosted
gain gap, the room server's and the sensor's bodies are the same text, and the
repeater's differs in two blocks. Measured on the commit this step started from,
comparing each old body with the shared one by `diff -w -B` (blank lines and
whitespace ignored, see "Diff proof" below):

| role | lines | differs from the shared body by |
|---|---|---|
| room server | 44 | class name, `loadPrefs()`, one comment, two hook calls |
| sensor | 44 | the same |
| repeater | 53 | class name, `loadPrefs()`, and two blocks replaced by hook calls |

The two repeater blocks are the `WITH_BRIDGE` start of the bridge and one debug
print of the gain mode the radio ended up in.

## What changes

`ServerMesh` gets a public `begin(FILESYSTEM*)` with the sequence, and the roles lose
theirs. Everything a role must say for itself becomes a protected virtual on the
base:

| hook | default | the role that overrides it |
|---|---|---|
| `loadPrefs()` | none, pure virtual | all three: `_cli.loadPrefs(_fs)`, the line each had. A role owns its `CommonCLI`, so the base cannot call it |
| `beginBridge()` | does nothing | the repeater, under `WITH_BRIDGE`: starts the bridge if `bridge_enabled`, the block it had |
| `onRadioConfigured()` | does nothing | the repeater: the debug print of the applied gain mode |

`applyGpsPrefs()` goes with the body. It was the same inline line in all three role
headers (`sensors.setSettingValue("gps", ...)`, checked by grep: three matches across
the three old headers and one in the base), and `begin()` was its only caller, so it
moves to `ServerMesh` as a protected method behind `ENV_INCLUDE_GPS`.

The order, which the tests pin, is:

1. `mesh::Mesh::begin()`, which starts the dispatcher and the radio driver
2. the file system pointer is stored
3. the prefs are loaded (`loadPrefs()`)
4. the ACL is loaded
5. the Regions are loaded
6. the default scope is established from the stored default Region, or from
   `DEFAULT_FLOOD_SCOPE_NAME` if the build names one
7. the bridge is started (`beginBridge()`)
8. the radio gets its parameters, TX power and boosted gain mode
9. `onRadioConfigured()`
10. the board gets its dynamic prefs
11. both advert timers are set
12. the board gets its ADC multiplier
13. the GPS toggle is pushed to the sensor manager (`ENV_INCLUDE_GPS`)

What does not change:

- **The order.** Every call is where it was. Sections of it are load bearing; see
  "The order is part of the behaviour" below.
- **Visibility.** `begin()` stays public because each role's `main.cpp` calls it. The
  hooks are protected on the base and overridden in the roles' non-public sections,
  so nothing outside the three roles sees a new name. This is the lesson of step 4b,
  where an access level nearly broke the firmware build in a place the host harness
  does not compile.
- **The roles' behaviour.** The room server and the sensor gain two empty virtual
  calls; nothing else.
- **One comment.** `// TODO: key_store.begin();` stood in the repeater's copy only.
  It is true of all three roles, since no role starts the key store, so it now stands
  in the shared body.

## Alternatives

- **Each role keeps its `begin()` and calls shared steps.** Rejected. The order of the
  steps is the thing worth sharing, and this keeps it written out three times; a
  future reordering would again have to be made in three places.
- **One virtual hook with a stage argument** (`onBootStage(AFTER_LOADING)` and so on)
  instead of two. Rejected for readability: two named hooks say where they run.
- **Move the `CommonCLI` into the base**, which removes `loadPrefs()` and the three
  copies of `savePrefs()` together. The three constructions are already identical
  (`_cli(board, rtc, sensors, region_map, acl, &_prefs, this)` in all three), so this
  is possible, but the base would need the board in its constructor, which changes
  the constructor of `ServerMesh` and of the three roles. That is more than a move.
  Listed as a next candidate.
- **Log the gain mode in all three roles** by putting the print into the shared body.
  It would change what the room server and the sensor print in a debug build, and a
  step that promises no change does not take that decision. The hook keeps the print
  where it was; making it shared is one line if it is wanted.

## The order is part of the behaviour

Writing the tests turned up an ordering that a reader of the three copies would not
notice and that a careless move could have changed: `mesh::Mesh::begin()` runs
**before** the prefs are loaded.

`Dispatcher::begin()` sizes the transmit budget from the airtime factor
(`src/Dispatcher.cpp:25-28`). At that point the node holds the factor its constructor
set (1.0), not the one on flash, so the budget starts at the size that factor gives
(half of the one hour window). `updateTxBudget()` reads the stored factor, and every
budget decision calls it first (`Dispatcher.cpp:96` and `:282`): if the stored factor
is stricter than 1.0 the budget is capped to the stored maximum before it is used, and
if it is looser the budget starts below its cap and refills at the stored rate. So
the effect is a start-up level, not a lasting setting.

This is read from the code. It is not measured on a board, and it is probably
harmless, which is why the test pins it as observed behaviour without endorsing it,
and why it is in the ledger as an observation and not as a fix (issue 15). The point
for this step is the order: the first two lines of `begin()` cannot be swapped in a
move without changing what the dispatcher starts with.

## Verification

### Characterization first

The tests were written and run on the three old `begin()` bodies, in their own
commit, before anything moved. One set of expectations,
`test/mocks/BootSequenceChecks.h`, is included by the suite of each role, so the
three are held to the same behaviour. They watch the boot from outside.

To make the order observable the mocks keep one ordered event log
(`test/mocks/EventLog.h`): the file system lookups, the calls on the radio, the board
and the sensor manager, and a stand-in RS232 bridge all append a tag when called. The
sensor manager mock now records the settings pushed to it.

The repeater had no host environment, because it shares the name `MyMesh` with the
room server. `native_repeater` adds it. `native_repeater_full` runs the same suite
with every optional block of `begin()` compiled in: a bridge, the GPS toggle, a
configured default scope and the debug log. Both are in the CI test run.

| | what is pinned | environments |
|---|---|---|
| order | the dispatcher first; prefs, then ACL, then Regions, all before the radio; params, power, gain, then the board's prefs and ADC multiplier; each exactly once | all |
| the quirk | the dispatcher sizes its budget from the constructor's airtime factor | all |
| values | stored radio settings, gain, ADC multiplier and advert intervals reach the radio, the board and the timers; the stored ACL (with its secrets recalculated) and Regions load | all |
| scope | a stored default Region becomes the default scope; none without one | all |
| scope, configured | the named Region is created with flood allowed, reused with its flags, loses to a stored default, and is not written to flash | `native_repeater_full` |
| GPS | the stored setting is pushed last, on and off; nothing is pushed without GPS support | `native_repeater_full`, the others |
| repeater only | the bridge starts after loading and before the radio, and not when disabled; the debug build logs the applied mode, after applying it | `native_repeater_full` |
| housekeeping | booting an empty file system writes nothing; the role can save its prefs right after `begin()` | all |

20 boot cases run in each of the three default environments and 28 in
`native_repeater_full`.

### Diff proof

Each old body extracted from the commit before the move and compared with the shared
one (`diff -w -B`):

```
OLD repeater vs NEW shared
1c1   void MyMesh::begin(FILESYSTEM *fs) {      ->  void ServerMesh::begin(FILESYSTEM* fs) {
5c5   _cli.loadPrefs(_fs);                       ->  loadPrefs();
30-34 #if defined(WITH_BRIDGE) ... bridge.begin(); ... #endif   ->  beginBridge();
40-41 MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: ...);           ->  onRadioConfigured();

OLD room server vs NEW shared (the sensor's diff is the same)
1c1   void MyMesh::begin(FILESYSTEM *fs) {      ->  void ServerMesh::begin(FILESYSTEM* fs) {
5-6   _cli.loadPrefs(_fs); <blank>               ->  loadPrefs();
7a7   (nothing)                                  ->  // TODO: key_store.begin();
29a30 (nothing)                                  ->  beginBridge();
32a36 (nothing)                                  ->  onRadioConfigured();
```

Every line is accounted for. The repeater's replaced blocks are the bodies of its
overrides in `examples/simple_repeater/MyMesh.h`, character for character (the
`MESH_DEBUG_PRINTLN` call and the `if (_prefs.bridge_enabled) { bridge.begin(); }`).

### Mutation tests

Fifteen deliberate faults in the committed code, one at a time, run against the four
environments they can affect. Every one turns the suite red. The faults were applied
to a copy and restored from it after each run, never with `git checkout`, because
that would also have removed any uncommitted work.

| # | fault | caught by |
|---|---|---|
| M1 | ACL and Regions loaded in the other order | `PrefsAreLoadedBeforeTheAclAndTheAclBeforeTheRegions`, all four |
| M2 | `Mesh::begin()` moved after loading the prefs | `TheDispatcherStartsBeforeAnythingIsLoaded`, `TheDispatcherSizesItsBudget...`, `PrefsAreLoadedBefore...` |
| M3 | TX power set before the radio parameters | `TheRadioIsSetUpInTheOrderParamsPowerGainThenTheBoard` |
| M4 | ADC multiplier no longer applied | `EachStepOfTheSetUpHappensExactlyOnce`, `TheStoredAdcMultiplierReachesTheBoard`, the order test |
| M5 | advert timers computed before the prefs are loaded | `AdvertTimersCountFromTheStoredIntervals`, `ZeroIntervalsLeaveBothTimersOff` |
| M6 | bridge started after the radio is configured | `TheBridgeStartsAfterTheLoadingAndBeforeTheRadioIsConfigured` |
| M7 | `onRadioConfigured()` before the gain is applied | `TheRepeaterLogsTheGainModeAfterApplyingIt` |
| M8 | a stored default Region no longer yields the default scope | `AStoredDefaultRegionBecomesTheDefaultScope`, all four |
| M9 | the auto-created default Region left denying flood | `TheConfiguredDefaultScopeIsCreatedWhenNoneIsStored` |
| M10 | the GPS setting pushed with inverted polarity | `AStoredGpsOffIsPushedAsOff`, `TheStoredGpsSettingIsPushedToTheSensorManagerLast` |
| M11 | the bridge started although it is disabled | `ADisabledBridgeIsNotStarted` |
| M12 | the sensor loads its prefs from nowhere | seven boot cases and `SensorRoleTest.AStoredBoostedGainSurvivesAReboot` |
| M13 | the room server loads its prefs from nowhere | seven boot cases |
| M14 | the repeater loads its prefs from nowhere | ten boot cases, both repeater environments |
| M15 | the file system pointer is null while the prefs load | segmentation fault in all four environments |

M5 was first written as an extra, earlier call to the timers with the original
calls left in place. That mutant survived, correctly: the later calls overwrite the
earlier ones, so it changes nothing observable. It was rewritten as a real move of
the calls and then turned the suite red. Of the fifteen, M12 to M14 are the ones
that show the shared checks reaching each role's own hook.

### Host suites

`pio test -e native -e native_ofast -e native_server -e native_sensor -e native_core
-e native_kiss_modem -e native_repeater -e native_repeater_full`: 211 of 211 test
cases pass, the same count before and after the move. The CI runs all eight; its GCC
build is the only run of the tests under GCC, since this machine has clang only.

As a partial substitute, the boot test files of the three suites and of
`native_repeater_full` were syntax checked (`-fsyntax-only`, host mocks and the
googletest headers) with the `arm-none-eabi-g++` of three PlatformIO toolchains:
7.2.1, 12.3.1 and 14.2.1. Zero errors in all twelve combinations, and a positive
control (a role type that does not exist) gives 18 errors on the same command. That
is a type check against newlib, not a run on Linux, so it does not replace the CI.

## The build matrix

Run on 20 October in three worktrees at exact commits, the same toolchains, one
`pio run` per environment: `dev` (`428a95c6`), the commit this step started from
(`0a93bc40`, steps 4a to 4e and the first half of the sensor alignment, called 4e in
the table) and the commit that carries the code of this step (`133fe68b`). The commits
after it change documentation only. **66 of 66 builds succeeded, none failed.**

The 18 environments of the series matrix, plus four that close gaps in it: the 18
contain no RP2040 room server, nothing on the pioarduino ESP-IDF 5 toolchain (which
also compiles these files), and no RS232 bridge on nRF52. That makes 22 per commit.

| environment | RAM | vs 4e | vs dev | Flash | vs 4e | vs dev |
|---|---|---|---|---|---|---|
| `Heltec_v3_repeater` | 62512 | +0 | +0 | 1173909 | +56 | -540 |
| `Heltec_v3_room_server` | 66200 | +0 | +0 | 1169205 | +52 | -500 |
| `Heltec_v3_companion_radio_ble` | 194844 | +0 | +8 | 1666053 | +48 | -384 |
| `Heltec_v3_companion_radio_wifi` | 172384 | +0 | +8 | 1102225 | +48 | -452 |
| `Heltec_v3_sensor` | 60600 | +0 | +40 | 1180417 | +84 | -176 |
| `RAK_4631_repeater` | 37240 | +0 | +0 | 441048 | +192 | +96 |
| `RAK_4631_room_server` | 40928 | +0 | +0 | 434232 | +192 | +144 |
| `RAK_4631_companion_radio_ble` | 149768 | +0 | +8 | 462196 | +0 | -352 |
| `RAK_4631_sensor` | 35328 | +0 | +40 | 437816 | +208 | +272 |
| `PicoW_repeater` | 85368 | +0 | +0 | 492132 | -24 | +160 |
| `wio-e5-mini_repeater` | 17172 | +0 | +0 | 203584 | -24 | +32 |
| `wio-e5-mini_sensor` | 15276 | +0 | +40 | 203040 | -24 | +136 |
| `RAK_3112_repeater_bridge_espnow` | 64072 | +0 | -8 | 1175209 | +84 | -508 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | 26704 | +0 | +0 | 240544 | -24 | +176 |
| `ProMicro_repeater` | 33892 | +0 | +0 | 345504 | +192 | +80 |
| `ProMicro_room_server` | 37588 | +0 | +0 | 338584 | +160 | +128 |
| `ProMicro_companion_radio_usb` | 99468 | +0 | +8 | 356964 | +0 | -336 |
| `ProMicro_sensor` | 31988 | +0 | +40 | 342140 | +192 | +240 |
| `waveshare_rp2040_lora_room_server` | 28532 | +0 | +0 | 232084 | -32 | +112 |
| `M5Stack_Unit_C6L_repeater` | 29752 | +0 | +0 | 552061 | -64 | -36 |
| `M5Stack_Unit_C6L_room_server` | 33448 | +0 | +0 | 546661 | -70 | -62 |
| `Heltec_t096_repeater_bridge_rs232` | 37148 | +0 | +0 | 415824 | +208 | +64 |

RAM did not move in any of the 22 environments from 4e to 4f. Against `dev` the
deltas are those of the earlier steps and nothing of this one: `+32` for the sensor
from step 4e (its peer index grew from 24 to `MAX_CLIENTS` entries), `+8` on the
companion and the sensor from step 2 (recorded in [0002](0002-core-reads-radio-prefs.md)), and `-8` on `RAK_3112_repeater_bridge_espnow`,
which 0004 already lists as not explained. The flash column against `dev` is the sum of
all steps and is shown for orientation; the column to read for this step is "vs 4e".

### Reproducing the numbers recorded in 0004

0004 prints, per environment, the series base's RAM and flash and the delta to the
tip. Rebuilt, the 4e column here equals base plus delta for RAM in all 18
environments and for flash in 14 of 18. The four that differ are the sensors, whose
recorded state also carried the flood limit half of the alignment
(`refactor/sensor-flood-limits`, not on this branch). Building those four sensor
environments on that branch's tip reproduces the recorded numbers exactly:

| environment | RAM | Flash |
|---|---|---|
| `Heltec_v3_sensor` | 60600 | 1180401 |
| `RAK_4631_sensor` | 35328 | 437672 |
| `wio-e5-mini_sensor` | 15276 | 203112 |
| `ProMicro_sensor` | 31988 | 342012 |

(Recorded in 0004: Heltec 60568 + 32 and 1179737 + 664, RAK 35296 + 32 and
437416 + 256, wio-e5-mini 15244 + 32 and 202424 + 688, ProMicro 31956 + 32 and
341772 + 240, which sum to the values above.)

### What the flash deltas of this step are

RAM is unchanged, so the whole difference between 4e and 4f is flash. Symbol sizes
(`nm -S`) and section sizes (`size -A`) from the ELF files, per toolchain:

| group | flash vs 4e | what the symbols show |
|---|---|---|
| ESP32-S3 servers (Heltec v3 x3, RAK 3112 bridge) | +52 to +84 | Code shrinks: `.flash.text` -8, -28 and -24 on repeater, room server and sensor. The three old `begin()` bodies (263 to 275 B each) become one of 176 B, plus `loadPrefs()` 16 B, `applyGpsPrefs()` 28 B and the empty hooks at 5 B. `.flash.rodata` grows by 64, 80 and 108: 24 B of that are the two vtables (12 B each, three new virtual slots), the rest (40 to 72 B) is **not attributed**; the rodata of the whole image shifts, and no new string or symbol accounts for it |
| nRF52 servers (RAK 4631 x3, ProMicro x3, Heltec t096 bridge) | +160 to +208 | The shared `begin()` is 456 to 488 B where each old one was 316 to 352 B. The disassembly of `RAK_4631_repeater` shows why: the two advert timer updates, which the role copies called through the vtable, are inlined in the shared body (two calls to `Dispatcher::futureMillis` appear, none before). That is the compiler's decision in a new translation unit under nRF52's `-Ofast`, probably speculative devirtualization; **not verified** with `-fno-devirtualize-speculatively`. On top: `loadPrefs()` 16 B, vtables 24 B, empty hooks 4 to 6 B. For `RAK_4631_repeater` that is 186 of the 192 B, the other 6 are not attributed; `ProMicro_room_server` shows +160 for a symbol sum of +184, a 24 B residual that is **not attributed** |
| RP2040, STM32 servers | -24 to -32 | `.text` -48 and `.rodata` +24 (the two vtables), measured on `PicoW_repeater` and `wio-e5-mini_repeater`. The shared body is 212 B (RP2040) and 168 B (STM32) against 292 and 244 for the old ones |
| ESP32-C6 servers (pioarduino) | -64 and -70 | `.flash.text` -80, `.flash.rodata` +16 on the repeater; the unwind tables (`.eh_frame` +64) are not part of the flash figure |
| Companion, nRF52 (RAK 4631, ProMicro) | +0 | No section differs at all |
| Companion, ESP32-S3 (Heltec v3 x2) | +48 | `.flash.text` is byte for byte the same size; only `.flash.rodata` moves, which is layout, as in 0004: `ServerMesh` is not in a companion image |

The pattern in the table is the expected one for a move that routes the
role-specific parts through three new virtual calls: a shared body that comes out
bigger or smaller depending on how the compiler handles it, plus 24 B of vtable per
image. The `+160` to `+208` on nRF52 is the one a reviewer should
look at; it is a one time cost of 0.04 percent of a 440 kB image.

### Other examples that compile `ServerMesh.cpp`

`helpers/*.cpp` is in the base source filter of every firmware environment, so
`ServerMesh.cpp` is compiled for the kiss modem and the terminal chat too, and this
step added `board.attachDynamicPrefs()` and `sensors.setSettingValue()` to it.
Whether those compile for every variant's board was checked in two ways.

First by argument: every one of the 91 variant directories with a `-I variants/<name>`
flag in some environment has at least one environment that compiles a server role or
the companion, and both already call `board.attachDynamicPrefs()` on that variant's
board, so the board types are proven. (Computed from `pio project config
--json-output`.)

Second by building one kiss modem or terminal chat environment per platform on `dev`,
4e and 4f. All 27 builds succeeded.

| environment | Flash dev | Flash 4e | Flash 4f | 4f vs 4e | RAM 4f vs 4e |
|---|---|---|---|---|---|
| `RAK_4631_kiss_modem` | 371560 | 371560 | 371560 | +0 | +0 |
| `Heltec_v3_kiss_modem` | 579993 | 580561 | 580609 | +48 | +0 |
| `PicoW_kiss_modem` | 439420 | 439420 | 439420 | +0 | +0 |
| `wio-e5-mini_kiss_modem` | 151032 | 151032 | 151032 | +0 | +0 |
| `M5Stack_Unit_C6L_kiss_modem` | 479807 | 479807 | 479807 | +0 | +0 |
| `PicoW_terminal_chat` | 468108 | 468908 | 468908 | +0 | +0 |
| `ProMicro_terminal_chat` | 295312 | 298112 | 298112 | +0 | +0 |
| `Heltec_v3_terminal_chat` | 600685 | 601517 | 601565 | +48 | +0 |
| `RAK_4631_terminal_chat` | 393416 | 393960 | 393960 | +0 | +0 |

Step 4f changes nothing in seven of the nine and moves the two ESP32-S3 ones by +48, the same layout shift as in the companion. Against `dev` the terminal chat
environments are larger by 544 to 2800 B and the ESP32-S3 kiss modem by 568 B, already
at 4e. That comes from the steps before this one; these environments were not part of
the series matrix. **Not examined and not explained** here; it is outside this step
and listed under "What was not verified".

### The debug line

`onRadioConfigured()` keeps the repeater's one debug print. Built with `-D MESH_DEBUG=1`
for `RAK_4631_repeater`, `RAK_4631_room_server` and `RAK_4631_sensor` at 4e and at 4f,
(nRF52 only) the sorted set of strings containing `DEBUG: ` or `Boosted` is identical in each pair
(117, 112 and 103 strings, empty diff), `RX Boosted Gain Mode` occurs once in the
repeater image and not in the room server or the sensor, before and after. The
room server and the sensor did not gain a log line.

## What was not verified

- **No board test.** The host environments model the boot sequence, not the radio, the
  board or the flash. See the next section.
- **GCC on Linux.** Only the CI runs the tests under GCC; locally there is a syntax
  check with three arm GCC versions, see above.
- **The debug line on the host for the room server and the sensor.** Their host
  environments do not define `MESH_DEBUG`; the firmware string comparison above is the
  evidence that they print nothing new.
- **The effect of the budget ordering** (ledger issue 15) is read from the code.
- **The unattributed bytes** named in the flash table: 40 to 72 B of rodata on
  ESP32-S3 and a 24 B residual on one nRF52 environment.
- **The growth of the terminal chat and kiss modem images** before this step.
- **Variants other than those built.** Argued from the variant directories, built for
  the 22 + 9 environments above, not for all 626.

## Board test

### Done, sensor role only

Run on `techo-b43b` (LilyGo T-Echo, SX1262, sensor role) after the merge into
`integration`, with marked images of the merge commit (`4cf3161b`, "4f") and of its
first parent (`3e685785`, "before"), both built with
`tools/refactoring/diag-sensor-rxgain-acks.patch` for `diag sensor` (stored RX gain,
the radio's RX gain register, extra ACK count) and for freeing a stuck I2C bus at boot
(ledger issue 14). Each step read all 30 server settings plus `region default` and
`diag sensor`:

1. Original 1.17.1 to "before": all settings identical.
2. "Before", `set radio.rxgain off`, `set multi.acks 1`, reboot: the register reads
   off and one extra ACK is used, as the sensor alignment intends.
3. "Before" to "4f": every reading identical, the register still off.
4. Reboot on "4f": identical to step 3, so the moved boot path reproduces itself.
5. "4f" to "before" (downgrade): identical.
6. Back to the original 1.17.1 with the original values: 31 of 31 lines identical to
   the reading taken before.

So the moved `begin()` loads the same prefs and pushes the stored RX gain to the radio
on real hardware. The I2C bus was found stuck at two of these boots and freed by the
diagnostic patch; that is ledger issue 14 and independent of this step.

### Not covered yet

The repeater and the room server were not run on a board, and the sensor run used
default values for everything but the RX gain and `multi.acks`. The rest of the plan
below is still open:

- **Images.** Two per role, same toolchain, marked with
  `PLATFORMIO_BUILD_FLAGS='-D FIRMWARE_BUILD_DATE=\"<tag>\"'` so `ver` shows which
  one runs: the 4e commit (`0a93bc40`) and the 4f commit (`133fe68b`). The T-Echo has
  `LilyGo_T-Echo_repeater` and `LilyGo_T-Echo_room_server`; there is no T-Echo sensor
  environment in the tree, so a sensor test needs the image the sensor alignment test
  uses, built at both commits.
- **What to compare.** Set non-default values first (a small TX power change, the two
  advert intervals, the ADC multiplier; no frequency change), reboot, and read them
  back (`get tx`, `get radio`, `get advert.interval`, `get flood.advert.interval`,
  `get adc.multiplier`, `get radio.rxgain` on an SX126x board). The reads must be the
  same on both images. Then check that the first scheduled advert comes after the
  stored interval, and that the file list on flash is the same before and after
  (`ls`), since booting must write nothing.
- **Upgrade and downgrade.** 4e to 4f and back, with the values set on the first. This
  step changes no persisted format, so the files must carry over untouched.
- **What only a board shows.** That the real `board.attachDynamicPrefs()`, the radio
  driver's `setRxBoostedGainMode()` and `setAdcMultiplier()` run in the same order on
  the real driver, which the host mocks record but cannot prove for hardware.

## Next candidates

- **`allowPacketForward()`.** Done in step 4g, [0008](0008-forward-policy.md), once
  the sensor applied the flood hop limits.
- **The `CommonCLI _cli` member.** Constructed with the same arguments in all three
  roles. Moving it into the base removes `loadPrefs()` and the three copies of
  `savePrefs()`, at the price of the board in `ServerMesh`'s constructor.
- **Sharing the gain log line**, if it is wanted in the room server and the sensor.

