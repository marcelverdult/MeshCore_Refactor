# A short CMD_SEND_CHANNEL_TXT_MSG frame stops the node

Status: fixed, verified on hardware. Behaviour change: a frame below 7 bytes is
answered with an error instead of being parsed.

## Symptom

A companion frame `03 00 00` (CMD_SEND_CHANNEL_TXT_MSG, 3 bytes) makes the node
stop answering and it never comes back. Measured on a LilyGo T-Echo running `dev`
428a95c6 over the USB companion interface:

```
CH_TXT len=7    -> RESP_CODE_OK
GET_STATS core  -> uptime=78s
CH_TXT len=3    -> NO REPLY
DEVICE_QUERY    -> NO REPLY   (three times over 8 s)
```

USB stayed enumerated under the application id `0x239a:0x8029` while the main
loop was gone. Neither a 1200 baud touch nor the reset button brought it back,
only unplugging the cable, so the fault state is not something a user recovers
from by restarting the app.

## Cause

`handleCmdFrame()` reads the fields of this command at fixed offsets: `txt_type`
at 1, `channel_idx` at 2, a 4 byte timestamp at 3, and the text from 7 on. The
branch had no length guard, so the text length is computed as `len - i` with
`i == 7` (`examples/companion_radio/MyMesh.cpp:1178`).

`len` is a `size_t`. For `len == 3` the subtraction wraps, and the result reaches
`BaseChatMesh::sendGroupMessage()` through an `int` parameter as `-4`. There the
clamp is

```c
if (text_len + prefix_len > MAX_TEXT_LEN) text_len = MAX_TEXT_LEN - prefix_len;
memcpy(ep, text, text_len);
```

A negative value passes that test, and `memcpy` converts the length back to a
`size_t`: the call asks for 4294967292 bytes into a 197 byte stack buffer
(`src/helpers/BaseChatMesh.cpp:518,526-527`).

The same chain reproduces on the host, away from the radio and the channel table:

```
$ bugdemo 5    calling sendGroupMessage(text_len=5) ...   returned false   exit=0
$ bugdemo -4   calling sendGroupMessage(text_len=-4) ...                   exit=139
```

### Root versus proximate

The missing `len >= 7` is the proximate cause. One layer down, the frame handler
derives payload lengths by subtraction without ever testing that the frame is
long enough; 35 of its 62 branches state their minimum in the condition and this
one did not. The other 14 are the subject of
`docs/fixes/companion-cmd-frame-min-len.md`.

One layer further down, `sendGroupMessage()` trusts a length its caller computed,
although its own sibling `sendGroupData()` in the same file already refuses a
negative one (`src/helpers/BaseChatMesh.cpp:538-541`). The library had no
invariant of its own, so any caller could do the same damage.

## Fix

Two guards, one per mistake.

- `examples/companion_radio/MyMesh.cpp`: the branch condition gains `len >= 7`,
  the way the neighbouring `CMD_SEND_TXT_MSG` branch states `len >= 14`. A
  shorter frame falls through to the existing final `else` and is answered with
  `ERR_CODE_UNSUPPORTED_CMD`, which is what the 35 already guarded branches
  answer for a frame that is too short. A frame of exactly 7 bytes keeps working
  and sends an empty message.
- `src/helpers/BaseChatMesh.cpp`: `sendGroupMessage()` refuses `text_len < 0`,
  mirroring `sendGroupData()`.

### Alternatives considered

- **Only the frame guard.** Rejected: it leaves the library able to corrupt
  memory for any other caller, and it cannot be tested on the host, because
  `MyMesh.cpp` needs `target.h`, the board, the radio driver, the sensors, the
  telemetry and the store.
- **Only the library guard.** Rejected: the frame handler would still read a
  timestamp out of the tail of an earlier frame and send a message with it.
- **A central table of minimum lengths**, checked once at the top of
  `handleCmdFrame()` and answering `ERR_CODE_ILLEGAL_ARG`. This is the better
  long term shape: the minimums would live in one place, the next command could
  not forget one, and the table would be testable on the host. It is not part of
  this fix because it changes the answer for the 35 branches that already guard,
  from `ERR_CODE_UNSUPPORTED_CMD` to `ERR_CODE_ILLEGAL_ARG`, which is a visible
  protocol change and does not belong in the same change as a crash fix.
- **Clamping a negative length to 0** instead of refusing. Rejected: it would
  send an empty message for a frame the sender got wrong, and silence is worse
  than an error code here.

## Verification

### Host tests

`test/test_chat_group_msg` runs in the new `native_chat` environment, which
builds the real `Utils.cpp`, `Packet.cpp`, `Dispatcher.cpp`, `Mesh.cpp`,
`BaseChatMesh.cpp`, `AdvertDataHelpers.cpp` and `TxtDataHelpers.cpp` against
fakes for radio, clock, RNG and packet manager, and against the link time
identity stubs the board crypto would otherwise provide. The packet manager keeps
every packet that was queued, so "refused before sending" and "sent" are told
apart by whether a packet was queued at all, and the payload length the chat
layer produced is observable.

The suite was written first and is red on the unchanged code:

```
$ pio test -e native_chat
Program received signal SIGBUS (Bus error: 10)

$ pio test -e native_chat -a "--gtest_filter=-GroupMessageTest.Refuses*"
6 test cases: 6 succeeded in 00:00:03.471
```

The six cases that describe today's behaviour pass before the fix, so the
expectations are right; only the two negative lengths bring the process down.
After the fix, on the committed tree with nothing uncommitted:

```
native             test_mesh_tables        PASSED
native             test_config_serializer  PASSED
native             test_utils              PASSED
native             test_utf8_helpers       PASSED
native             test_routing_policy     PASSED
native_chat        test_chat_group_msg     PASSED
native_kiss_modem  test_kiss_modem         PASSED
62 test cases: 62 succeeded in 00:00:33.664
```

`test_companion_node_prefs` is reported SKIPPED. It is also SKIPPED on unchanged
`dev` (46 of 46 there), so that is not a change from this fix.

`native_chat` is added to the environment list in
`.github/workflows/run-unit-tests.yml`; without that the workflow would never run
the suite, because it names its environments explicitly.

### Mutation tests

Each fault was introduced on its own and the suite had to fail:

| Mutation | Result |
|----------|--------|
| guard removed | SIGBUS |
| guard only below -10 | SIGBUS |
| guard returns `true` | 2 cases fail |
| clamp forgets the prefix | `ClampsTextThatIsTooLong` fails |
| clamp `>` becomes `>=` | not caught, see below |
| datagram length off by one | `SendsTextAcrossABlockBoundary` fails |
| guard refuses 0 as well | `SendsEmptyText` fails |

The clamp mutation is an equivalent mutant, not a gap: `>` and `>=` differ only
when `text_len + prefix_len == MAX_TEXT_LEN`, and in that case the clamp assigns
`MAX_TEXT_LEN - prefix_len`, which is the value `text_len` already has. The
behaviour is identical for every input.

The suite cannot cover the frame guard, and that was measured rather than
assumed: with `len >= 7` mutated to `len >= 1`, `native_chat` still reports 8 of
8 passing, because the environment does not build `MyMesh.cpp`. That half of the
fix rests on the hardware check below.

### Builds

The 18 environments of the review series plus both T-Echo companion environments
were built from `dev` (428a95c6) and from this branch with the same toolchain. All
20 build on both sides. RAM is unchanged everywhere. Only the environments that
build the companion example change size at all:

| Environment | Flash `dev` | Flash fix | Delta |
|---|---:|---:|---:|
| `Heltec_v3_companion_radio_ble` | 1,666,437 | 1,666,457 | +20 |
| `Heltec_v3_companion_radio_wifi` | 1,102,677 | 1,102,693 | +16 |
| `RAK_4631_companion_radio_ble` | 462,548 | 462,532 | -16 |
| `ProMicro_companion_radio_usb` | 357,300 | 357,284 | -16 |
| `LilyGo_T-Echo_companion_radio_ble` | 374,096 | 374,080 | -16 |
| `LilyGo_T-Echo_companion_radio_usb` | 366,704 | 366,688 | -16 |

The other 14 environments, all repeater, room server and sensor roles, report the
same flash and RAM size as `dev`. Two independent readings say why, and the
second corrects the obvious guess:

- None of the 14 has `+<../examples/companion_radio/*.cpp>` in its
  `build_src_filter`, so `MyMesh.cpp` is not part of those builds. Exactly the
  six environments that do have it are the six whose size changed.
- `BaseChatMesh.cpp` *is* compiled in all of them (the object file is there), but
  `sendGroupMessage` is not linked into their images. `nm` finds the symbol once
  in `RAK_4631_companion_radio_ble` and not at all in `Heltec_v3_repeater` or
  `RAK_4631_repeater`, on both sides of the change: the roles never call it, so
  the linker drops it.

Their images are not byte identical: the difference is 69 bytes of build
metadata, located by comparing the two binaries and reading the differing
regions. For `Heltec_v3_repeater` they are the 32 byte ELF SHA-256 in the ESP32
app descriptor at offset 177, the `__TIME__` string (`"22:32:04"` against
`"23:05:08"`) at offset 274217, and the 33 byte image hash esptool appends at the
end. The stock environments do not set `SOURCE_DATE_EPOCH`, so two builds of the
same source always differ in those bytes.

### Hardware

LilyGo T-Echo `techo-3dcf`, USB companion build, images marked through
`FIRMWARE_BUILD_DATE` and confirmed by `ver` before each run.

Before, on `dev` reporting `v1.18.0 (Build: f1-dev)`: see the symptom above.

After, on this branch reporting `v1.18.0 (Build: f1-fix1)`:

```
CH_TXT len=1 .. len=6   -> ERR UNSUPPORTED_CMD   (each one)
uptime                  -> 23 s before, 35 s after, so no restart
CH_TXT len=7 (empty)    -> RESP_CODE_OK
CH_TXT len=12 (text)    -> RESP_CODE_OK, node transmits
CH_TXT txt_type=1       -> ERR UNSUPPORTED_CMD, unchanged
DEVICE_QUERY            -> answers, before and after
```

Downgrading to the `dev` image afterwards left every stored value unchanged:
tx_power 22, freq 869.618, bw 62.5, sf 8, cr 8, rx_delay 0, airtime_factor 1.0,
name `android_dev_3`, BLE pin, path hash mode 2, 68 contacts.

Two observations from the runs that this fix does not change, noted because they
look like findings and are not:

- A message addressed to a channel index that is in range but has no channel
  configured is accepted. `BaseChatMesh::getChannel()` returns true for every
  index below `MAX_GROUP_CHANNELS` and hands out a zeroed channel, so the node
  sends on a zero secret. That is unchanged upstream behaviour.
- The crash leaves USB enumerated while the main loop is dead, so a host sees a
  device that cannot be opened. That is a property of the fault, not of the fix.
