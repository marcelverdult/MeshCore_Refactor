# The packet parsers read header fields before testing the length

Status: fixed, measured on the host, no hardware run (ohne Brett). Behaviour change:
none. Every packet is accepted or refused as before, and an accepted packet has the
same fields; only the bytes the parsers read change.

## Symptom

`Dispatcher::tryParsePacket()` (`src/Dispatcher.cpp`) turns raw bytes into a
`mesh::Packet`. Every received radio packet goes through it (`checkRecv()`), and so
does every companion `CMD_SEND_RAW_PACKET` frame (code 65,
`MyMesh::cmdSendRawPacket()`). For the two route types with transport codes
(`ROUTE_TYPE_TRANSPORT_FLOOD` 0 and `ROUTE_TYPE_TRANSPORT_DIRECT` 3) it copies the
4 code bytes and reads the path length byte before it compares anything with `len`.
For the other two it reads the path length byte unconditionally, and for `len == 0`
the header.

`Packet::readFrom()` (`src/Packet.cpp`) has the same shape and goes one step
further: it also copies the whole path, up to 64 bytes, before its only length test
(`if (i >= len)`).

The answer was never wrong. Every packet that is too short for its header is still
refused by the later test, so nothing that was read past `len` ever reached a
caller. Step 10c measured this with AddressSanitizer on the companion
(`docs/refactoring/0010-companion-command-table.md`, "Open findings", point 1),
and the same probe on `integration` 07fa9c5c, before this change:

```
$ python3 tools/refactoring/companion_asan_probe_summary.py probe_before.log
...
10 ('MyMesh::cmdSendRawPacket', 'mesh::Dispatcher::tryParsePacket', 'READ') codes ['65'] fills ['00', '3f'] lens 4 .. 6
```

Frames of 4 to 6 bytes are a raw packet of 2 to 4 bytes after the code and the
priority. Fills `00` and `3f` are the two headers with version 0 and a route type
with transport codes.

## Where it is reached

| Caller | Input | Read past `len` before the fix |
|---|---|---|
| `Dispatcher::checkRecv()` | every radio packet, `len` 1 to 255 | yes, into the rest of the 256 byte stack buffer |
| `MyMesh::cmdSendRawPacket()` | companion frame, raw `len` 2 to 174 | yes, into the tail of the previous frame in `cmd_frame` |
| `RS232Bridge::loop()` via `readFrom()` | bridge frame with a valid checksum, `len` 0 to 256 (256 arrives as 0: `readFrom()` takes a `uint8_t`) | yes, into the checksum and older bytes of `_rx_buffer` |
| `ESPNowBridge::onDataRecv()` via `readFrom()` | ESP-NOW frame with a valid checksum | yes, into the stack copy `decrypted` |
| `BaseChatMesh::importContact()` via `readFrom()` | companion `CMD_IMPORT_CONTACT` | no: the command needs 99 bytes, the parser reads at most 70 |
| `BaseChatMesh::shareContactZeroHop()` via `readFrom()` | an advert the node stored itself | no in practice: the blob was written by `writeTo()` |

In none of them does the read leave the buffer it is in, so this is a read of stale
or unrelated bytes, not an out of bounds access. What was read only decided which
of two `return false` lines refused the packet. Whether it has any effect on a
board beyond that, for example on timing, is not measured.

## Fix

Both parsers test the length before each read:

- `tryParsePacket()`: `len < 1` is refused before the header is read. After the
  version check, which stays first, a packet shorter than its fixed fields (header,
  transport codes if any, path length byte: 6 or 2 bytes) is refused with the
  existing "partial or corrupt packet" debug line. The path test that follows is
  unchanged.
- `readFrom()`: the same two tests, and the existing `i >= len` test moves in front
  of the path `memcpy` as `i + bl >= len`. It is the same condition, since `i` is
  only advanced by `bl` after it.

### Alternatives considered

- **A `FrameReader` over the raw bytes**, as step 10c uses for the companion
  frames. Rejected for this fix: `Packet.cpp` and `Dispatcher.cpp` are in every
  build, and a structural change of the parsers belongs in its own step with its
  own review, not in a bounds fix.
- **One shared parser for both.** They differ on purpose: `readFrom()` needs one
  payload byte at least and checks no version, `tryParsePacket()` accepts an empty
  payload and refuses versions above `PAYLOAD_VER_1`. Merging them would change
  which packets are accepted.
