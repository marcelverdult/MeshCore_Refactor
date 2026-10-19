# Architecture review of v1.18.0

Read-only review of MeshCore v1.18.0 (`companion-v1.18.0` = `repeater-v1.18.0` =
`room-server-v1.18.0`, commit `81dbae59`) and of its history since v1.14.0.
The question was: **why is adding a feature expensive, and which refactors
would make it cheaper without changing behaviour?**

Every number below was measured on the source tree or with `git` and can be
reproduced with the command given. File and line references point into v1.18.0;
later steps of this series move some of that code. Findings that came only from reading code
and were not reproduced on hardware say so.

## How the cost shows up

| Change | Commit | Files touched | Reproduce |
|--------|--------|---------------|-----------|
| CAD as its own setting (`set cad on/off`) | `7c8e0924` | 15 | `git show --stat 7c8e0924` |
| Remote LNA / RX gain toggle | `71136671` | 171 (162 in `variants/`) | `git show --stat 71136671` |
| Same feature moved into the radio wrappers, 23 days later | `9a95e25e` | 164 (156 in `variants/`) | `git show --stat 9a95e25e` |
| One line added to every `radio_init()` (preamble) | `f0ec5d2a` | 66 (64 in `variants/`) | `git show --stat f0ec5d2a` |
| Build flag for USB companion firmwares | `e7f2c717` | 70 | `git show --stat e7f2c717` |

Churn between v1.14.0 and v1.18.0 (`git log --no-merges --oneline companion-v1.14.0..companion-v1.18.0 -- <file> | wc -l`):
`examples/companion_radio/MyMesh.cpp` 80, `src/helpers/CommonCLI.cpp` 43,
`examples/simple_repeater/MyMesh.cpp` 35, `examples/simple_room_server/MyMesh.cpp` 29.

## Findings

Each finding names the place where the same knowledge exists more than once,
or where a value cannot reach the code that needs it.

### F1. Settings are spelled out in many places

- Two different classes are both called `NodePrefs`: `src/helpers/CommonCLI.h:25`
  (repeater, room server, sensor) and `examples/companion_radio/NodePrefs.h:14`
  (companion). Their radio adapter (`RadioPrefs`, 60 lines) was a copy that differed
  only in one comment (`CommonCLI.h:77-137` vs `NodePrefs.h:63-124`).
- The core reads settings only through virtual getters that every role
  re-implements as `return _prefs.x;` (`src/Dispatcher.h:166-173`, `src/Mesh.h:72`).
  `Dispatcher::loop()` pushes CAD and the interference threshold to the radio every
  2 seconds (`src/Dispatcher.cpp:16`, `:67-71`).
- `CommonRadioPrefs.cpp:4` includes the variant's `target.h` and calls the global
  `radio_driver` directly (`:129`, `:143`), so the radio CLI could not be built
  on the host.
- The role callbacks `setTxPower()` and `setRxBoostedGain()` declared in
  `CommonCLICallbacks` have no caller anywhere in `src/`, `examples/` or `variants/`.

Step 1 shares the radio and GPS settings between both classes; step 2 lets the
core read its tuning values from them instead of a getter per role. The
remaining points are candidates for the next steps.

### F2. Server roles carry the same code three times

Repeater, room server and sensor each implement the same plumbing.
For example `sendFloodReply()` is identical in all three except for whitespace
in one comment (`diff -w` is empty), and the tested `mesh::chooseReplyRoute()`
(`src/helpers/RoutingPolicy.h:39`) is used in one place only
(`examples/simple_repeater/MyMesh.cpp:600`).
The copies have drifted (found by reading the code, not reproduced on hardware):

- The sensor's `allowPacketForward()` checks only `flood_max`
  (`examples/simple_sensor/SensorMesh.cpp:319-323`); the repeater also applies
  `flood.max.unscoped`, `flood.max.advert`, the region rule and loop detection
  (`examples/simple_repeater/MyMesh.cpp:434-460`).
- The sensor does not override `getExtraAckTransmitCount()`, whose base returns 0
  (`src/Mesh.cpp:25`), so `set multi.acks` is accepted but has no effect there.

Since step 2 the sensor states this explicitly (`getExtraAckTransmitCount()`
returning 0 with a comment), so the gap is documented rather than accidental, and
`SensorMesh::sendAckTo()` still carries a branch that the base's 0 makes
unreachable.

#### Why the sensor drifted

Checked with `git log -S` and the commit history of `upstream/dev` 428a95c6 before
deciding whether to align the sensor. No deviation has a role specific reason such
as battery life. Each one is a change that reached the repeater and the room server
and skipped the sensor. The history below is from the commits; the run time effects
are read from the source and not reproduced on a board:

| deviation | history |
|---|---|
| Retransmit delay `nextInt(0, 6)*t` | The sensor sketch copied it verbatim from the repeater at its birth (`7fb7b69b`, 7 July 2025, where the repeater had the same line). The repeater and the room server left the six slot scheme in `8cbcd227` (29 October 2025), whose subject calls itself experimental, and got the divide by zero fix `96e786fa` a day later. The sensor keeps the old shared behaviour. The lines were touched once more, in `3e76161e` (19 February 2026, `path_len` to `getPathByteLen()`), in all three roles including the sensor, so only the formula itself was never revisited. |
| Flood limits only `flood_max` | `fad11c90` (3 August 2026) moved the repeater and the room server to `mesh::isFloodHopLimitExceeded()`, with the shared helper in `src/helpers/RoutingPolicy.h` and `test/test_routing_policy`. The sensor was not touched. |
| `multi.acks` without effect | `6bc8dd28` and `5881b04a` (16 July 2025) added the setting and the companion's use of it. `136f733d` (27 August 2025) added a consumer to `SensorMesh::sendAckTo()`, but no role side provider, so the base's 0 keeps that branch dead. |
| RX boosted gain not applied at boot | The room server had the same gap until `a71cc238` (21 July 2026) fixed it by mirroring the repeater. The sensor still initialises no default and applies nothing in `begin()`, so a value set through the CLI applies at once but is lost on the next boot. |

`nextInt(_min, _max)` returns `(num % (_max - _min)) + _min` (`src/Utils.cpp:18`), so
the upper bound is exclusive: the sensor draws from the six multiples 0 to 5 times
`t`, the other roles uniformly from 0 to `5*t`. Both have the same maximum. The
`+ 1` of `96e786fa` exists because `t == 0` made the bound 0 and `num % 0` crashed,
which the sensor's form could not do.

### F3. Board variants are near copies

89 `variants/*/target.cpp` files; `radio_new_identity()` is identical (ignoring
repeated spaces) in 74 of them. The FEM gain handlers (`attachDynamicPrefs()` + `handleCommand()`) of
HeltecV4, HeltecV4R8, T096 and HeltecTrackerV2 are identical once the class name is
normalized. 618 PlatformIO environments; the pull request build check builds 14.

### F4. Radio chip classes are near copies

`isReceiving()` exists in seven copies of about 41 lines; after normalizing chip
names, the SX1262, SX1268, LLCC68 and STM32WLx copies are identical, and the
LR1110 and LR1121 copies are identical (`src/helpers/radiolib/Custom*.h`).

### F5. The companion protocol is one long function

`MyMesh::handleCmdFrame()` (`examples/companion_radio/MyMesh.cpp:1055-2063`) is
1009 lines with 62 `if (cmd_frame[0] == CMD_...)` branches. Length checks are
written per branch.

### F6. Little of this can be tested on the host

The native test environment builds four source files (`platformio.ini`,
`[env:native]`). None of the role code, the persistence code or the radio CLI was
covered before step 1.

## Issues noticed while reading (not part of this series)

Found by reading v1.18.0, not reproduced on hardware. Each should get its own
issue or fix.

| # | Issue | Where |
|---|-------|-------|
| 1 | `CMD_SEND_CHANNEL_TXT_MSG` has no minimum length check; with a 3 to 5 byte frame, `len - i` is negative and reaches `memcpy` in `sendGroupMessage()` | `examples/companion_radio/MyMesh.cpp:1178`, `src/helpers/BaseChatMesh.cpp:527-528` |
| 2 | `CustomLLCC68Wrapper::doResetAGC()` calls `sx126xResetAGC()` with one argument; the function takes two. No environment builds this wrapper | `src/helpers/radiolib/CustomLLCC68Wrapper.h:38`, `SX126xReset.h:8` |
| 3 | LR1110, LR1121 and LR2021 wrappers do not override `packetScore()`, so the base assumes SF10 | `src/helpers/radiolib/RadioLibWrappers.h:76` |
| 4 | `ESPNOWRadio::setRxBoostedGainMode()` returns `bool` but has no `return` | `src/helpers/esp32/ESPNOWRadio.h:41` |
| 5 | Seven variant GPS loops keep `next_gps_update` in a signed `long` and compare `millis() > next_gps_update`; the shared sensor manager fixed exactly this stall after the ~24.8 day sign flip in `63731d3f`, the variant copies did not get the fix | e.g. `variants/heltec_tracker/target.cpp:74-85` |
| 6 | `extra_sf` and `discovery_mod_timestamp` are not part of `/prefs.json` | `src/helpers/CommonCLI.h:65`, `:74` |
| 7 | The server roles load `/prefs.json` without range checks (only the legacy loader clamps) | `src/helpers/CommonCLI.cpp:38` |
| 8 | Role fallback radio defaults (915 / 250 / SF10) differ from the root `platformio.ini` (869.618 / 62.5 / SF8) | `examples/simple_repeater/MyMesh.cpp:7-13`, `platformio.ini` |
| 9 | nRF52 firmware is built with `-Ofast` (`-ffast-math`), so float comparisons may assume no NaN; whether a range check rejects `nan` then depends on how it is written. `set af nan` is accepted on the boards. (Found during step 3, measured on a T-Echo) | `src/helpers/CommonRadioPrefs.cpp` |
