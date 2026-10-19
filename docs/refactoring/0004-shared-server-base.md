# 0004: A shared base for the three server roles

Status: proposal, not implemented. This note is the analysis and the cut plan for
steps 4a to 4f; each step gets its own branch and its own note on landing.

## Context

Repeater, room server and sensor each carry their own copy of the same server
underpinnings. Candidate 2 of the v1.18 architecture review estimated 350 to 400
duplicated lines. This note replaces that estimate with a measurement.

Measured on `refactor/radio-cli-table` (the base of this step), by extracting every
top level member function of the three role implementations and comparing the bodies
after stripping comments, whitespace and the class name:

| file | lines |
|---|---|
| `examples/simple_repeater/MyMesh.cpp` | 1328 |
| `examples/simple_room_server/MyMesh.cpp` | 1069 |
| `examples/simple_sensor/SensorMesh.cpp` | 1102 |

### Identical in all three roles

73 lines per role, so 146 lines exist only as copies:

| function | lines per role |
|---|---|
| `onRecvPacket` | 14 |
| `sendFloodReply` | 20 |
| `sendFloodScoped` | 10 |
| `startRegionsLoad` | 6 |
| `saveRegions` | 3 |
| `onDefaultRegionChanged` | 7 |
| `formatStatsReply` | 3 |
| `formatRadioStatsReply` | 3 |
| `formatPacketStatsReply` | 4 |
| `setTxPower` | 3 |

### Identical in all three apart from cosmetics or one role value

83 lines per role, so 166 more lines exist only as copies. The differences are
pointer star placement, whitespace and redundant parentheses, with three exceptions
listed under "Real differences" below:

`applyTempRadioParams` (9), `getPeerSharedSecret` (9), `saveIdentity` (12),
`sendSelfAdvertisement` (12), `updateAdvertTimer` (7), `updateFloodAdvertTimer` (7),
`formatFileSystem` (12), `searchPeersByHash` (9), `createSelfAdvert` (6).

### Identical in repeater and room server, absent from the sensor

50 lines per role, so 50 more lines exist only as copies, and the sensor lacks the
capability altogether: `clearStats` (5), `dumpLogFile` (15), `getLogDateTime` (8),
`logRxRaw` (8), `logTxFail` (11), `setRxBoostedGain` (3). Near identical in the same
two roles: `logRx` (25 and 19), `logTx` (24 and 18), `openAppend` (9 and 9).

### The boot sequence

`begin()` is 53, 44 and 43 lines and 87 to 96 percent identical. The only real
differences are the repeater's `WITH_BRIDGE` block, the sensor's missing RX boosted
gain apply and a debug print the room server does not have.

### Role specific, not shared

`handleRequest` (167, 67, 111 lines; 23 to 55 percent similar), `onPeerDataRecv`
(102, 167, 98), `onAnonDataRecv` (51, 88, 28) and `loop` (44, 85, 116) differ by
role and are out of scope. `handleCommand` (90, 93, 106; 78 to 88 percent) looks
shareable but mixes role commands into one dispatcher, so it is deferred.

So the measurement supports the review: 312 lines of the three role files are pure
copies of each other, rising to about 420 if the logging block the sensor lacks is
counted, and about 500 if the boot sequence is unified too.

## Real differences found while comparing

Three differences are behaviour, not formatting. They are the reason this is a
series of small steps and not one move.

1. `searchPeersByHash`: the sensor bounds its loop with `n < MAX_SEARCH_RESULTS`
   because its `matching_peer_indexes` holds 24 entries, while the repeater and the
   room server size the array `MAX_CLIENTS` (32) and need no bound. Neither can
   overflow: `ClientACL` holds `clients[MAX_CLIENTS]` and caps `num_clients` at
   MAX_CLIENTS in both `load()` and `putClient()`, so `getNumClients()` never
   exceeds 32. The sensor can, however, report only 24 of up to 32 matching
   clients. One array of `MAX_CLIENTS` in the shared base removes the truncation.
2. `formatFileSystem`: the repeater and the sensor erase with `InternalFS.format()`
   under `NRF52_PLATFORM || STM32_PLATFORM`, the room server only under
   `NRF52_PLATFORM`, so an STM32 room server would hit the `#error` branch. This is
   latent today: the two STM32 variants (`wio-e5-mini`, `wio-e5-dev`) define no
   room server environment, so the branch is never compiled.
3. `createSelfAdvert`: differs only by the advert type the role builds
   (`ADV_TYPE_REPEATER`, `ADV_TYPE_ROOM`, `ADV_TYPE_SENSOR`), the classic role
   parameter.

## The cut

