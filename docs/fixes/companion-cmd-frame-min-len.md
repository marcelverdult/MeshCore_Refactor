# Fourteen companion commands act on the previous frame's bytes

Status: fixed, measured on hardware for 8 of the 14 commands. Behaviour change:
a frame too short for its command is answered with an error instead of being
parsed.

## Symptom

`handleCmdFrame()` reads the fields of each command at fixed offsets derived from
the command code. Thirty-five of its sixty-two branches state the length they
need in the condition, for example `CMD_SEND_TXT_MSG && len >= 14`. Fourteen do
not, or test `cmd_frame[1]` before the length. Those branches read fields that
the frame does not contain.

What they read is not uninitialised memory. `cmd_frame` is a member of `MyMesh`
(`examples/companion_radio/MyMesh.h:267`), `ArduinoSerialInterface::rx_buf` is a
member too (`src/helpers/ArduinoSerialInterface.h:12`), and `checkRecvFrame()`
copies only `_frame_len` bytes out (`.cpp:66`). Everything above `len` is
therefore the tail of an earlier frame, and that is what the handler acts on.

For these fourteen commands no read leaves the buffer: their largest
unconditional offset is 135 and the buffer holds `MAX_FRAME_SIZE + 1 = 177`
bytes. `checkRecvFrame()` also clamps `len` to `MAX_FRAME_SIZE` and never returns
0, so `len` is always between 1 and 176. For them this is a stale data bug, not
an out of bounds bug.

Two related defects in the same function are deliberately not part of this
change, because they are a different fault and each deserves its own review:

- `CMD_SEND_CHANNEL_TXT_MSG` turns the length itself into the argument of a
  `memcpy`. That one is `docs/fixes/companion-channel-txt-min-len.md`.
- `CMD_SET_DEFAULT_FLOOD_SCOPE` calls `strlen((char *) &cmd_frame[1])` with no
  bound. `len` is clamped to 176, so a 176 byte frame of non-zero bytes fills
  offsets 0 to 175, and if the stale byte at 176 is non-zero too there is no zero
  left in `cmd_frame`. The scan then runs past the array into `out_frame`, which
  is the next member, and stops at the first zero there. The `strcpy` that follows
  is safe, because `n < 31` is tested and the field is `char[31]`, and `n` is
  never returned, so nothing leaks and nothing is written out of bounds. It is
  still an unbounded read of data the host controls, and a length check cannot fix
  it: it needs `strnlen` or a scan limited to `len`.

## The minimums

Each is the offset just past the last byte its branch reads without first testing
`len`.

| Command | Code | Minimum | What it reads unconditionally |
|---|---:|---:|---|
| `CMD_SEND_CHANNEL_DATA` | 62 | 3 + path + 2 | `channel_idx`, `path_len`, the path, then the 2 byte `data_type` |
| `CMD_ADD_UPDATE_CONTACT` | 9 | 136 | `updateContactFromFrame()` reads to offset 135 |
| `CMD_REMOVE_CONTACT` | 15 | 33 | `pub_key[1..32]` |
| `CMD_SHARE_CONTACT` | 16 | 33 | `pub_key[1..32]` |
| `CMD_GET_CONTACT_BY_KEY` | 30 | 33 | `pub_key[1..32]` |
| `CMD_SET_RADIO_PARAMS` | 11 | 11 | `freq`, `bw`, `sf`, `cr`; the repeat flag stays optional |
| `CMD_SET_RADIO_TX_POWER` | 12 | 2 | `power` |
| `CMD_SET_TUNING_PARAMS` | 21 | 9 | `rx_delay_base`, `airtime_factor`, neither range checked |
| `CMD_SET_OTHER_PARAMS` | 38 | 2 | `manual_add_contacts`; the rest stays optional |
| `CMD_SET_PATH_HASH_MODE` | 61 | 3 | operand order only, the constant was already there |
| `CMD_REBOOT` | 19 | 7 | `memcmp` against `"reboot"` |
| `CMD_FACTORY_RESET` | 51 | 6 | `memcmp` against `"reset"` |
| `CMD_SET_AUTOADD_CONFIG` | 58 | 2 | `autoadd_config`; `autoadd_max_hops` stays optional |
| `CMD_SEND_PATH_DISCOVERY_REQ` | 52 | 34 | operand order only, the constant was already there |

