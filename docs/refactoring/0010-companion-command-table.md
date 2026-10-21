# 0010: A command table for the companion protocol

Status: steps 10a (host harness and characterization), 10b (the table) and 10c
(a bounded frame reader) ready for review; 10d is a proposal.
Behaviour change: none.

## Context

The companion firmware speaks to the app (BLE, USB serial, WiFi) in frames of up
to `MAX_FRAME_SIZE` (176) bytes. The first byte selects the command.
`MyMesh::handleCmdFrame()` (`examples/companion_radio/MyMesh.cpp`, about 1,020
lines) is one chain of 62 branches of the form

```c
} else if (cmd_frame[0] == CMD_X && len >= N && <more>) {
  ... reads cmd_frame[i] at fixed offsets ...
```

and a final `else` that answers `ERR_CODE_UNSUPPORTED_CMD`. Each branch states
its own minimum length, or forgets to. Two fixes in this fork came from exactly
that: `docs/fixes/companion-channel-txt-min-len.md` (a 3 byte frame stopped the
node) and `docs/fixes/companion-cmd-frame-min-len.md` (fourteen branches acted on
the previous frame's bytes). The architecture review lists the function as F5.

Three things make the function expensive to extend:

- The minimum length of a command is not data. Nothing can list, test or check
  the minimums, and a new branch can leave its own out.
- The command codes are only reachable through the chain, so the protocol cannot
  be enumerated (for documentation, a test sweep or a client generator).
- The branches cannot be called on their own, so none of it was testable on the
  host until this step.

## Inventory

Every code the chain handles, read from the source on `integration` (which has
both length fixes) and checked against the frame sweep of step 10a. "Short" is
what a frame below the minimum gets today. All lengths include the code byte.

| Code | Command | Min | Further condition | Reply | Short |
|---:|---|---:|---|---|---|
| 1 | `APP_START` | 8 | | `SELF_INFO` | unsupported |
| 2 | `SEND_TXT_MSG` | 14 | | `SENT`, or `NOT_FOUND`, `UNSUPPORTED` for a txt type | unsupported |
| 3 | `SEND_CHANNEL_TXT_MSG` | 7 | | OK, `NOT_FOUND`, `UNSUPPORTED` for a txt type | unsupported |
| 4 | `GET_CONTACTS` | 1 | `since` from 5 bytes | `CONTACTS_START`, then one `CONTACT` per loop, then `END_OF_CONTACTS` | |
| 5 | `GET_DEVICE_TIME` | 1 | | `CURR_TIME` | |
| 6 | `SET_DEVICE_TIME` | 5 | | OK, `ILLEGAL_ARG` if earlier | unsupported |
| 7 | `SEND_SELF_ADVERT` | 1 | flood if byte 1 is 1 | OK, `TABLE_FULL` | |
| 8 | `SET_ADVERT_NAME` | 2 | | OK | unsupported |
| 9 | `ADD_UPDATE_CONTACT` | 136 | lat, lon, lastmod optional | OK, `TABLE_FULL` | unsupported |
| 10 | `SYNC_NEXT_MESSAGE` | 1 | | queued frame or `NO_MORE_MESSAGES` | |
| 11 | `SET_RADIO_PARAMS` | 11 | repeat flag optional | OK, `ILLEGAL_ARG` | unsupported |
| 12 | `SET_RADIO_TX_POWER` | 2 | | OK, `ILLEGAL_ARG` | unsupported |
| 13 | `RESET_PATH` | 33 | | OK, `NOT_FOUND` | unsupported |
| 14 | `SET_ADVERT_LATLON` | 9 | altitude optional, ignored | OK, `ILLEGAL_ARG` | unsupported |
| 15 | `REMOVE_CONTACT` | 33 | | OK, `NOT_FOUND` | unsupported |
| 16 | `SHARE_CONTACT` | 33 | | OK, `TABLE_FULL`, `NOT_FOUND` | unsupported |
| 17 | `EXPORT_CONTACT` | 1 | below 33 exports self | `EXPORT_CONTACT`, `NOT_FOUND`, `TABLE_FULL` | |
| 18 | `IMPORT_CONTACT` | 99 | | OK, `ILLEGAL_ARG` | unsupported |
| 19 | `REBOOT` | 7 | bytes 1 to 6 are `reboot` | none, the board restarts | unsupported |
| 20 | `GET_BATT_AND_STORAGE` | 1 | | `BATT_AND_STORAGE` | |
| 21 | `SET_TUNING_PARAMS` | 9 | | OK | unsupported |
| 22 | `DEVICE_QUERY` | 2 | | `DEVICE_INFO` | unsupported |
| 23 | `EXPORT_PRIVATE_KEY` | 1 | build option | `PRIVATE_KEY` or `DISABLED` | |
| 24 | `IMPORT_PRIVATE_KEY` | 65 | build option | OK, `ILLEGAL_ARG`, `FILE_IO_ERROR`, `DISABLED` | unsupported |
| 25 | `SEND_RAW_DATA` | 6 | path plus 4 payload bytes, else `ILLEGAL_ARG`; flood path unsupported | OK, `TABLE_FULL` | unsupported |
| 26 | `SEND_LOGIN` | 33 | | `SENT`, `NOT_FOUND`, `TABLE_FULL` | unsupported |
| 27 | `SEND_STATUS_REQ` | 33 | | `SENT`, `NOT_FOUND`, `TABLE_FULL` | unsupported |
| 28 | `HAS_CONNECTION` | 33 | | OK, `NOT_FOUND` | unsupported |
| 29 | `LOGOUT` | 33 | | OK | unsupported |
| 30 | `GET_CONTACT_BY_KEY` | 33 | | `CONTACT`, `NOT_FOUND` | unsupported |
| 31 | `GET_CHANNEL` | 2 | | `CHANNEL_INFO`, `NOT_FOUND` | unsupported |
| 32 | `SET_CHANNEL` | 50 | 66 and more: unsupported (256 bit secret) | OK, `NOT_FOUND` | unsupported |
| 33 | `SIGN_START` | 1 | | `SIGN_START` | |
| 34 | `SIGN_DATA` | 2 | | OK, `BAD_STATE`, `TABLE_FULL` | unsupported |
| 35 | `SIGN_FINISH` | 1 | | `SIGNATURE`, `BAD_STATE` | |
| 36 | `SEND_TRACE_PATH` | 11 | path length below `MAX_PACKET_PAYLOAD - 5`; a multiple of the hash size | `SENT`, `ILLEGAL_ARG`, `TABLE_FULL` | unsupported |
| 37 | `SET_DEVICE_PIN` | 5 | | OK, `ILLEGAL_ARG` | unsupported |
| 38 | `SET_OTHER_PARAMS` | 2 | bytes 2 to 4 optional | OK | unsupported |
| 39 | `SEND_TELEMETRY_REQ` | 4 | exactly 4: self; 36 and more: a peer; 5 to 35: unsupported | `TELEMETRY_RESPONSE` push, or `SENT`, `NOT_FOUND` | unsupported |
| 40 | `GET_CUSTOM_VARS` | 1 | | `CUSTOM_VARS` | |
| 41 | `SET_CUSTOM_VAR` | 4 | | OK, `ILLEGAL_ARG` | unsupported |
| 42 | `GET_ADVERT_PATH` | 34 | | `ADVERT_PATH`, `NOT_FOUND` | unsupported |
| 43 | `GET_TUNING_PARAMS` | 1 | | `TUNING_PARAMS` | |
| 50 | `SEND_BINARY_REQ` | 34 | | `SENT`, `NOT_FOUND`, `TABLE_FULL` | unsupported |
| 51 | `FACTORY_RESET` | 6 | bytes 1 to 5 are `reset` | OK, then restart; `FILE_IO_ERROR` | unsupported |
| 52 | `SEND_PATH_DISCOVERY_REQ` | 34 | byte 1 is 0 | `SENT`, `NOT_FOUND`, `TABLE_FULL` | unsupported |
| 54 | `SET_FLOOD_SCOPE_KEY` | 2 | byte 1: 0 sets (key from 18 bytes, else clears), 1 unscoped, others unsupported | OK | unsupported |
| 55 | `SEND_CONTROL_DATA` | 2 | high bit of byte 1 set | OK, `TABLE_FULL` | unsupported |
| 56 | `GET_STATS` | 2 | sub type 0, 1, 2; others `ILLEGAL_ARG` | `STATS` | unsupported |
| 57 | `SEND_ANON_REQ` | 34 | | `SENT`, `TABLE_FULL` | unsupported |
| 58 | `SET_AUTOADD_CONFIG` | 2 | max hops optional | OK | unsupported |
| 59 | `GET_AUTOADD_CONFIG` | 1 | | `AUTOADD_CONFIG` | |
| 60 | `GET_ALLOWED_REPEAT_FREQ` | 1 | | `ALLOWED_REPEAT_FREQ` | |
| 61 | `SET_PATH_HASH_MODE` | 3 | byte 1 is 0 | OK, `ILLEGAL_ARG` | unsupported |
| 62 | `SEND_CHANNEL_DATA` | 1 | checks its own length: below 4, or no room for `data_type` behind the path: `ILLEGAL_ARG` | OK, `NOT_FOUND`, `ILLEGAL_ARG`, `TABLE_FULL` | (`ILLEGAL_ARG`) |
| 63 | `SET_DEFAULT_FLOOD_SCOPE` | 1 | below 48 clears the scope | OK, `ILLEGAL_ARG` | |
| 64 | `GET_DEFAULT_FLOOD_SCOPE` | 1 | | `DEFAULT_FLOOD_SCOPE` | |
| 65 | `SEND_RAW_PACKET` | 4 | | OK, `ILLEGAL_ARG`, `TABLE_FULL` | unsupported |
| 66 | `RUN_CLI_COMMAND` | 3 | | `CLI_REPLY` | unsupported |

Codes 0, 44 to 49, 53 and 67 to 255 are not commands and are answered
`ERR_CODE_UNSUPPORTED_CMD` at every length.

Observations that the inventory makes visible, recorded and not changed:

- "Too short" and "not a command" are the same answer for 54 codes. Only
  `SEND_CHANNEL_DATA` answers `ILLEGAL_ARG`. The minimums are therefore not
  observable as such by a client, which is why the table below may not change
  the answer.
- Three codes split into sub commands by length or by byte 1 (39, 32, 54), and
  four more refuse a frame by a byte value after the length test (19, 51, 52,
  55, 61). A refusal there is also `UNSUPPORTED`, so the table needs a way for a
  handler to say "not mine" and fall through to the same answer.
- A key of 32 zero bytes is not "no contact". The contact table starts with
  eight slots reserved for anonymous requests, zeroed by `resetContacts()`, and
  `lookupContactByPubKey()` searches them too. `REMOVE_CONTACT` with a zero key
  therefore answers OK and removes a reserved slot, eight times in a row, and
  `GET_CONTACTS` then reports `num_contacts - 8`: on an empty node the count
  wraps to `0xFFFFFFFF`, on the board of the hardware check it dropped from 350
  to 342. The slots return on the next boot, and the stored contacts are not
  affected (`saveContacts()` skips type `ADV_TYPE_NONE`); both were checked on
  the board. Pinned in `AZeroKeyNamesAReservedSlot`, not changed here.
- Senders in this workspace were checked against the minimums: the KiekR command
  builders (`core-lib/src/companion/cmd.rs`), `docs/companion_protocol.md` and
  `tools/refactoring/nodetool.py`. None sends below a minimum, none depends on a
  fall through. Official apps and meshcore-cli are not in the workspace and were
  not checked; for them the length fixes, not this step, are the change that
  matters, and this step keeps their minimums byte for byte.

## Decision

### Step 10b: one row per command

`MyMesh` gets a table, a private static member so its rows can name private
methods:

```c++
struct CmdHandler {
  uint8_t code;
  uint8_t min_len;                        // whole frame, code byte included
  bool (MyMesh::*handle)(size_t len);     // false: "not this command", falls through
};
static const CmdHandler CMD_TABLE[];
```

`handleCmdFrame()` becomes the dispatcher:

```c++
void MyMesh::handleCmdFrame(size_t len) {
  for (const CmdHandler& c : CMD_TABLE) {
    if (c.code != cmd_frame[0]) continue;
    if (len >= c.min_len && (this->*c.handle)(len)) return;
    break;
  }
  writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
  MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
}
```

Each branch body moves, unchanged apart from the outer test, into a method
`cmdAppStart(size_t len)` and so on, which returns `true`. Branches with a further
condition test it first and return `false`, which reproduces the fall through:

```c++
bool MyMesh::cmdSetPathHashMode(size_t) {
  if (cmd_frame[1] != 0) return false;
  if (cmd_frame[2] >= 3) {
    ...
  return true;
}
```

The codes with two branches (39, 32, 54) get one row and one method that
contains both, in the old order. The minimum in the row is the smaller of the
two, and the method returns `false` where neither branch matched.

How the minimum is enforced: in one place, before any handler runs, with the
answer the chain gives today. A command without a minimum has 1 in its row
(`checkRecvFrame()` never returns 0). `SEND_CHANNEL_DATA` keeps 1 and its own
`ILLEGAL_ARG` checks, because that is its answer today.

A new command is then one method and one row, and a forgotten minimum is a
visible `1` in a table rather than an absent clause.

### Cost on the targets

Measured in 10b, see "Verification of step 10b" for the table. RAM is unchanged
in every environment (the table is `const`, 59 rows). Flash grows by about
1.6 to 1.7 kB on nRF52, 1.5 kB on STM32, 1.2 to 1.9 kB on RP2040, 2.6 kB on
ESP32-C3 and 3.5 kB on ESP32-S3, and shrinks by 1.3 kB on ESP32-C6. The design
set 2 kB on nRF52 as the bound beyond which the cheaper `switch` variant would be
used instead; the measured cost is inside it. The extra flash is the 59 function
prologues and epilogues the chain did not need, plus the table (on ARM a pointer
to member function is 8 bytes, so a row is 12).

### Steps

| Step | Content | Behaviour |
|---|---|---|
| 10a | Host harness for `MyMesh.cpp`, frame sweep, per command tests | none |
| 10b | `CMD_TABLE` and one method per command, bodies moved unchanged | none, the sweep must stay byte identical |
| 10c | A bounded frame reader (`FrameReader r(cmd_frame, len); r.u8(); r.u32(); r.bytes(n)`) in the handlers, replacing the hand counted offsets | none; a reader that runs out reports it instead of reading the tail |
| 10d | Optional, a protocol change: answer a too short frame with `ILLEGAL_ARG` | visible to apps, needs its own proposal |

10b and 10c are kept apart because 10b is a pure move whose diff can be read with
`git diff --color-moved`, while 10c rewrites the parsing in every handler and
needs a reviewer to read each one.

## Alternatives considered

- **A `switch` on the code, plus a separate array of minimums.** Cheaper in
  flash (a jump table, no member pointers), but the code list then exists twice,
  in the `switch` and in the array, and they can disagree. Kept as the fallback
  if the measured cost of the table is too high.
- **Only a minimum length table in front of the existing chain**, leaving the 62
  branches in place. The smallest diff, and the guard would be central. Rejected
  as the end state: the codes are still not enumerable, the bodies still cannot be
  tested or called on their own, and every branch keeps a length clause that now
  has to agree with the table.
- **Static functions taking `MyMesh&`.** Four byte pointers instead of eight, but
  every body would change from `_prefs` to `self._prefs`, which turns a move into
  a rewrite. Member functions keep the bodies textually identical.
- **An X macro list of commands.** Generates the table and the declarations from
  one list, but is harder to read and to step through in a debugger than a plain
  array, for the saving of one declaration per command.
- **Answer `ILLEGAL_ARG` from the central check.** Rejected for this series: it
  changes the answer of 54 commands, which the length fixes already rejected for
  the same reason. Listed as 10d.
- **The frame reader first (10c before 10b).** It fixes the class of bug more
  directly, but it rewrites every branch inside a 1,000 line function that cannot
  be reviewed piece by piece. Moving first makes each handler a unit the reader
  step can change and test one at a time.

## Upstream overlap

`upstream/dev` was 428a95c6 on 20 October 2026, the same as this fork's base. No
upstream pull request proposes a table, a dispatcher or central length
validation for `handleCmdFrame()`. 19 open pull requests change lines inside it,
mostly 2 to 32 lines each; 10b moves every one of those lines, so each of them
would need to be re-applied to the method its branch moved to. The largest are
meshcore-dev/MeshCore#3523 (single key DM), #2964 (chunked contacts), #2682
(ThinkNode M4) and #3638 (dual radio profiles). Some need the table's shape
specifically: #2843 and #2670 add optional trailing bytes (a minimum, not an
exact length, which the table has), #2717 and #2682 add commands (both claim code
66, which upstream itself has to resolve; this fork already has 66 as
`RUN_CLI_COMMAND`), and #3454 adds a `GET_STATS` sub type (a sub dispatch inside
one handler, which the table allows). #3643 is the channel text length guard of
this fork, already in `integration`.

