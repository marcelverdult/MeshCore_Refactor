# A scoped flood with transport code 0 ignored `region denyf *`

## Context

`RegionMap::findMatch()` decides which Region a `ROUTE_TYPE_TRANSPORT_FLOOD`
packet belongs to. The three server roles call it from `onRecvPacket()` with
`REGION_DENY_FLOOD` as the mask and keep the result in `recv_pkt_region`
(`examples/simple_repeater/MyMesh.cpp:558`,
`examples/simple_room_server/MyMesh.cpp:311`,
`examples/simple_sensor/SensorMesh.cpp:525`, all on `upstream/dev` 428a95c6).

An un-scoped flood (`ROUTE_TYPE_FLOOD`) never goes through `findMatch()`. The
same `onRecvPacket()` looks at the wildcard itself:

```cpp
} else if (pkt->getRouteType() == ROUTE_TYPE_FLOOD) {
  if (region_map.getWildcard().flags & REGION_DENY_FLOOD) {
    recv_pkt_region = NULL;
  } else {
    recv_pkt_region =  &region_map.getWildcard();
  }
}
```

So `region denyf *` takes the Region away from an un-scoped flood. Since
`01f9293b` ("treat transport code 0 the same as un-scoped", meshcore-dev/MeshCore
PR 3568, merged 13 Oct 2026 and part of the v1.18.0 releases) `findMatch()` starts
with

```cpp
if (packet->transport_codes[0] == 0) return &wildcard;
```

(`src/helpers/RegionMap.cpp:191`). The wildcard is not in `regions[]`, so the
mask is applied to every Region but never to it, and a scoped flood that carries
code 0 keeps the wildcard as its Region however the wildcard is set. Before that
commit code 0 fell through the loop, no Region owns it, and the result was NULL.

The commit message is the title alone, the pull request has no description and
no discussion, and nothing else explains the difference. The title says "the
same as un-scoped", and an un-scoped flood respects the denial. The CLI
reference says the same about the setting: `region denyf *` "drops packets
without region transport codes" (`docs/cli_commands.md:796`), and
`flood.max.unscoped` applies to "a packet without a scope" (`:696`).

### What the Region is used for

Only the repeater acts on it when forwarding: `allowPacketForward()` refuses a
flood whose `recv_pkt_region` is NULL (`examples/simple_repeater/MyMesh.cpp:440`).
The room server and the sensor forward without looking at the Region
(`simple_room_server/MyMesh.cpp:300`, `SensorMesh.cpp:319`). In all three roles
`sendFloodReply()` uses it to pick the scope of a flooded reply, through
`chooseReplyScope()` (`src/helpers/RoutingPolicy.h:60`): a request that arrived
on the wildcard is answered un-scoped, one that arrived on no Region is answered
on the default scope when there is one.

### Who sends code 0

The firmware does not build such a frame itself. `TransportKey::calcTransportCode()`
maps a computed 0x0000 to 0x0001 (`src/helpers/TransportKeyStore.cpp:12`), and
the four places that call `sendFlood(pkt, codes, ...)` all sit behind it
(`companion_radio/MyMesh.cpp:498`, `simple_repeater/MyMesh.cpp:1006`,
`simple_room_server/MyMesh.cpp:748`, `simple_sensor/SensorMesh.cpp:916`).
`CMD_SEND_RAW_PACKET` of the companion (`companion_radio/MyMesh.cpp:2045`) hands a
host supplied frame to `sendPacket()` after `tryParsePacket()`, which reads the
four code bytes of a route type 0x00 frame as they are. Any companion client can
therefore put a code 0 flood on the air. Whether anyone does is not measured.

This is not a security boundary. Region keys come from the Region name, so
anyone can scope a flood to a Region that is allowed. What the code 0 route
changes is that no name has to be known: it gets past `denyf *` unscoped.

## Decision

Keep the shortcut and let the mask apply to it, the way it applies to every
Region in the loop:

```cpp
if (packet->transport_codes[0] == 0) {   // no scope: handled like an un-scoped flood...
  return (wildcard.flags & mask) == 0 ? &wildcard : NULL;   // ...so the wildcard's own flags decide
}
```