- **Only `tryParsePacket()`.** Rejected: `readFrom()` reads further past `len`
  (the whole path) and is reached from both bridges.

## Verification

### Host tests

`test/test_packet_parse` runs in `native_core`, which builds the real `Packet.cpp`,
`Dispatcher.cpp` and `Mesh.cpp`. It has three parts:

- **Guard page.** Each input is placed at the very end of a readable page with a
  `PROT_NONE` page behind it, for every route type, every `len` from 0 to 72 and 13
  path length bytes. Each case runs in a death test child, which exits with code 3
  on `SIGSEGV`/`SIGBUS`, so a read at or above `len` fails that case.
- **Reference.** The code before the fix is kept in the test, verbatim except for
  its debug output, and runs on a buffer large enough for every read it makes. Both
  parsers are compared with it for all 16 pairs of version and route type, all 256
  path length bytes, `len` 0 to 80 and 180 to 256, and two fills behind `len`. The
  result must be the same, and for an accepted packet the header, transport codes,
  path length, path, payload length and payload must be equal. The test also
  requires both outcomes to occur at least 50,000 times, so it cannot pass vacuously.
- **Radio receive.** The same inputs through `Dispatcher::loop()` with a fake radio
  that fills the whole receive buffer with a stale byte first: a packet the
  reference accepts reaches `onRecvPacket()` with the same fields, any other goes
  back to the pool.

`CommandTest.SendRawPacketShortFramesOfEveryRouteType` in `test_companion_frames`
sends raw packets of 2 to 10 bytes for every route type and four path length
bytes, after a full length frame of `5a` or `c0` (or nothing) has left its bytes
in `cmd_frame`. A packet is taken exactly when its fixed fields and path are inside
the frame, and goes out byte for byte as it came in. The frame sweep golden
(`frame_sweep.golden`) is unchanged.

Before the fix (the tests of d748d432 with `src/` of 07fa9c5c), the guard page
cases fail exactly where the analysis says, each with exit code 3, and nothing
else fails:

```
tryParsePacket route 0 lens 0 .. 5    route 1 lens 0 .. 1
tryParsePacket route 2 lens 0 .. 1    route 3 lens 0 .. 5
readFrom       route 0 lens 0 .. 69   route 1 lens 0 .. 65
readFrom       route 2 lens 0 .. 65   route 3 lens 0 .. 69
PacketParse.*MatchesReference, RadioReceiveMatchesReference   PASSED
CommandTest.SendRawPacketShortFramesOfEveryRouteType          PASSED
```

For `readFrom()` the limit is the longest path among the tested path length bytes,
`0x60` (32 hashes of 2 bytes, 64 bytes): 6 + 64 bytes with transport codes, 2 + 64
without.

After the fix all of them pass.

### Mutation tests

Each mutation on its own, `pio test -e native_core -f test_packet_parse`:

| Id | Mutation | Failing cases |
|---|---|---|
| M0 | both files as before the fix | `ReadFromReadsNothingPastLen`, `TryParsePacketReadsNothingPastLen` |
| D1 | `tryParsePacket()`: fixed field test removed | `TryParsePacketReadsNothingPastLen` |
| D2 | `tryParsePacket()`: 6 becomes 5 | `TryParsePacketReadsNothingPastLen` |
| D3 | `tryParsePacket()`: 6 becomes 7 | `RadioReceiveMatchesReference`, `TryParsePacketMatchesReference` |
| D4 | `tryParsePacket()`: 2 becomes 1 | `TryParsePacketReadsNothingPastLen` |
| D5 | `tryParsePacket()`: 2 becomes 3 | `RadioReceiveMatchesReference`, `TryParsePacketMatchesReference` |
| D6 | `tryParsePacket()`: `len < 1` removed | `TryParsePacketReadsNothingPastLen` |
| P1 | `readFrom()`: fixed field test removed | `ReadFromReadsNothingPastLen` |
| P2 | `readFrom()`: 6 becomes 5 | `ReadFromReadsNothingPastLen` |
| P3 | `readFrom()`: 6 becomes 7 | none, equivalent (below) |
| P4 | `readFrom()`: `i + bl >= len` becomes `>` | `ReadFromMatchesReference` |
| P5 | `readFrom()`: path test removed | `ReadFromMatchesReference`, `ReadFromReadsNothingPastLen` |
| P6 | `readFrom()`: `len < 1` removed | `ReadFromReadsNothingPastLen` |