Step 10a touches none of those lines: it adds tests and a native environment.

## Step 10c: a bounded frame reader

### What the handlers read today

Measured on `integration` at `68e0fcdc` in two ways.

`tools/refactoring/cmd_frame_offsets.py` lists every access to `cmd_frame` in the
59 handlers of `CMD_TABLE`. 48 handlers touch the frame, 11 read nothing past the
code byte. Between them they use 68 constant offsets (`cmd_frame[1]`,
`&cmd_frame[1 + PUB_KEY_SIZE]`), 30 accesses through a running index `i`, 7
lengths derived from `len` that go to a callee together with a pointer into the
frame, and 6 writes into the frame: the terminator at `len` (`cmd_frame[len] = 0`
or `text[tlen] = 0`) in `APP_START`,
`RUN_CLI_COMMAND`, `SEND_TXT_MSG`, `SEND_LOGIN` and `SET_CUSTOM_VAR` (the in place
terminator; `cmd_frame` is `MAX_FRAME_SIZE + 1` bytes, so index `len` is always
inside it) and the `':'` that `SET_CUSTOM_VAR` replaces. Every constant offset that
is read without a further length test lies below the row's minimum; the reads
above it (`alt` of `SET_ADVERT_LATLON`, the tail of `SET_OTHER_PARAMS`, the repeat
flag, `since`, the 256 bit scope key) each sit behind their own `len` test.