`CMD_ADD_UPDATE_CONTACT` is the only branch whose existing constant is raised, 36
to 136. `updateContactFromFrame()` reads `type`, `flags`, `out_path_len`, 64 bytes
of `out_path`, 32 bytes of `name` and the 4 byte `last_advert_timestamp` without
testing `len`; only `gps_lat`, `gps_lon` and `lastmod` are optional
(`MyMesh.cpp:2048-2070`). No valid frame can be shorter: the reverse direction
`writeContactRespFrame()` writes the same fields in the same order and sends 148
bytes.

## Fix

Twelve branches gain `&& len >= N` in the condition, so a short frame falls
through to the existing final `else` and is answered the way the 35 already
guarded branches answer it. `CMD_SET_PATH_HASH_MODE` and
`CMD_SEND_PATH_DISCOVERY_REQ` only have their existing length test moved in front
of the `cmd_frame[1]` test. `CMD_SEND_CHANNEL_DATA` keeps its explicit `len < 4`
check and gains a second one for the `data_type` behind the path, whose offset is
only known once `path_len` has been parsed; that is the shape
`CMD_SEND_RAW_DATA` already uses in the same function.

### Alternatives considered

- **A central table of minimum lengths**, checked once at the top of
  `handleCmdFrame()` and answering `ERR_CODE_ILLEGAL_ARG`. The better long term
  shape: one place for the minimums, nothing for the next command to forget, and
  host testable. Left out of this fix because it changes the answer for the 35
  branches that already guard, from `ERR_CODE_UNSUPPORTED_CMD` to
  `ERR_CODE_ILLEGAL_ARG`, which is a visible protocol change and does not belong
  in the same change as the guards themselves. Worth proposing on its own.
- **Clearing `cmd_frame` between frames.** Rejected: it hides the bug instead of
  fixing it. A command would then act on zeros rather than on the previous
  frame, which for `CMD_SET_TUNING_PARAMS` means writing 0 to the prefs and for
  `CMD_REMOVE_CONTACT` means a lookup for the all zero key. Both are still wrong,
  and the cost is a memset per frame.
- **Answering `ERR_CODE_ILLEGAL_ARG` from each new guard** through an explicit
  check and early return, as `CMD_SEND_CHANNEL_DATA` does. Rejected for the
  twelve: it adds twelve new code paths where extending the condition adds none,
  and it makes this change inconsistent with the 35 neighbours.

## Verification

### No host test, and that was measured

There is no host test for this change. The `native_chat` environment added for
the group message length does not build `MyMesh.cpp`, and compiling it on the
host would need stand-ins for `target.h`, the board, the radio driver, the
sensors, the telemetry and the store.

That the host suite cannot see this file was measured rather than assumed: with
`len >= 7` in the neighbouring branch mutated to `len >= 1`, `native_chat` still
reports 8 of 8 passing. The suites that do exist stay green:

```
native             test_mesh_tables        PASSED
native             test_config_serializer  PASSED
native             test_utils              PASSED
native             test_utf8_helpers       PASSED
native             test_routing_policy     PASSED
native_kiss_modem  test_kiss_modem         PASSED
54 test cases: 54 succeeded in 00:00:51.743
```

### Hardware, before and after

LilyGo T-Echo `techo-3dcf`, USB companion build, images marked through
`FIRMWARE_BUILD_DATE` and confirmed by `ver` before each run: `f1-dev` for
`dev` 428a95c6, `f1-fix2` for this branch. Every step first sends a longer frame,
so the stale tail is known, and then a frame too short for the command.