`D3` is also caught by `SendRawPacketShortFramesOfEveryRouteType` and by the frame
sweep golden in `native_companion`.

`P3` is an equivalent mutant: a packet with transport codes and exactly 6 bytes has
no payload byte, so the path test refuses it whatever the first test says.
`readFrom()` with `2 -> 3` is equivalent for the same reason and was not run.

### AddressSanitizer

`tools/refactoring/companion_asan_probe.patch` with `PROBE_SKIP=1` (the patch needs
`git apply -C1` on this base, its context in `platformio.ini` has moved). After the
fix the `cmdSendRawPacket` / `tryParsePacket` line is gone; the three
`cmdRunCliCommand` lines of open finding 2 stay, with the same counts:

```
150 ('MyMesh::cmdRunCliCommand', '', 'READ') codes ['66'] ...
630 ('MyMesh::cmdRunCliCommand', 'CommonRadioPrefs::handleCommand', 'READ') codes ['66'] ...
220 ('MyMesh::cmdRunCliCommand', 'MyMesh::handleCommand', 'READ') codes ['66'] ...
```

### Native environments

All 22 `native*` environments of `platformio.ini` (the 17 of
`.github/workflows/run-unit-tests.yml` and five more) on c0a25f58, on macOS and in
a Linux container (`python:3.12-slim` with `pip install platformio`, the tree
copied in with `git archive`):

```
macOS  793 test cases: 1 skipped, 792 succeeded in 00:09:55.643
Linux  793 test cases: 1 skipped, 792 succeeded in 00:13:11.503
native_core  test_packet_parse  PASSED   (both)
```

The skipped case is `ChipWrapperTest/7.SetParamsGivesTheChipItsDeadlines`, which
skips itself for the SX1276 (`GTEST_SKIP`), as on the base.

### Builds

A repeater, a room server and a companion per platform (ESP32, nRF52, RP2040,
STM32), plus the two bridge repeaters that reach `readFrom()`, built from
07fa9c5c and from c0a25f58 with the same toolchain. The STM32 board `wio-e5` has
no room server environment. All 26 builds succeed, RAM is unchanged everywhere,
flash grows by the two length tests:

| Environment | Flash 07fa9c5c | Flash fix | Delta | RAM 07fa9c5c | RAM fix |
|---|---:|---:|---:|---:|---:|
| `Heltec_v3_repeater` | 1,174,961 | 1,174,977 | +16 | 62,512 | 62,512 |
| `Heltec_v3_room_server` | 1,170,101 | 1,170,113 | +12 | 66,200 | 66,200 |
| `Heltec_v3_companion_radio_ble` | 1,671,589 | 1,671,605 | +16 | 194,844 | 194,844 |
| `Heltec_v3_repeater_bridge_espnow` | 1,185,425 | 1,185,445 | +20 | 64,168 | 64,168 |
| `RAK_4631_repeater` | 441,916 | 441,932 | +16 | 37,240 | 37,240 |
| `RAK_4631_room_server` | 435,068 | 435,084 | +16 | 40,928 | 40,928 |
| `RAK_4631_companion_radio_ble` | 465,176 | 465,208 | +32 | 149,768 | 149,768 |
| `RAK_4631_repeater_bridge_rs232_serial1` | 407,992 | 408,024 | +32 | 38,144 | 38,144 |
| `RAK_11310_repeater` | 236,172 | 236,180 | +8 | 24,728 | 24,728 |
| `RAK_11310_room_server` | 232,124 | 232,140 | +16 | 28,432 | 28,432 |
| `RAK_11310_companion_radio_usb` | 240,532 | 240,556 | +24 | 40,596 | 40,596 |
| `wio-e5_repeater` | 196,848 | 196,856 | +8 | 17,084 | 17,084 |
| `wio-e5_companion_radio_usb` | 199,968 | 199,976 | +8 | 32,940 | 32,940 |

### Hardware

None (ohne Brett). The host tests cover both parsers directly and the two paths a
board takes into `tryParsePacket()`; the bridge paths into `readFrom()` are covered
only through the parser itself.
