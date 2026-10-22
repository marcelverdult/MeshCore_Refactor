# 0008: The forwarding checks move into the shared base

Status: implemented. Step 4g of the series described in
[0004](0004-shared-server-base.md), stacked on the sensor's flood hop limits
([0006](0006-sensor-flood-limits.md)), which made this step possible.

It is a move, not a change: the characterization tests written before it pass after
it without being touched.

## Context

`allowPacketForward()` is the hook `mesh::Mesh` asks before it passes a received
packet on. Each server role had its own copy. Once the sensor applied the flood hop
limits like the other roles (0006), the room server's and the sensor's bodies were
the same six lines, and the repeater's began with the same five:

| role | lines | made of |
|---|---|---|
| room server | 6 | forwarding switched off; the flood hop limits |
| sensor | 6 | the same text |
| repeater | 24 | the same first five lines; the Region a flood arrived on; loop detection |

This is how the sensor fell behind before: the hop limits reached the repeater and
the room server and not the third copy.

## What changes

- `ServerMesh` overrides `allowPacketForward()` with the two checks every role makes:
  `disable_fwd`, and `mesh::isFloodHopLimitExceeded()` for floods.
- The room server and the sensor lose their overrides.
- The repeater keeps its override, calls the shared one first, and then checks the
  Region and loops exactly as before.

What does not change:

- **The order.** The shared checks run first in the repeater, as they did. A debug
  build test pins that the Region check is not reached when forwarding is off.
- **Visibility.** The override is protected in `ServerMesh`, as it is in
  `mesh::Mesh` and was in the three roles. No `main.cpp` calls it (checked by grep:
  no match in any of the three).
- **The companion radio**, which has its own `allowPacketForward()` and is not a
  `ServerMesh`.

## Alternatives

- **A hook for the repeater's extra checks** (`allowFloodForward()` or similar,
  called by the shared body) instead of an override that calls the base. Rejected for
  now: one role has extra checks, and an override that calls its base first says the
  same with less machinery. If a second role ever needs its own checks, the hook is a
  small next step.
- **Move the Region check into the base too.** Rejected: it would change what the
  room server and the sensor do with a flood that arrives without a Region (today
  they forward it, see the tests). That is a decision about behaviour, not a move.
- **Leave it.** Rejected: the three copies are the reason the sensor drifted.

## Verification

### Characterization first

`test/mocks/ForwardPolicyChecks.h`, in its own commit before the move, included by the
suite of each role after the boot checks, so one set of expectations holds all three:

| | what is pinned | roles |
|---|---|---|
| switch | forwarding off refuses every route type; a fresh node forwards only on the repeater | all |
| limits | a fresh flood of either kind is forwarded; `flood.max` stops both kinds at the limit (`>=`); `flood.max.unscoped` only un-scoped floods; `flood.max.advert` only adverts; no hop limit applies to a direct route | all |
| Region | a flood without a Region is refused, a direct route is not | repeater |
| loops | minimal loop detection refuses a flood holding this node's hash four times, not three; off by default | repeater |
| order | with forwarding off the Region check is not reached (debug log) | repeater, debug build |
| the others | no Region needed for a flood; loop detection not applied | room server, sensor |

Green on the old bodies: 9 cases in each of `native_server`, `native_sensor` and
`native_repeater`, 10 in `native_repeater_full`.

### Diff proof

Function bodies extracted from the commit before the move and from the move (an awk
range anchored at `^bool <Class>::allowPacketForward(`):

- the room server's and the sensor's removed bodies are identical to the new
  `ServerMesh` body (`diff -w`, no output);
- the repeater's old lines 1 to 5 are identical to the shared body's first five; its
  old lines 6 to 24 are identical to its new lines 2 to 20, after the new first line
  `if (!ServerMesh::allowPacketForward(packet)) return false;`.

The first attempt at the extraction was wrong: an unanchored pattern took the new
call line for the start of a function and skipped it. Caught because the result
showed the repeater's body starting at the Region check.

### Mutation tests

Each against the committed move, the tree restored by copy after every run and
checked clean at the end. `pio test -e native_server -e native_sensor -e native_repeater -e native_repeater_full`.

| # | mutation | caught by |
|---|---|---|
| G1 | the base ignores `disable_fwd` | forwarding off refuses every route, a fresh node forwards only on the repeater, the order test, the sensor's own switch test (all four environments) |
| G2 | the base ignores the hop limits | the three limit tests and the sensor's three (all four) |
| G3 | the hop limits also apply to direct routes | no hop limit on a direct route (all four) |
| G4 | the un-scoped and the advert limit swapped | the un-scoped and the advert tests, shared and sensor |
| G5 | `flood.max` ignored | `flood.max` stops both kinds, and the sensor's own |
| G6 | the repeater does not call the shared checks | five shared tests, repeater environments only |
| G7 | the repeater checks the Region before the shared checks | the order test, `native_repeater_full` only |

All seven are caught, each by a different set of tests. G7 only by the debug build,
which is what that test is for: without the log the two orders refuse the same
packets.

### Host suites

`pio test -e native -e native_kiss_modem -e native_core -e native_ofast -e native_server -e native_sensor -e native_repeater -e native_repeater_full`:
288 of 288 cases pass, 37 of them new.

### Build matrix

The 18 environments of the series, built on the move (`e5553e1d`) and on its base
(`24ce8b5f`, the flood hop limits) with the same toolchain: 36 of 36 succeed. RAM is
unchanged everywhere.

| | flash |
|---|---|
| repeaters, bridges included (seven environments, four platforms) | +32 to +56 |
| room servers and sensors (seven environments) | -4 to +8 |
| companions (four environments) | +0 |

The repeater pays for one extra call, now a call into the base rather than inline
code; the room server and the sensor lose their override and use the base's, which
costs or saves a few bytes depending on the platform. The companion numbers are equal
sizes, not a byte comparison.

GCC: the fork's CI ran the host suites and fourteen firmware builds on the move,
15 of 15 checks green.

## Next candidates

- **The `CommonCLI _cli` member**, constructed with the same arguments in all three
  roles (see 0007).
- **A Region check for floods in the room server and the sensor**, if they should
  refuse a flood without a Region as the repeater does. A behaviour decision, not a
  move.
