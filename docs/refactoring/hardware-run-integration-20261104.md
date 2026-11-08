# Hardware run of `integration` (bd69fa52), 4 November 2026

Rule 3 of the series asks for runs on real boards for every step that touches
stored data (steps 1 to 4: shared NodePrefs, radio settings table, server base
class), including an upgrade from the previous firmware and a downgrade back.
Until now the merges were covered by host tests and builds only. This note is
the board evidence.

Result in one line: on 8 board and role combinations, no stored setting changed
across upgrade and downgrade, with one expected difference (sensor
`flood.max.unscoped`, step 0006). Several things were not covered; they are
listed under "Not verified".

## Setup

| | Base (v1.18.0) | Candidate |
|---|---|---|
| Source | `81dbae59` (tag state of v1.18.0), built locally in a separate worktree | `integration` at `bd69fa52` |
| `ver` output | `v1.18.0 (Build: 15 Oct 2026)` | `v1.18.0 (Build: 15 Oct 2026)` (identical, see below) |

The base is a local rebuild of `81dbae59`, not the downloaded official release
binary. The `ver` string is the same for both builds, so the artifact identity
rests on the build hashes below, on the flasher logs, and on one behavioural
marker (sensor `flood.max.unscoped`, which only the candidate changes).

Flashing keeps the stored settings:

- nRF52 (FakeTec ProMicro): `adafruit-nrfutil dfu serial -pkg firmware.zip -b 115200 --touch 1200`
  (application only; the file system with `/prefs.json` is not erased).
- ESP32-S3: `pio run -e <env> -t upload --upload-port <port>` (bootloader, partition
  table and application; the file system partition is not written). No erase.
- Settings were read with a serial CLI helper (`get ...`) for repeater, room
  server and sensor, and with `meshcore-cli 1.6.9 infos` for the companions.

### Artifacts (sha256, first 12 characters)

| Env | Base | Candidate |
|---|---|---|
| ProMicro_repeater (zip) | 300a72521167 | 1d82f4eeaf58 |
| ProMicro_room_server (zip) | 70d366014ece | 3d446df1e751 |
| ProMicro_sensor (zip) | 216dd0510cd8 | 9bc1318862f0 |
| ProMicro_companion_radio_usb (zip) | c404020fff5b | 7e2d53cf6cf5 |
| Heltec_v3_repeater (bin) | 7ac26d6baded | b86a70242a48 |
| Heltec_v3_sensor (bin) | 5826136aca64 | bdc6a679425a |
| heltec_v4_room_server (bin) | d9f82b5da5d4 | 3194d6ce9da4 |
| heltec_v4_companion_radio_usb (bin) | 8563ca6dd8d5 | 3534d0cf4df4 |

### Values set on the base and read back at each phase

Radio `869.525,125,10,6`, `tx 7`, `af 1.5`, `rxdelay 1.5`, `txdelay 0.7`,
`direct.txdelay 0.4`, `multi.acks 1`, `flood.max 40`, `flood.max.advert 20`,
`int.thresh 14`, `agc.reset.interval 16`, `lat 52.1234`, `lon 8.4321`,
`guest.password hwguest`, `owner.info hwrun-owner`, `loop.detect minimal`,
`advert.interval 120`, `flood.advert.interval 24`, `repeat on`, admin
`password hwrunpw`, a per-board `name`. Read back: all of these plus
`flood.max.unscoped`, `public.key`, `role`, `allow.read.only`, `path.hash.mode`,
`radio.rxgain`, `cad`, `region`, `region list allowed`. The admin password
cannot be read, only used (see the login checks).

Companions (`meshcore-cli set ...`): name, radio, tx, coords, `multi_acks 1`,
`telemetry_mode_base/loc/env`, `manual_add_contacts on`; read back with `infos`.

## Result

Phases: 1 = base after setting the values, 2 = candidate flashed over it,
3 = base flashed again over that. "Identical" means a `diff` of the complete
read-back text of phase 1 against phase 2 and against phase 3.

| Board (port) | Role / env | 1 to 2 (upgrade) | 1 to 3 (downgrade) |
|---|---|---|---|
| faketec-6060 (ProMicro nRF52840) | repeater, ProMicro_repeater | identical (31 values) | identical |
| faketec-0224 | room server, ProMicro_room_server | identical | identical |
| faketec-6502 | sensor, ProMicro_sensor | one difference: `flood.max.unscoped` 0 to 64 | same difference remains (64) |
| faketec-80da | companion USB, ProMicro_companion_radio_usb | identical (`infos`) | identical |
| heltecv3-jork (Heltec V3, ESP32-S3) | repeater, Heltec_v3_repeater | identical | identical |
| heltecv3-jork | sensor, Heltec_v3_sensor | one difference, same as nRF52 sensor | same (64 stays) |
| heltecv4-80f1 (Heltec V4, ESP32-S3) | room server, heltec_v4_room_server | identical | identical |
| heltecv4-80f1 | companion USB, heltec_v4_companion_radio_usb | identical | identical |

### The one difference

`get flood.max.unscoped` on the sensor reads `0` on the base and `64` after the
upgrade, on both platforms. This is the intended one-time migration of step
[0006](0006-sensor-flood-limits.md): the v1.18.0 sensor stored `0` without ever
applying it, and the candidate rewrites it to the role default. After the
downgrade the value stays `64`, because the file now carries the new value and
v1.18.0 reads the file (and the added version key) without complaint. Nothing
else on the sensor changed (`flood.max.advert` stayed `20`, `flood.max` `40`).
This is a deviation from "byte for byte identical" only in the sense that 0006
was split out as a behaviour change on purpose.