With the wildcard allowing flood (the default) nothing changes. With
`denyf *` a code 0 flood now ends up where an un-scoped flood does: no Region.
The mask is the one the caller passed, not a fixed `REGION_DENY_FLOOD`, so the
function keeps meaning "does this Region allow what the caller asks about".

## Alternatives considered

- **Check the wildcard in each role's `onRecvPacket()`.** Rejected. It is three
  copies on `upstream/dev` (identical apart from the class name), the callers
  already pass the mask for exactly this purpose, and the next role would have
  to remember it.
- **Remove the shortcut again.** Rejected. Code 0 would be dropped by every
  repeater whatever the wildcard says, which is the opposite of what the title
  of `01f9293b` asks for.
- **One predicate for "is un-scoped" used by `findMatch()`, the hop limit and
  the reply scope.** The right shape in the long run, because "un-scoped" is
  decided in three places independently (see below). It needs a new method on
  `Packet` and changes in every role, which is a larger change than the defect
  asks for. Not done here.

## Compatibility

- Settings, files and the protocol are untouched, so there is nothing to migrate
  and a downgrade needs nothing.
- Repeaters with the wildcard allowing flood behave as before. On a repeater
  with `region denyf *` a flood with code 0 is no longer forwarded.
- Room servers and sensors forward as before. Their reply to a code 0 request
  under `denyf *` goes out on the default scope when one is set, which is what a
  reply to an un-scoped request does today, instead of un-scoped.
- In a mixed mesh a repeater with this change drops what v1.18.0 repeaters with
  `denyf *` still forward, which is what happens to un-scoped floods already.
- The companion does not call `findMatch()` and is not affected.

## Verification

### Host tests

New `test/test_region_map` drives `RegionMap::findMatch()` with the real
`RegionMap.cpp`, `TransportKeyStore.cpp` and `Packet.cpp`. Eight cases: code 0
with the wildcard allowing and denying flood, code 0 with other masks, an unknown
code with and without a Region in the table, the code of an allowed Region, a
Region that denies flood, and a denied wildcard in front of an allowed scope.
It has its own small stubs (`test/test_region_map/stubs`), listed first on the
include path, because the shared SHA256 mock leaves `finalizeHMAC()` empty and
would hand `calcTransportCode()` an uninitialised code.

| | before the change | after |
|---|---|---|
| `native_region` | 2 of 8 red | 8 of 8 green |

The six cases that pass on the old code are guards: they pin what the change must
not touch (the loop, the mask, an allowed wildcard).

All suites of this branch, `pio test -e native -e native_kiss_modem -e native_region`,
come out green (PlatformIO reports 62 of 62 cases). The workflow now runs `native_region` too; without that the new
suite would not have run in CI.

### Mutations

Seven deliberate errors, each had to turn the suite red:

| Mutation | Red test |
|---|---|
| shortcut ignores the mask again (the old code) | `CodeZeroHasNoRegionWhenTheWildcardDeniesFlood`, `CodeZeroIsOnlyHeldBackByTheFlagTheMaskNames` |
| mask test inverted | three tests |
| code 0 never matches | `CodeZeroArrivesOnTheWildcardWhenItAllowsFlood`, `CodeZeroIsOnlyHeldBackByTheFlagTheMaskNames` |
| fixed `REGION_DENY_FLOOD` instead of the given mask | `CodeZeroIsOnlyHeldBackByTheFlagTheMaskNames` |
| wildcard denial applied to every scoped flood | `ADeniedWildcardDoesNotHoldBackAScopeThatIsAllowed` |
| loop matches any code | `ACodeThatBelongsToNoRegionHasNoRegionWhileRegionsExist` |
| loop ignores the mask | `ARegionThatDeniesFloodIsSkippedForItsOwnCode` |

### Effect on the server base series