The static list cannot see what a callee does with a pointer, and a running index
can be driven by a byte of the frame. So the second measurement is dynamic: the
host harness built with AddressSanitizer (`tools/refactoring/companion_asan_probe.patch`),
the bytes of `cmd_frame` above the frame poisoned before each frame is handled,
and every code 0 to 69 sent at every length 1 to 176 with ten fills (`00 01 02 3f
40 7f 81 bf ff 5a`, which include the path length bytes of every hash size), each
to a freshly booted node. 123,200 frames. Reads above `len`:

| Handler | Where | Lengths | What it reads | Answer, either way |
|---|---|---|---|---|
| `SEND_CHANNEL_DATA` | `Packet::writePath()` | 4 to 64 | the path the length byte announces, before the test whether path and `data_type` fit | `ILLEGAL_ARG` |
| `SEND_RAW_DATA` | `Packet::writePath()` | 6 to 63 | the same, before the test for 4 payload bytes | `ILLEGAL_ARG` |
| `SEND_RAW_PACKET` | `Dispatcher::tryParsePacket()` | 4 to 6 | the 4 transport code bytes and the path length byte, before its length test | `ILLEGAL_ARG` |
| `RUN_CLI_COMMAND` | `CommonRadioPrefs::handleCommand()`, `MyMesh::handleCommand()` | 3 to 22 | `memcmp()` of a command prefix longer than the text, past the terminator | the CLI reply |
| `SET_DEFAULT_FLOOD_SCOPE` | `strlen()` in the handler | 48 to 175 | the byte at `len`, and on a board onwards while no terminator comes | `ILLEGAL_ARG` |

The last row only shows up with the byte at `len` poisoned too (`PROBE_SKIP=0`): in
the probe that byte is always 0 (every node is fresh), on a board it is left over
from the previous frame, and `strlen()` runs on to the next zero. Reads of the byte
at `len` by the five handlers that just wrote the terminator there are not listed;
with the poison starting at `len + 1` (`PROBE_SKIP=1`) the first four rows remain
and nothing else.

None of the five changes an answer. For the first three, a path or header that
runs past the frame always fails the length test that follows, because the cursor
is then already beyond `len`; the stale bytes go into a local buffer that is
discarded. For `SET_DEFAULT_FLOOD_SCOPE`, `strlen()` only runs past the frame when
bytes 1 to `len - 1` (at least 47) hold no zero, and then it returns at least 47,
which is refused as at least 31. For the CLI, `memcmp()` stops being decisive at
the terminator, which differs from the prefix. The first two and the fifth are in
handlers and go away with the reader without changing an answer; the CLI and
`tryParsePacket()` are callees that 10c does not touch (the latter also parses
every received radio packet), recorded here as findings for their own fixes.

A side observation of the ASan build, not checked further: built with ASan, the
frame sweep's packet digests differ for codes 2, 3, 26, 27 and 39 (encrypted
requests and messages) while every reply stays the same. Unproven guess: the
encryption pads its last block from an uninitialised stack buffer, which the
sweep's stack scrub reaches in a normal build and does not reach under ASan's
stack layout. Not part of 10c.

### The reader

A header only class in `src/helpers/FrameReader.h`:

```c++
class FrameReader {
public:
  FrameReader(uint8_t* buf, size_t len, size_t pos = 0);
  uint8_t  u8();             // 0 when it runs out
  uint16_t u16();            // memcpy, as the handlers do today
  uint32_t u32();
  int32_t  i32();
  uint8_t* bytes(size_t n);  // n bytes at the cursor, nullptr when they are not all there
  void     skip(size_t n);
  bool     has(size_t n) const;    // n more bytes are in the frame
  size_t   remaining() const;
  size_t   pos() const;
  bool     ok() const;       // false once a read ran out; stays false
  char*    restAsCString();  // the rest as a C string: writes 0 at buf[len], as today
};
```

Each handler starts with `FrameReader r(cmd_frame, len, 1);` and reads its fields in
order instead of counting offsets:

```c++
bool MyMesh::cmdSetTuningParams(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint32_t rx = r.u32();
  uint32_t af = r.u32();
  ...
```

Optional trailing fields keep their own test, written as `r.has(n)` (or
`r.remaining()`, where the old test compared `len`). Reads of several bytes use
`memcpy` into the value, exactly as the handlers do today, so the byte order is
the same on every target by construction rather than by a little endian
assumption.

`restAsCString()` writes the terminator at `buf[len]`, the same byte the five
handlers write today; its precondition (the buffer has room for `len + 1` bytes)
is what `cmd_frame` already guarantees. `SET_CUSTOM_VAR` keeps editing its text in
place after that.

### Why the answers stay byte for byte the same

- **The minimum stays in the table.** A frame below a row's minimum never reaches
  the handler and gets the dispatcher's `UNSUPPORTED` as before. The reader does
  not add a second check that could answer differently.
- **Inside the minimum, nothing runs out.** Every unconditional read is below the
  row's minimum (measured above), so for every frame a handler sees, the reader
  returns the same bytes the offsets did. A read that would run out yields 0 and
  clears `ok()`; in 10c that can only happen in the places listed next, each of
  which already tests and refuses.
- **The handlers that check their own length keep the exact test.** `len < 4` in
  `SEND_CHANNEL_DATA`, the 4 payload bytes of `SEND_RAW_DATA`, `len == 4` and
  `len >= 36` of `SEND_TELEMETRY_REQ`, the two lengths of `SET_CHANNEL`, `len < 33`
  of `EXPORT_CONTACT`, `len >= 48` of `SET_DEFAULT_FLOOD_SCOPE`, `len >= 18` of
  `SET_FLOOD_SCOPE_KEY` and the upper bound of `SEND_TRACE_PATH` stay the same
  comparisons, on `len` or on `r.remaining()` with the same value. Where a test
  follows a path copy today (`SEND_CHANNEL_DATA`, `SEND_RAW_DATA`), the copy becomes
  `r.bytes(n)`, and "did not fit" (`!r.ok()`) joins the existing test with `||`;
  both lead to the `ILLEGAL_ARG` the over-read case answers today. The number of
  bytes a path length byte announces comes from a small helper next to the table,
  `pathByteLen()`, the same product `Packet::writePath()` copies.
- **Byte order stays as written.** Where a handler composed a value by shifting
  (the `data_type` of `SEND_CHANNEL_DATA`, low byte first), it still does, from two
  `u8()` reads; everything that used `memcpy` now uses `u16()`/`u32()`/`i32()`,
  which use `memcpy`.
- **The unbounded `strlen()`** of `SET_DEFAULT_FLOOD_SCOPE` becomes a length
  bounded to the frame (`strnlen(p, r.remaining())`); argued above, the answer is
  the same for every frame.
- **Callees are not changed.** `updateContactFromFrame()`, `importContact()`,
  `tryParsePacket()`, `createControlData()`, `sendAnonReq()` and the CLI parser get
  the same pointer and length as today (`r.bytes()`/`r.remaining()` instead of
  `&cmd_frame[n]`/`len - n`).

On the host the reader counts every read that runs out (`-D
FRAME_READER_COUNT_OVERRUNS`, set only in `native_companion`; on the targets the
counter does not exist), so a test can assert that no frame makes a handler run
out, apart from the two path cases where running out is the refusal.

### Tests and evidence (plan; results under "Verification of step 10c")

- The sweep and the 49 command tests of 10a, unchanged, before and after.
- New tests first, on the unchanged handlers: for every row, the frame one byte
  below the minimum (`UNSUPPORTED`) and exactly at it (accepted); every optional
  field one byte short and complete (`SET_ADVERT_LATLON` 12/13, `SET_RADIO_PARAMS`
  11/12, `SET_OTHER_PARAMS` 2 to 5, `SET_AUTOADD_CONFIG` 2/3, `GET_CONTACTS` 4/5,
  `SET_FLOOD_SCOPE_KEY` 17/18, `SET_CHANNEL` 49/50/65/66, `SEND_TELEMETRY_REQ`
  4/5/35/36, `EXPORT_CONTACT` 32/33, `SET_DEFAULT_FLOOD_SCOPE` 47/48,
  `SEND_TRACE_PATH` at its upper bound); and each measured over-read with a
  stale tail that would change the outcome if it were used (a path length byte
  announcing more than the frame holds, behind a previous frame whose bytes form a
  valid path and payload; a 47 byte scope name without terminator behind a tail
  with a zero after 3 more bytes).
- Unit tests of the reader itself (each read at the end of the frame, past it,
  `ok()` staying false, `restAsCString()` writing exactly `buf[len]`).
- Mutations of the converted handlers (a field read with the wrong width, an
  optional field read without `has()`, `has(n)` off by one, a lost `!r.ok()`).
- Builds of every companion environment (BLE, USB, WiFi) per platform plus control
  environments, flash and RAM against `68e0fcdc`; all 17 host environments;
  `companion_session.py` on a board if one can be claimed, otherwise recorded as
  not run.

The cost to expect: the bounds test in every read. Target: at most 1 kB of flash on
nRF52 for the whole step; if it is more, the reads inside the minimum get an
unchecked variant (the table has already checked them) and the checked reads stay
for the optional fields.

### One pull request or several

One. The reader is useless without users, and the handlers are independent of one
another, so the step is cut into commits instead: the new boundary and stale tail
tests on the unchanged handlers; the reader with its unit tests; the 34 handlers
that read their fields one after the other without a length test of their own;
the 14 with optional fields, sub commands, a path or their own length tests,
where a reviewer has to compare each test. Each commit passes all suites on its
own. Splitting into two pull requests along the last line would double the build
matrix and the review overhead without making either half easier to read.

### Alternatives considered for 10c

- **The dispatcher passes the reader** (`bool (MyMesh::*)(FrameReader&)`). Makes it
  harder to bypass the reader, but changes 59 declarations, the table type and
  every handler that only wants `len`, while a handler could still index
  `cmd_frame`. Not worth a diff that touches the 11 handlers that read nothing.
- **Reading into packed structs** (`memcpy(&frame, cmd_frame, sizeof frame)`).
  The fewest lines, but the layout then depends on the compiler's packing rules on
  five platform packages, and variable length fields (paths, text) do not fit a
  struct.
- **A macro per handler (`NEED(n)`)** that returns `ILLEGAL_ARG` when the frame is
  short. That is step 10d, a protocol change; in 10c the table's minimum is the
  only length answer.