On the V3 sensor the first three lines of the phase 1 text (`ver`, `get name`,
`get radio`) were read while the board was still booting and are garbled in the
capture. The same three values read correctly in phases 2 and 3 (name
`HWRUN-v3sens`, radio `869.5250244,125,10,6`), and the `set` replies in phase 1
were all `OK`. I treat this as a capture problem, not as a result.

## Behaviour between boards (candidate firmware, nRF52 set only)

The four nRF52 boards ran the candidate at the same time, all on
`869.525/125/10/6`, with the companion `faketec-80da` on USB:

```
$ meshcore-cli -s <companion> contacts | grep HWRUN
HWRUN-sens   SENS ee98752c9107
HWRUN-room   ROOM 82dd59eef33a
HWRUN-rep    REP  b9a6eff00615
$ meshcore-cli ... login HWRUN-room hwrunpw   -> Login success
$ meshcore-cli ... login HWRUN-room wrongpw   -> Login failed : Error or Timeout waiting response
$ meshcore-cli ... login HWRUN-rep hwrunpw    -> Login success
$ meshcore-cli ... login HWRUN-sens hwrunpw   -> Login success
$ meshcore-cli ... req_telemetry HWRUN-sens   -> voltage 4.06, temperature 34.2
$ meshcore-cli ... req_status HWRUN-rep       -> bat 4272, nb_recv 20, nb_sent 10, sent_flood 9 ...
```

The old stored admin password survives the upgrade (login with it works on all
three server roles). Repeater counters around a flood advert sent by the
companion (`stats-packets`):

```
before: {"recv":27,"sent":14,"flood_tx":12,"direct_tx":2,"flood_rx":18,"direct_rx":9,...}
after:  {"recv":29,"sent":15,"flood_tx":13,"direct_tx":2,"flood_rx":20,"direct_rx":9,...}
```

Two floods received and one flood sent. That is consistent with forwarding, but
one counter step does not separate a forward from the repeater's own advert, so
I call it weak evidence only.

`req_status` against the sensor returned "Error getting data" on the candidate.
I did not run it against the base, so whether that is a difference is open.

## Incidents during the run (for Marcel)

1. **Wrong Heltec V3 flashed once.** The two Heltec V3 boards share a USB serial
   number, and the port assignment in the device list had swapped: the port
   registered for `heltecv3-jork` (`/dev/cu.usbserial-0001`) now belonged to
   `heltecv3-dutch` (public key `D6ADD22B...`, MAC `34:b7:da:56:91:7c`, "Dutch
   Embassy"). I flashed `Heltec_v3_repeater` v1.18.0 onto it once, application
   only, before checking the public key. Its stored settings and identity are
   unchanged (name `Dutch Embassy`, radio `869.6179809,62.5,8,5`), but it now
   runs v1.18.0 instead of `v1.13.0-evo_0.1.9-9e5dbf2`. The 8 MB dump the device
   note refers to no longer exists under `~/Projects/_backup/firmware_dumps/`,
   so I could not put the old image back. I did not touch it again and did not
   claim it. The real jork is on `/dev/cu.usbserial-12` (public key
   `E936D957...`, MAC `48:ca:43:59:74:38`).
2. **Previous contents of the boards are gone** (they were free to reflash, but
   not restored, as the task ends on v1.18.0):
   `heltecv3-jork` was "Jork Observer" on `1.14.1-letsmesh.net-dev-2026-03-20`
   (no dump available, the 22 October dump is also missing);
   `faketec-6060` was the companion "kb-sender" (869.618/62.5/8/8, tx 2);
   `faketec-80da` was a companion "Demo_03" (tx 22);
   `faketec-6502` carried repeater prefs named "clock test repeater";
   `heltecv4-80f1` was the WiFi companion `wifi_app_test` on v1.15.0. It keeps
   that identity (public key `c1bcec9d...`) and is now a USB companion on
   v1.18.0 (base).
3. After flashing, `heltecv4-80f1` enumerates as `/dev/cu.usbmodem202334301`
   instead of `/dev/cu.usbmodem80F1B29FB0AC1`. The device list entry needs the
   new port.
4. Roles share `/prefs.json`: flashing a role over another keeps the old
   values (for example `faketec-6060` kept `kb-sender` and its radio until I set
   new values). This is the known behaviour from the KB, not a finding of the
   series.
5. Nodes with an unset RTC (2024) were ignored by the companion for contacts it
   had seen with a later timestamp (the sensor key was already known as "clock
   test repeater"). Setting the clock with `time <epoch>` fixed it. Not related
   to the series.

All boards ended on the base build (v1.18.0, `81dbae59`). The candidate is not
left on any board.

## Not verified

- ESP32-S3: only the stored settings were compared. Login, telemetry and
  forwarding between boards were not run on the candidate there.
- nRF52 functional checks ran on the candidate only. After the downgrade I
  compared settings but did not repeat login and telemetry.
- Region and flood scope: `region` and `region list allowed` were read
  (default `*` only). No named region or scope was configured, so the region
  store was not exercised across the upgrade.
- LilyGo T-Echo (`techo-46db`) was claimed but not flashed: it hosts the shared
  KiekR test room that other sessions use. RP2040, STM32, BLE and WiFi
  companions were not run.
- Only an artifact built from the base source, not the official release binary.
- The exact byte content of `/prefs.json` and the identity file was not dumped;
  the comparison is through the CLI and `infos`, which cover every field I
  listed, not every field of the file.
- No long-running soak, no over-the-air traffic beyond the short checks above.