| Step | On `dev` | On this branch |
|---|---|---|
| `[21]` after a CLI frame carrying `"get name"` | tuning (0, 1000) becomes (544499136, 1701667200) | `ERR_CODE_UNSUPPORTED_CMD`, tuning stays (0, 1000) |
| `[12]` after `GET_CHANNEL 5` | tx_power 22 becomes 5 | error, tx_power stays 22 |
| `[38]` after `GET_CHANNEL 0` | manual_add 1 becomes 0 | error, manual_add stays 1 |
| `[58]` after `GET_CHANNEL 0` | autoadd (5, 0) becomes (0, 0) | error, autoadd stays (5, 0) |
| `[30]` after a full keyed frame | a complete contact record is returned | error, nothing returned |
| `[62]` with len 4 | `RESP_CODE_OK`, a datagram goes out with a `data_type` built from a stale byte | `ERR_CODE_ILLEGAL_ARG` |
| `[9]` with len 36 after a 141 byte CLI frame of `"Z"` | a contact is created with `out_path` = 5a5a..., `name` = "ZZZZ...Z", `last_advert_timestamp` = 0x5a5a5a5a | error, no contact created |
| `[19]` after a CLI frame carrying `"reboot"` | uptime 258 s becomes 19 s, the node rebooted | error, uptime 68 s becomes 73 s, no restart |
| `[61]`, `[52]` with len 1 | `ERR_CODE_UNSUPPORTED_CMD` | unchanged, as expected: the only fault there is one read above `len` |

The `[21]` numbers are worth spelling out, because they identify the source
exactly. The stale bytes are `"get "` and `"name"`, which as little endian
uint32 are 544499047 and 1701667182. The prefs hold them as float and the read
multiplies by 1000 again; the same round trip on the host gives 544499072 and
1701667200. `"name"` matches to the byte, `"get "` is one float32 step out, which
is what the nRF52 build allows because it compiles with `-Ofast`
(KB `nrf52-ofast-nan.md`).

`CMD_FACTORY_RESET` was deliberately not triggered on hardware. It formats the
file system and takes identity, prefs, contacts and channels with it. It is the
same mechanism as `CMD_REBOOT`, which is shown above: a frame carrying the text
at offset 1, then a one byte frame with the command code.

Valid frames still work on this branch, checked at the exact minimum where the
command allows it: `[21]` with 9 bytes, `[12]` with 2, `[58]` with 3, `[61]` with
3, `[15]` with 33 and `[30]` with 33 all answered as before, and the node stayed
up throughout.

Afterwards every value was set back and read again: tuning (0, 1000), tx_power
22, manual_add 1, freq 869618, sf 8, cr 8, autoadd (5, 0), path hash mode 2, BLE
pin 207263, default scope "hansemesh" with its key, 68 contacts, and all three
test contacts removed. Fifteen checks, all matching the state recorded before the
runs.

### What the hardware runs do not cover, and how it is covered instead

The runs show that a short frame is refused. They do not show, for every command,
that a frame of exactly the minimum length is still accepted; that was exercised
for six of them (above) and not for the rest, because the board had to go back to
another session. Three further readings close that gap:

- Two of the fourteen are not new constants at all. `CMD_SET_PATH_HASH_MODE`
  already required `len >= 3` and `CMD_SEND_PATH_DISCOVERY_REQ` already required
  `len >= 2 + PUB_KEY_SIZE`; only the operand order changed, so no number can be
  wrong there.
- Every sender in this repository was checked against the minimums.
  `tools/refactoring/nodetool.py:141` sends `CMD_REBOOT` as exactly seven bytes,
  `[19]` followed by `"reboot"`, which is exactly the minimum: a guard one higher
  would have broken it. `docs/companion_protocol.md` gives
  `3E 01 FF FF FF A1 B2 C3` for `CMD_SEND_CHANNEL_DATA` (8 bytes against a
  minimum of 5) and `03 00 01 D2 02 96 49 48 65 6C 6C 6F` for
  `CMD_SEND_CHANNEL_TXT_MSG` (12 against 7).