- **Upstream's `ByteReader`.** meshcore-dev/MeshCore#2864 (OTA over LoRa, open,
  12,000+ lines) brings `mesh::ota::ByteReader` in `src/helpers/ota/OtaByteIO.h`:
  sticky `ok`, `u8()`, `u32()`, `take(n)`, `skip(n)`, little endian by shifting.
  Close to this design, which takes the same method names where they mean the same
  (`u8`, `u32`, `skip`, `ok`). Not reused: it is not merged, lives in the OTA
  namespace, has no in place terminator and decodes by shifting (the same bytes on
  the five platforms, all little endian, but a different expression than the
  handlers' `memcpy`). If both land, merging them is one rename.
- **Converting `updateContactFromFrame()` and `importContact()` too.** They parse
  frames as well, but they are shared with the contact store and the advert path;
  out of the scope of the handlers, a candidate for later.

### Upstream overlap for 10c

`upstream/dev` is `dec8ad6e` on 21 October 2026; since `428a95c6` it only adds WiFi
to two variants and changes nothing in `examples/companion_radio`. Of 499 open
pull requests, 74 change `examples/companion_radio/MyMesh.cpp`; 10 of them change
lines that touch `cmd_frame`, and so do the handlers 10c rewrites: #2670 and #2843
(each a new optional byte 5 in `SET_OTHER_PARAMS`), #2682 (`SEND_RAW_DATA`,
`RUN_CLI_COMMAND`), #2717 and #1374 (new commands), #1923 (`SEND_TRACE_PATH`),
#1551 (the timestamp of `SEND_TXT_MSG` and `SEND_CHANNEL_TXT_MSG`), #3523
(`ADD_UPDATE_CONTACT`), #3643 (this fork's channel text fix, already in
`integration`). Each would be re-applied as reader calls; the two optional byte 5
proposals become one `if (r.has(1))`. None proposes a reader for the companion
frames; #2864 is the only reader-like change and is discussed above.

## Verification of step 10a

### What the harness is

`[env:native_companion]` builds the real `MyMesh.cpp` and `DataStore.cpp` with the
core, the chat layer and the helpers they use, against the mocks in `test/mocks`
(in-memory file system, radio, board, clock, sensor manager). Two mocks gained
what the companion needs: `FSInfo` and a directory listing in `FS.h`, and the
declaration of `radio_new_identity()` in `target.h`. The build options are the
ones companion environments set: `MAX_CONTACTS` and `MAX_GROUP_CHANNELS` for the
whole build, and private key export and import compiled in.

`MAX_CONTACTS` for the whole build is not cosmetic. With it only in `MyMesh.h`,
as a first version of the harness had it, `BaseChatMesh.cpp` was compiled with 32
contacts and `MyMesh.cpp` with 100, the two disagreed about the layout of
`BaseChatMesh`, and `GET_CONTACTS` reported zero contacts after two were added.
Every firmware environment sets it in its build flags.

A test drives the node only through the protocol: a fake serial interface hands
`MyMesh::loop()` one frame, and the test gets every frame written back. State
that the protocol does not report is observed on the collaborators: what was
written to flash, which packets were queued for the air, whether the board
restarted, whether the radio or the clock were set. Each node is built in zeroed
storage, which is how a global starts on a board, so the bytes above a frame's
length are the same on every run.

### The tests

- `test_frame_sweep.cpp`: for each code 0 to 255, each length 1 to 176 and five
  fills (four byte values, and zeros after a full length frame of `0x5A`, so a
  read above `len` would show), a freshly booted node gets one frame. The replies,
  byte for byte, and the side effects form one line; consecutive lengths with the
  same line fold into a range, and one more line per code and fill carries a
  digest of every packet queued and every file stored. 225,280 frames, compared
  with `frame_sweep.golden` (about 2,000 lines). The golden file was checked for
  hidden state by generating it once with the codes in ascending and once in
  descending order; the two are identical. A first run was not: the global mock
  clock kept the last unique timestamp of the previous node, so a request's tag
  depended on the order. The harness now resets the clock completely.
- `test_commands.cpp`: 49 tests that send the frames an app sends and check the
  meaning: the fields of each reply, the state a setter changed (read back through
  the protocol where it can be), the boundary at each minimum with a real frame,
  and the sub commands.

The sweep also scrubs the stack with a fixed byte before each frame. Without
that, a run of the sweep after the other tests disagreed with a run on its own,
and the difference was real: `CMD_SET_CHANNEL` builds its record in a
`ChannelDetails` on the stack, `StrHelper::strncpy()` copies the name only up to
its terminator, and `DataStore::saveChannels()` writes all 32 name bytes, so up to
31 bytes of whatever the stack held go to `/channels2`, on a board as well.
Nothing reads them back (the name is used as a C string, and `GET_CHANNEL` copies
it with `strcpy`), so no reply and no packet carries them; it is flash content
only. Recorded here and not changed. The sweep zeroes those bytes before taking
its digest and nothing else.

The golden file says nothing about whether today's behaviour is right; it records
it. Places where it looks odd are named in the tests and above (for example, a
contact added through the protocol cannot be shared, because sharing resends a
stored advert and there is none).

### Mutation tests

Each fault was put into `MyMesh.cpp` on its own, the suite run, the file
restored:

| Mutation | Caught by |
|---|---|
| `SET_TUNING_PARAMS` minimum 9 becomes 8 | sweep, `TuningParamsRoundTrip` |
| `ADD_UPDATE_CONTACT` minimum 136 becomes 135 | sweep, `AddContactNeedsEveryMandatoryField` |
| `SEND_CHANNEL_DATA` `len < 4` becomes `len < 3` | not caught, equivalent (below) |
| `SET_PATH_HASH_MODE` loses its `cmd_frame[1] == 0` | sweep, `PathHashMode` |
| self telemetry `len == 4` becomes `len >= 4` | sweep, `TelemetryHasTwoForms` |
| the final `else` answers `ILLEGAL_ARG` | sweep and 38 command tests |
| the 66 byte `SET_CHANNEL` branch moves behind the 50 byte one | sweep, `Channels` |
| `SEND_LOGIN` no longer terminates the password at `len` | sweep only (the packet digest) |
| unscoped `SET_FLOOD_SCOPE_KEY` needs 3 bytes | sweep, `FloodScopeKey` |
| `SET_TUNING_PARAMS` stores `rx` into the airtime factor | `TuningParamsRoundTrip` only |

The third is an equivalent mutant: with `len == 3` the branch goes on to its
second check, whether the two `data_type` bytes fit behind the path, which fails
for every path length and answers the same `ILLEGAL_ARG`. The `len < 4` test is
covered by that second one and cannot be observed on its own. The last two rows
are why both suites exist: the sweep sends zeros, under which `rx` and `af` are
the same, and the command tests do not look at packet contents.

### Host suites

All host environments, on the committed tree, including the new one:

```
native_companion       test_companion_frames      PASSED
... (the other 21 suites in 13 environments)     PASSED
================ 414 test cases: 414 succeeded ================
```

`native_companion` is added to `.github/workflows/run-unit-tests.yml`, which
names its environments explicitly.

### Builds