A new class `ServerMesh` in `src/helpers/`, between `mesh::Mesh` and the three role
classes, holding the members all three already declare separately (the ACL, the
region map and its temp instance, the key store, the default scope, the received
packet's region, the advert timers, the temp radio state) and the shared methods.
Role differences become either a constructor argument (the advert type) or a small
virtual hook.

Each step is a branch of its own, stacked in order, each a candidate for a separate
upstream pull request:

- **4a Region and reply scope plumbing.** Move `onRecvPacket`, `sendFloodReply`,
  `sendFloodScoped`, `startRegionsLoad`, `saveRegions`, `onDefaultRegionChanged`.
  All six are byte identical in all three roles, so the move is provable by diff.
  63 lines per role.
- **4b Advert schedule.** Move `createSelfAdvert` (advert type as a constructor
  argument), `sendSelfAdvertisement`, `updateAdvertTimer`, `updateFloodAdvertTimer`.
  32 lines per role.
- **4c Temp radio.** Move `applyTempRadioParams` and the revert branch of `loop()`.
  The revert lives inside three different `loop()` bodies, so this step reads the
  three loops carefully rather than moving a whole function.
- **4d Identity, file system and stats.** Move `saveIdentity`, `formatFileSystem`
  (with the STM32 guard of the repeater and the sensor, which fixes difference 2),
  `formatStatsReply`, `formatRadioStatsReply`, `formatPacketStatsReply`,
  `setTxPower`, `clearStats`.
- **4e ACL peer lookup.** Move `searchPeersByHash` and `getPeerSharedSecret` with
  one array of `MAX_CLIENTS` and one bound, which fixes difference 1.
- **4f Boot sequence.** Move `begin()` with a hook for the repeater's bridge block,
  after the sensor's RX gain gap is closed (see below), so the three boot paths are
  the same code.

The packet logging block (`logRx`, `logTx`, `logRxRaw`, `logTxFail`,
`getLogDateTime`, `dumpLogFile`, `openAppend`) is left for a later step: giving it to
the sensor is a behaviour change, not a move.

## The sensor, decided separately

Marcel's decision of 19 October is to align the sensor with the other two roles, on
its own branch and with its own note, because it changes behaviour. Checking the
upstream history first, as that decision required, found no role specific reason for
any of the four deviations; each one is a change that reached the repeater and the
room server and skipped the sensor. Details are in the ledger.

Step 2 already made two of the deviations explicit, so on this base the alignment is
small:

- `getExtraAckTransmitCount`: delete the sensor's override. The core returns
  `_radio_prefs->multi_acks` since step 2, so deleting three lines makes
  `multi.acks` effective and brings the already present branch in
  `SensorMesh::sendAckTo` to life.
- `getRetransmitDelay` and `getDirectRetransmitDelay`: delete both overrides. The
  core applies `nextInt(0, 5*t + 1)`, the formula of the other roles. This is the
  one open point, see below.
- `allowPacketForward`: use `mesh::isFloodHopLimitExceeded`, so
  `flood.max.unscoped` and `flood.max.advert` take effect as on the other roles.
- RX boosted gain: initialise the pref under the same guards as the other roles and
  apply it in `begin()`, mirroring upstream's own room server fix.

## What the series achieved

Steps 4a to 4e have landed. Measured on the role implementations, against the state
before the series (`98ffa5c1`):

| file | before | after |
|---|---|---|
| `examples/simple_repeater/MyMesh.cpp` | 1328 | 1137 |
| `examples/simple_room_server/MyMesh.cpp` | 1069 | 879 |
| `examples/simple_sensor/SensorMesh.cpp` | 1102 | 919 |
| `src/helpers/ServerMesh.h` and `.cpp` | 0 | 328 |

564 lines left the roles, 328 arrived in the base, and what remains exists once
instead of three times. The host suite grew from 82 to 115 test cases.

What is still shared between all three roles, and why it stayed:

- `begin()` is step 4f, which waits on the sensor's RX boosted gain gap being
  closed first, so the three boot paths can become one.
- `allowPacketForward()` is part of the sensor alignment, not of a move.
- `createSelfAdvert()` is a role hook by design now: only the advert type differs.
- `handleCommand()`, `handleRequest()`, `onAnonDataRecv()`, `onPeerDataRecv()` and
  `loop()` differ by role and are out of scope, as stated above.
- `onPeerPathRecv()` is 90 percent shared between the repeater and the room server
  but only about 30 percent with the sensor, so it was never part of this cut.

## Verification of the steps

Marcel's decision of 19 October: the 18 environment build matrix and the flash and
RAM comparison against `dev` run **once, at the end of the 4a to 4f series**, not per
step. Every step is a move inside the same three roles, so one matrix at the end
covers what six would. Each step still carries its host tests, its mutation table and
its diff proof, and a step is not called verified until the matrix has run.

## Open points

1. **The retransmit formula: decided, the sensor keeps its own.** Marcel's decision
   of 19 October, after the upstream history was checked: the sensor stays on the six
   slot scheme, and the retransmit delay becomes the one role parameter of the shared
   base. The reason is that upstream moved the repeater and the room server off that
   scheme in `8cbcd227`, whose subject calls itself experimental, and never
   revisited or confirmed it in the eleven and a half months since. Adopting it for
   the sensor would change radio timing on battery nodes on an experimental basis,
   which is exactly the case the decision reserved for a role profile.

   So the sensor alignment covers `multi.acks`, the flood limits and the RX boosted
   gain, and leaves `getRetransmitDelay()` and `getDirectRetransmitDelay()` as
   documented role overrides.
2. **How deep the characterization goes: settled by measuring it.** The question was
   whether the real role classes can be driven on the host at all, or whether the
   steps have to lean on the diff alone. Probing the compiler rather than estimating
   showed the gap was small: three headers the native build lacked (a file system, an
   RTC, the LittleFS global) and a handful of methods and globals missing from the
   existing mocks. That is now built, and all three role implementations compile
   natively; `native_server` runs the real room server, and the first six
   characterization tests pin the temporary radio parameters.

   What remains open is only how far to carry it:
   - The repeater and the sensor each need an environment of their own, because the
     repeater and the room server both define `MyMesh`.
   - The deeper paths (`handleRequest`, `onPeerDataRecv`) need packets and a
     populated ACL, which is more harness than the steps in this note require.

   This also answers review finding F6 ("Little of this can be tested on the host")
   for the server roles, which was not the goal of this step but is the larger gain.