- For the rest the constant is, by construction, the offset just past the last
  byte the branch reads without testing `len`. A frame that contains the
  command's own mandatory fields satisfies it. The one raised constant,
  `CMD_ADD_UPDATE_CONTACT`, is additionally pinned by the reverse direction
  described above.

### Builds

The 18 environments of the review series plus both T-Echo companion environments
were built from `dev` (428a95c6) and from this branch with the same toolchain.

All 20 build on both sides. 14 report the same flash and RAM size as `dev`.

| Environment | Flash `dev` | Flash fix | Flash delta | RAM delta |
|---|---:|---:|---:|---:|
| `Heltec_v3_repeater` | 1,174,449 | 1,174,449 | +0 | +0 |
| `Heltec_v3_room_server` | 1,169,705 | 1,169,705 | +0 | +0 |
| `Heltec_v3_companion_radio_ble` | 1,666,437 | 1,666,393 | -44 | +0 |
| `Heltec_v3_companion_radio_wifi` | 1,102,677 | 1,102,633 | -44 | +0 |
| `Heltec_v3_sensor` | 1,180,593 | 1,180,593 | +0 | +0 |
| `RAK_4631_repeater` | 440,952 | 440,952 | +0 | +0 |
| `RAK_4631_room_server` | 434,088 | 434,088 | +0 | +0 |
| `RAK_4631_companion_radio_ble` | 462,548 | 462,372 | -176 | +0 |
| `RAK_4631_sensor` | 437,544 | 437,544 | +0 | +0 |
| `PicoW_repeater` | 491,972 | 491,972 | +0 | +0 |
| `wio-e5-mini_repeater` | 203,552 | 203,552 | +0 | +0 |
| `wio-e5-mini_sensor` | 202,904 | 202,904 | +0 | +0 |
| `RAK_3112_repeater_bridge_espnow` | 1,175,717 | 1,175,717 | +0 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | 240,368 | 240,368 | +0 | +0 |
| `ProMicro_repeater` | 345,424 | 345,424 | +0 | +0 |
| `ProMicro_room_server` | 338,456 | 338,456 | +0 | +0 |
| `ProMicro_companion_radio_usb` | 357,300 | 357,124 | -176 | +0 |
| `ProMicro_sensor` | 341,900 | 341,900 | +0 | +0 |
| `LilyGo_T-Echo_companion_radio_ble` | 374,096 | 373,920 | -176 | +0 |
| `LilyGo_T-Echo_companion_radio_usb` | 366,704 | 366,528 | -176 | +0 |


The six that changed are exactly the six that build `MyMesh.cpp`, read from their
`build_src_filter` rather than guessed from the numbers. The other fourteen are
repeater, room server and sensor roles, which do not build the companion example
at all. Their images are not byte identical even so, because the stock
environments do not set `SOURCE_DATE_EPOCH`; the differing bytes are the compile
time string and the two hashes.

Those 20 cover two of the five platform entries that build `MyMesh.cpp`. Measured
with `pio project config --json-output`, the file is compiled by 222 companion
environments across `nordicnrf52` (93), `platformio/espressif32@6.11.0` (112),
`maxgerhardt/platform-raspberrypi` (7),
`pioarduino/platform-espressif32` 53.03.13-1 (6) and `ststm32@19.7.1` (4). The
20 above only reach the first two, and the fork CI builds the other three
toolchains with repeater environments, which do not compile this file. One
companion environment per missing platform was therefore built as well.

One companion environment per platform that the 20 above do not cover. All 3 build on both sides.

| Environment | Flash `dev` | Flash fix | Flash delta | RAM delta |
|---|---:|---:|---:|---:|
| `PicoW_companion_radio_ble` | 633,124 | 633,028 | -96 | -32 |
| `LilyGo_Tlora_C6_companion_radio_ble_` | 1,446,483 | 1,446,549 | +66 | +0 |
| `wio-e5-mini_companion_radio_usb` | 208,024 | 207,992 | -32 | +0 |