Step 10a changes no firmware source: its diff against its base, limited to
`src`, `examples` and `variants`, is empty. The changes to shared test files (`test/mocks/FS.h`,
`test/mocks/target.h`) only add members and a declaration, and every existing
host suite passes unchanged. The fork CI builds its 15 firmware environments on
the pull request.

### Hardware

The point of the hardware check for 10a is to show that the host harness models
the firmware, so that 10b can rely on it. LilyGo T-Echo `techo-2`, a companion
with 350 contacts and 40 channels, normally on the official 1.18.0 BLE image. The
USB companion image was built from the base of this change (which has the same
firmware source) with the marker `c10a-int`, and `ver` answered
`v1.18.0 (Build: c10a-int)` before the runs.

`tools/refactoring/companion_sweep.py` sends the frames of the golden file's all
zero pass that cannot change the board: every frame the host refuses, every
length of the commands that only report, and four lengths of every code that is
not a command. Error and OK replies are compared byte for byte; for report
commands, whose content is the board's own, the reply code is compared.

```
6502 frames in 729 s: 6502 as on the host, 0 different, 0 without reply
(compared byte for byte: 4083, by reply code: 2419)
codes covered: 251
```

The first run was not clean, and that is how the zero key finding above came to
light: the tool still counted `REMOVE_CONTACT` with a zero key as harmless. On
the host every frame meets a freshly booted node, so each one removes the first
reserved slot and answers OK. On the board the frames meet the same node one
after another: lengths 33 to 40 answered OK and removed the eight slots, every
later frame answered `NOT_FOUND`, and `SHARE_CONTACT` and `GET_CONTACT_BY_KEY`
with a zero key then found nothing either. Those were 424 of 6,790 frames, all in
codes 15, 16 and 30, and `GET_CONTACTS` reported 342 contacts. After a reboot it
reported 350 again and every value below was as before. The tool now leaves
`REMOVE_CONTACT` and `RESET_PATH` out, and the second run, after the reboot, is
the one above.

State before the runs, after the reboot and after the second run, read through
the protocol: self info (key, name, radio, TX power, location, telemetry modes),
device info (including the BLE PIN and path hash mode), tuning parameters,
auto add configuration, default flood scope and key, all 40 channels (digest),
contact count 350. All three readings identical.

Afterwards the official image was flashed back
(`LilyGo_T-Echo_companion_radio_ble-v1.18.0-81dbae5.zip`, SHA-256 prefix
`6d602a54`, the release digest), and the phone that uses this board as its
companion reconnected over BLE 17 seconds later.

Not covered on hardware: frames the host accepts for commands that store,
transmit, restart or format (setters, sends, `REBOOT`, `FACTORY_RESET`, private
key import, contact and channel changes). For those the host tests are the
evidence; the length fixes recorded hardware runs for some of them. Only
nRF52 was run; ESP32 and RP2040 companions compile the same `MyMesh.cpp`, and
their build is checked by CI, not their behaviour.

## Verification of step 10b

### What changed