The server base series (`refactor/server-base`, `refactor/sensor-align`) pins the
old behaviour in `test/test_server_base/test_server_base.cpp`,
`RegionResolutionTest.TransportCodeZeroIgnoresTheWildcardsFloodDenial`. With this
change applied to a copy of `refactor/sensor-align` 0a93bc40, `native_server`
runs 33 tests, 32 green and that one red (the second expectation, "scoped flood
with code 0 does not"); `native`, `native_core`, `native_kiss_modem`, `native_ofast`
and `native_sensor` stay green (PlatformIO reports 90 of 90 cases). That test has to be turned around when
the two lines of work meet: it should expect no Region for the scoped flood, and
its name no longer fits. `RegionMap.cpp` is the same blob on `upstream/dev`,
`integration`, `refactor/sensor-align` and `refactor/server-base`, so the change
merges without a text conflict in that file.

### Builds

All 18 environments of the matrix build with this change, and so does
`upstream/dev` 428a95c6 in its own worktree with the same toolchain. They are the
roles that link `RegionMap.cpp` (repeater, room server, sensor, companion) on every
platform of the matrix.

| Environment | Build | RAM against dev | Flash against dev |
|---|---|---|---|
| `Heltec_v3_companion_radio_ble` | OK | +0 | +0 |
| `Heltec_v3_companion_radio_wifi` | OK | +0 | +0 |
| `Heltec_v3_repeater` | OK | +0 | +12 |
| `Heltec_v3_room_server` | OK | +0 | +8 |
| `Heltec_v3_sensor` | OK | +0 | +8 |
| `PicoW_repeater` | OK | +0 | +16 |
| `ProMicro_companion_radio_usb` | OK | +0 | +0 |
| `ProMicro_repeater` | OK | +0 | +0 |
| `ProMicro_room_server` | OK | +0 | +0 |
| `ProMicro_sensor` | OK | +0 | +0 |
| `RAK_3112_repeater_bridge_espnow` | OK | +0 | +8 |
| `RAK_4631_companion_radio_ble` | OK | +0 | +0 |
| `RAK_4631_repeater` | OK | +0 | +0 |
| `RAK_4631_room_server` | OK | +0 | +0 |
| `RAK_4631_sensor` | OK | +0 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | OK | +0 | +16 |
| `wio-e5-mini_repeater` | OK | +0 | +8 |
| `wio-e5-mini_sensor` | OK | +0 | +8 |

RAM is unchanged everywhere. The ESP32, RP2040 and STM32 server builds grow by 8
to 16 bytes (`findMatch()` itself, 0x70 to 0x79 bytes on the ESP32-S3). Two groups
come out at +0, for different reasons:

- The companion builds never call `findMatch()`; the symbol is not in their images
  at all, before or after (`nm` finds none in `Heltec_v3_companion_radio_wifi` and
  `RAK_4631_companion_radio_ble`).
- The nRF52 server builds (`RAK_4631_*`, `ProMicro_*`) contain the change, but the
  function is 0xda bytes before and after. The disassembly of `findMatch()` in
  `RAK_4631_repeater` differs from the fifth instruction on (the mask is kept in
  `r8` instead of `r9`), so the extra compare fits into the same size.

### What was not measured

- Nothing was run on a board or on the air. The repeater's forwarding gate
  (`simple_repeater/MyMesh.cpp:440`) is read from the source; there is no host
  harness for the repeater role. The choice `findMatch()` makes is covered by
  the host test, and what a role does with it is covered for the room server by
  the series' `native_server` suite.
- The reply scope change for the room server and the sensor is read from
  `chooseReplyScope()`, not measured.
- Whether any client sends code 0 floods on the air is not known.

## Related

- **The hop limit has the same hole.** `isFloodHopLimitExceeded()` counts only
  `ROUTE_TYPE_FLOOD` as un-scoped (`src/helpers/RoutingPolicy.h:19`), so a code 0
  flood also passes `flood.max.unscoped`, the alternative to `denyf *` that
  `docs/cli_commands.md:700` recommends. Same cause, different function, left
  alone here.
- **Open pull request 2960** (runtime region gating by duty cycle) replaces the
  flag test in this loop and in the un-scoped branch with `effectiveFlags()`. A
  merge will not conflict with this change, so the wildcard check added here has to
  use `effectiveFlags()` too, or code 0 will walk past that gate the same way.
- **Open pull request 1791** (a default "any" Region) changes the end of
  `findMatch()` and does not overlap.