`handleCmdFrame()` is the dispatcher above, followed by 59 handlers in the order
of the old chain. The bodies were moved by a script, not retyped: of the 863
lines the diff removes from `MyMesh.cpp`, 796 reappear with the same text, and
the 67 without a counterpart are exactly the 62 branch heads, the final `else`
(now the dispatcher's fallback) and the three early `return;` of
`SEND_CHANNEL_DATA`, now `return true;`. `MyMesh.h` gains the row type, the
table and the 59 declarations, all private.

### Host

The suites of 10a pass unchanged, test files and golden file untouched: the frame
sweep (every code, every length, five fills, replies byte for byte and a digest of
every queued packet and stored file) and the 49 command tests. All host suites on
the committed tree: 414 of 414.

The 10a mutations were aimed at the chain. These are aimed at the table, each on
its own:

| Mutation | Caught by |
|---|---|
| `SET_TUNING_PARAMS` row minimum 9 becomes 8 | sweep, `TuningParamsRoundTrip` |
| `SEND_TELEMETRY_REQ` row minimum 4 becomes 5 | sweep, `TelemetryHasTwoForms` |
| `ADD_UPDATE_CONTACT` row minimum loses the 4 timestamp bytes | sweep, `AddContactNeedsEveryMandatoryField` |
| the row of `GET_TUNING_PARAMS` is missing | sweep, `TuningParamsRoundTrip` |
| the dispatcher ignores a handler that declines | sweep and 6 command tests |
| the dispatcher skips the minimum | 32 tests fail |
| `cmdSetPathHashMode` no longer declines byte 1 other than 0 | sweep, `PathHashMode` |
| `cmdSendControlData` declines the wrong flag value | sweep, `ControlData` |

### Builds

15 environments built from 10a (`2f5cf53c`) and from 10b with the same
toolchain, each in its own worktree. All build on both sides.

| Environment | Flash 10a | Flash 10b | Delta | RAM delta |
|---|---:|---:|---:|---:|
| `Heltec_v3_companion_radio_ble` | 1,666,365 | 1,669,897 | +3,532 | +0 |
| `Heltec_v3_companion_radio_wifi` | 1,102,533 | 1,105,985 | +3,452 | +0 |
| `Heltec_v3_companion_radio_usb` | 652,925 | 656,449 | +3,524 | +0 |
| `RAK_4631_companion_radio_ble` | 462,228 | 463,956 | +1,728 | +0 |
| `RAK_4631_companion_radio_usb` | 453,188 | 454,916 | +1,728 | +0 |
| `ProMicro_companion_radio_usb` | 356,980 | 358,708 | +1,728 | +0 |
| `LilyGo_T-Echo_companion_radio_ble` | 373,776 | 375,360 | +1,584 | +0 |
| `LilyGo_T-Echo_companion_radio_usb` | 366,384 | 367,968 | +1,584 | +0 |
| `PicoW_companion_radio_ble` | 632,364 | 633,580 | +1,216 | +0 |
| `PicoW_companion_radio_wifi` | 526,780 | 528,708 | +1,928 | +0 |
| `LilyGo_Tlora_C6_companion_radio_ble_` | 1,446,171 | 1,444,861 | -1,310 | +0 |
| `wio-e5-mini_companion_radio_usb` | 207,472 | 208,944 | +1,472 | +0 |
| `Xiao_C3_companion_radio_ble` | 1,252,846 | 1,255,424 | +2,578 | +0 |
| `Heltec_v3_repeater` | 1,174,625 | 1,174,625 | +0 | +0 |
| `RAK_4631_repeater` | 441,656 | 441,656 | +0 | +0 |

The thirteen companions cover the five platform packages that compile
`MyMesh.cpp` (nRF52, Espressif 32 for S3 and C3, pioarduino for C6, RP2040,
STM32) and the BLE, USB and WiFi interfaces. The two repeaters do not compile the
file and are there as the control. The tightest flash budget among them is the
STM32WLE5 of `wio-e5-mini` (256 kB), which goes from 79.1 % to 79.7 %.

### Hardware

The same T-Echo, `techo-2`, with two USB companion images built from 10a and from
10b, marked `c10a-int` and `c10b-tab` and confirmed by `ver` before each run.

- `tools/refactoring/companion_session.py` sends 39 frames the commands accept:
  every setter with the value the board already holds (TX power, radio, tuning,
  other parameters, auto add, path hash mode, name, location, PIN, default scope),
  the declining sub commands, out of range values, stats, self telemetry, a whole
  signing round, channels, two CLI commands and the wrong reboot and reset words.
  Run once per image and compared: the only two differing lines are the build
  marker inside `DEVICE_QUERY` and the reply to `ver`.
- `companion_sweep.py` against the 10b image: 6502 of 6502 frames as on the host.
- State before and after, read as in 10a: identical apart from the build marker.
- Afterwards the official 1.18.0 BLE image went back on, and the phone that uses
  the board reconnected 4 seconds later.

Not covered on hardware: the setters with new values, sends on air, `REBOOT`,
`FACTORY_RESET`, private key import, contact and channel changes. Their handlers
are moved text, pinned on the host by the sweep and the command tests. Only nRF52
ran on hardware.

## Verification of step 10c

### What changed

48 handlers start with `FrameReader r(cmd_frame, len, 1);` and read through it;
the 11 that read nothing past the code byte are untouched. In `MyMesh.cpp` the
file changes by 217 lines added and 171 removed (34 handlers in one commit, 14 in
the next, the include and `pathByteLen()` among them). After the step,
`tools/refactoring/cmd_frame_offsets.py` finds no constant offset, no running
index and no length derived from `len` in any handler; the one remaining write
into the frame is the `':'` that `SET_CUSTOM_VAR` replaces in its own text. The
dispatcher, the table and every callee are unchanged.

`SET_AUTOADD_CONFIG` reads its hop count into a local before `min()`, because
`min()` is a macro on some cores and would read twice.

### New tests

All written and run on the unchanged handlers first (commit `29e35f06`), and run
again against the handlers of `68e0fcdc` after the step (60 of 60 pass there too).

- `test_frame_bounds.cpp`, `NoCommandReadsThePreviousFrame`: a catalog of 98
  frames as an app sends them, each command at its minimum, both sides of every
  optional field, the measured reads past the end (an announced path longer than
  the frame in two hash sizes for `SEND_RAW_DATA` and `SEND_CHANNEL_DATA`, an
  unterminated scope name, transport codes past the end in `SEND_RAW_PACKET`), and
  the text commands. Each goes to a fresh node with a contact and a started
  signing round, once as it is and once behind each of four tails a previous
  frame leaves in `cmd_frame` (zeros, `0x5A`, `0xFF`, and a pattern with a zero
  every eighth byte). Replies, queued packets, stored files (except the name tails
  in `/channels2`, see 10a), calls to board, radio, clock and sensors, and what 12
  more loops, a `GET_CHANNEL` and a channel flood show afterwards, must be
  identical.
- `OneByteBelowEachMinimum`: for each of the 44 rows with a minimum above 1, the
  catalog frame at the minimum minus one byte answers `UNSUPPORTED`
  (`SEND_CHANNEL_DATA`: `ILLEGAL_ARG`) behind every tail and queues nothing; and the
  catalog has a frame of exactly each minimum.
- `OnlyAnAnnouncedPathRunsOut`: every code 0 to 69 at every length with six
  fills, and every catalog frame; the reader may run out only for an announced
  path in `SEND_RAW_DATA` and `SEND_CHANNEL_DATA`.
- `TheReadsPastTheEndThatStep10cRemoved`: the three handler cases of the
  inventory (`SEND_CHANNEL_DATA` and `SEND_RAW_DATA` with a path length byte that
  announces 8 bytes, `SET_DEFAULT_FLOOD_SCOPE` with 47 bytes and no zero), each
  behind four tails chosen so that the stale bytes would complete the path and
  payload or end the name early. Every time: `ILLEGAL_ARG`, nothing queued, no file
  changed (the prefs file holds the default scope). Passes on the handlers of
  `68e0fcdc` and on the converted ones; a mutation that accepts the 47 byte name
  fails it.
- `FloodScopeKeyNeedsAllSixteenBytes`: added after a mutation survived (below).
  The key's effect only shows in the next flood, which carries a transport code
  with 16 key bytes and none with 15.
- `test_frame_reader.cpp`: six tests of the reader alone.

One finding of writing the catalog: the upper bound of `SEND_TRACE_PATH`
(`len - 10 >= MAX_PACKET_PAYLOAD - 5`, so 189 bytes) lies above `MAX_FRAME_SIZE`
(176) and is never reached; its `return false` is dead code today. Kept as it is.

### Mutation tests

23 faults, each on its own against the converted code, the whole
`native_companion` suite run, the file restored; 19 caught:

| Mutation | Caught by |
|---|---|
| `SET_TUNING_PARAMS` reads `af` with `u16()` | sweep |
| `SET_TUNING_PARAMS` reads `af` before `rx` | `TuningParamsRoundTrip` |
| `RUN_CLI_COMMAND` text starts one byte late | `RunCliCommand` |
| `SET_OTHER_PARAMS` reads the location policy without `has(1)` | `OnlyAnAnnouncedPathRunsOut` |
| `SET_AUTOADD_CONFIG` hop count behind `has(0)` | `AutoAddConfig`, `OnlyAnAnnouncedPathRunsOut` |
| `SET_CHANNEL` 256 bit test `has(64)` instead of `has(65)` | sweep |
| `SET_FLOOD_SCOPE_KEY` key behind `has(17)` | `FloodScopeKeyNeedsAllSixteenBytes` (not caught before that test existed) |
| `SET_FLOOD_SCOPE_KEY` key behind `has(15)` | crash (null pointer from `bytes(16)`) |
| `SET_DEFAULT_FLOOD_SCOPE` behind `has(46)` | crash (null pointer from `bytes(16)`) |
| `SEND_TELEMETRY_REQ` self form also at 5 bytes | `TelemetryHasTwoForms`, sweep |
| `SEND_RAW_DATA` payload test `has(3)` | `SendRawData`, sweep |
| `pathByteLen()` ignores the hash size | sweep |
| `SEND_TRACE_PATH` reads `flags` before `auth` | `TracePath` |
| `SET_ADVERT_LATLON` takes `lon` from `lat` | `SetAdvertLatLon` |
| reader: `has()` off by one (`<` for `<=`) | crash |
| reader: `has()` allows one byte past the end | 10 tests, among them the sweep and `NoCommandReadsThePreviousFrame` |
| reader: terminator one byte late | `NoCommandReadsThePreviousFrame`, `RestAsCStringWritesTheTerminatorAtLen` |
| reader: terminator not written | the same and the sweep |
| reader: `ok()` not sticky | `AReadPastTheEndYieldsZeroAndStops`, sweep |

Not caught (4), and why:

- `SET_CHANNEL` with `has(48)` instead of `has(49)`: equivalent. The row's
  minimum is 50, so the handler never sees 49 bytes; the handler's own test is
  redundant with the table.
- `SEND_RAW_DATA` and `SEND_CHANNEL_DATA` without `!r.ok()`: equivalent. Once a
  read ran out, `has()` is false as well, so the `has()` test alone refuses the
  frame. The `!r.ok()` stays because it says what is meant.
- `SEND_CHANNEL_DATA` with the two `data_type` bytes swapped: not observable on the
  host. The mock cipher leaves no plaintext in the queued packet, and
  `sendGroupData()` is not virtual, so the harness cannot see the value. The code
  reads low byte, then high byte, as the shift expression it replaces did; checked
  by review only.

### Host suites

All 17 host environments on the committed tree, the environment list passed as a
bash array:

```
native_companion                test_companion_frames         PASSED
... (the other 26 suites in 16 environments)                  PASSED
================ 588 test cases: 588 succeeded ================
```

577 before the step, 11 new: five in `test_frame_bounds.cpp`, six in
`test_frame_reader.cpp`.

### Builds

19 environments built from `68e0fcdc` and from the code commit `90b5da2e`, each in
its own worktree with the same toolchain. All build on both sides.

| Environment | Flash before | Flash after | Delta | RAM delta |
|---|---:|---:|---:|---:|
| `Heltec_v3_companion_radio_ble` | 1,670,229 | 1,671,653 | +1,424 | +0 |
| `Heltec_v3_companion_radio_wifi` | 1,106,381 | 1,107,821 | +1,440 | +0 |
| `Heltec_v3_companion_radio_usb` | 656,829 | 658,193 | +1,364 | +0 |
| `RAK_4631_companion_radio_ble` | 464,212 | 465,172 | +960 | +0 |
| `RAK_4631_companion_radio_usb` | 455,188 | 456,148 | +960 | +0 |
| `ProMicro_companion_radio_usb` | 358,836 | 359,796 | +960 | +0 |
| `LilyGo_T-Echo_companion_radio_ble` | 375,680 | 376,640 | +960 | +0 |
| `LilyGo_T-Echo_companion_radio_usb` | 368,288 | 369,248 | +960 | +0 |
| `PicoW_companion_radio_ble` | 633,604 | 635,332 | +1,728 | +0 |
| `PicoW_companion_radio_wifi` | 528,732 | 530,428 | +1,696 | +0 |
| `PicoW_companion_radio_usb` | 495,284 | 497,012 | +1,728 | +0 |
| `LilyGo_Tlora_C6_companion_radio_ble_` | 1,444,915 | 1,446,667 | +1,752 | +0 |
| `M5Stack_Unit_C6L_companion_radio_usb` | 544,593 | 546,345 | +1,752 | +0 |
| `wio-e5-mini_companion_radio_usb` | 209,016 | 211,296 | +2,280 | +0 |
| `Xiao_C3_companion_radio_ble` | 1,255,482 | 1,256,962 | +1,480 | +0 |
| `Xiao_C3_companion_radio_usb` | 543,422 | 544,902 | +1,480 | +0 |
| `Xiao_C3_companion_radio_wifi` | 1,010,858 | 1,012,330 | +1,472 | +0 |
| `Heltec_v3_repeater` | 1,175,021 | 1,175,021 | +0 | +0 |
| `RAK_4631_repeater` | 441,944 | 441,944 | +0 | +0 |

The seventeen companions cover the five platform packages that compile
`MyMesh.cpp` and every interface each of them builds (ESP32-S3 BLE, USB and WiFi;
ESP32-C3 BLE, USB and WiFi; ESP32-C6 BLE and USB; nRF52 BLE and USB; RP2040 BLE,
USB and WiFi; STM32 USB). The repeaters do not compile the file and are the
control.

The design's bound, 1 kB on nRF52, holds: +960 bytes. The other cores pay more
for the inlined bounds tests: about 1.4 kB on ESP32-S3 and C3, 1.7 kB on RP2040
and C6, 2.3 kB on STM32, where `wio-e5-mini` goes from 79.7 % to 80.6 % of its
256 kB. Making the reader's two checking functions `noinline` was tried and does
not help (T-Echo +2,240 instead of +960, `wio-e5-mini` unchanged), so the reader
stays inline. An unchecked variant for the reads inside the minimum, which the
design kept in reserve, was not needed for the bound and would give back the
guarantee the step is for.

### Hardware

LilyGo T-Echo `techo-3dcf` (claimed with `kb device claim`), a companion with its
contacts and channels, normally on the official 1.18.0 BLE image. Two USB
companion images built from `68e0fcdc` and from `90b5da2e`, marked `c10c-base` and
`c10c-head` and confirmed by `ver` before each run.

- `companion_session.py` once per image (39 accepted frames: every setter with the
  value the board holds, declining sub commands, out of range values, stats, self
  telemetry, a signing round, channels, CLI, the wrong reboot and reset words) and
  compared: the only two differing lines are the build marker inside
  `DEVICE_QUERY` and the reply to `ver`.
- `companion_sweep.py` against the 10c image:

  ```
  6502 frames in 751 s: 6502 as on the host, 0 different, 0 without reply
  (compared byte for byte: 4083, by reply code: 2419)
  codes covered: 251
  ```

- State read through the protocol (`nodetool.py compinfo`) before the first run and
  after the sweep: identical.
- Afterwards the official image went back on
  (`LilyGo_T-Echo_companion_radio_ble-v1.18.0-81dbae5.zip`, SHA-256 prefix
  `6d602a54`); the DFU reported success and the board enumerated again. The phone
  this board serves was not connected, so the BLE reconnect was not checked.

Not covered on hardware: the setters with new values, sends on air, `REBOOT`,
`FACTORY_RESET`, private key import, contact and channel changes, and the stale
tail cases (a board cannot be made to hold a chosen tail without sending that
frame first, which the host tests do). For those the host tests are the evidence.
Only nRF52 ran on hardware.

### Open findings, not changed in 10c

Each needs its own fix and proposal; 10c leaves the code as it is.

1. **`Dispatcher::tryParsePacket()` reads before its length test.** For a route
   type with transport codes it reads the 4 code bytes and the path length byte
   before it compares with `len`. Reached from `SEND_RAW_PACKET` with frames of 4
   to 6 bytes, and on every received radio packet. The answer is unchanged
   (`ILLEGAL_ARG`, the later test fails). Reproduce: apply
   `tools/refactoring/companion_asan_probe.patch`, run it with `PROBE_SKIP=1`, and
   `companion_asan_probe_summary.py` lists `MyMesh::cmdSendRawPacket` with
   `mesh::Dispatcher::tryParsePacket` for code 65, fills `00` and `3f`, lengths 4
   to 6 (see `tools/refactoring/README.md`).
2. **The CLI parser compares prefixes past the terminator.**
   `CommonRadioPrefs::handleCommand()`, `MyMesh::handleCommand()` and `isGet()` use
   `memcmp()` with prefixes longer than the command text. The result is decided at
   the terminator, so no reply changes. Reproduce: the same probe lists
   `MyMesh::cmdRunCliCommand` with those three callers for code 66, lengths 3 to 22.
3. **Packet digests under ASan (unchecked).** Built with ASan, the frame sweep's
   packet digests differ for codes 2, 3, 26, 27 and 39. Guess, not verified: the encryption pads from an uninitialised stack
   buffer that the sweep's stack scrub reaches only without ASan. Reproduce: apply
   the probe patch and run `pio test -e native_companion` without a filter;
   `FrameSweep.MatchesTheRecordedTranscript` fails; the differences it printed (it
   prints at most ten) were all `packets` lines of those codes, with equal `files`
   digests.
4. **`SEND_TRACE_PATH`'s upper bound is dead code.** `len - 10 >= MAX_PACKET_PAYLOAD - 5`
   needs 189 bytes; a frame has at most 176.
