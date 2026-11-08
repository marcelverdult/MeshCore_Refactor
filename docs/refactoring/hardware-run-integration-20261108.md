# Hardware run of `integration` (ef708534), 8 November 2026

The run of 4 November (`hardware-run-integration-20261104.md`) covered `integration`
at bd69fa52. Since then two upstream syncs have been merged
(`sync-upstream-dev-20261104.md` and `sync-upstream-dev-20261107.md`), the
last one with upstream #3496 (KISS modem v2, boosted gain, FEM), #3774 (RP2350) and
#3099 (C6L KISS). This note repeats the upgrade and downgrade check on the same boards
for `ef708534`, and looks at what #3496 changes on SX126x boards at run time.

Result: on six board and role combinations, no stored setting changed
across upgrade and downgrade, and login, telemetry and status requests worked on both
builds. The boosted gain change of #3496 makes no difference that these boards can
show; the reasons and the limits are under "Upstream #3496".

## Setup

| | Base (v1.18.0) | Candidate |
|---|---|---|
| Source | `81dbae59`, built locally in a separate worktree | `integration` at `ef708534` |
| `ver` output | `v1.18.0 (Build: 15 Oct 2026)` | `v1.18.0 (Build: 15 Oct 2026)` (identical) |

Both trees were built with `SOURCE_DATE_EPOCH=1792540800`. The `ver` string is the same
for both builds again, so the identity of what ran rests on the file hashes below, on
the flasher logs (see "Flashing") and on the written sizes.

### Starting state

All six boards were still in the end state of the 4 November run (base build, the
`HWRUN-*` values). Phase 1 is a read-back of that state, taken before the first flash.
It is also the state each board was put back into at the end.

| Board | Port (checked against `ls /dev/cu.usb*`) | Identity | Role | Name | Public key |
|---|---|---|---|---|---|
| faketec-6060 | `/dev/cu.usbmodem202324301` | device list entry, name and public key as on 4 November | repeater | HWRUN-rep | B9A6EFF0... |
| faketec-0224 | `/dev/cu.usbmodem202324201` | device list entry, name and public key as on 4 November | room server | HWRUN-room | 82DD59EE... |
| faketec-6502 | `/dev/cu.usbmodem202324101` | device list entry, name and public key as on 4 November | sensor | HWRUN-sens | EE98752C... |
| faketec-80da | `/dev/cu.usbmodem20233201` | device list entry, name and public key as on 4 November | companion USB | HWRUN-comp | d2a8609b... |
| heltecv3-jork | `/dev/cu.usbserial-12` | MAC 48:ca:43:59:74:38 (esptool), public key E936D957... | sensor | HWRUN-v3sens | E936D957... |
| heltecv4-80f1 | `/dev/cu.usbmodem202334301` | MAC 80:f1:b2:9f:b0:ac (esptool and ioreg) | companion USB | HWRUN-v4comp | c1bcec9d... |

The device list entries for two of these boards are stale (see "Incidents").

### Artifacts (sha256, first 12 characters, size in bytes)

| Env | Base | Candidate |
|---|---|---|
| ProMicro_repeater (zip) | 3e9eb936dc2c, 346227 | 2b4adfbe97bc, 347626 |
| ProMicro_room_server (zip) | 686750151a94, 339259 | 9ab5d3d8ff27, 340675 |
| ProMicro_sensor (zip) | 48f17cbf977e, 342703 | f0ed778a9562, 344327 |
| ProMicro_companion_radio_usb (zip) | 57da5ceb7e6c, 359623 | 7eeec6cdb3f0, 364527 |
| Heltec_v3_sensor (bin) | b55ea51a6754, 1181264 | 7cb2a953ff9d, 1182400 |
| heltec_v4_companion_radio_usb (bin) | c3d89cd42f09, 652528 | 8914f9dca4c3, 659792 |

The base hashes differ from the base column of the 4 November note (for example
ProMicro_repeater 300a72521167 there). Both are local builds of `81dbae59`; why the
bytes differ was not investigated.

Built but not flashed: `Heltec_v3_repeater` and `heltec_v4_room_server`, the other two
rows of the 4 November table.

### Flashing

- nRF52 (FakeTec ProMicro): `adafruit-nrfutil dfu serial -pkg firmware.zip -b 115200 --touch 1200`,
  application only, no erase. The tool is the one in the PlatformIO package folder, run
  with Homebrew Python 3.14, because the interpreter that PlatformIO's own environment
  points to no longer exists. Every flash ended with `Device programmed.`
- ESP32-S3: `pio run -e <env> -t upload --upload-port <port>` with the same
  `SOURCE_DATE_EPOCH`, after `esptool.py -p <port> chip_id` had confirmed the MAC. No
  erase. esptool printed `Hash of data verified.` for each segment, and the application
  segment was exactly the artifact size (V4 candidate 659792, V4 base 652528, V3
  candidate 1182400, V3 base 1181264).
- Settings were read with a serial CLI helper (`get ...`) for repeater, room server and
  sensor, and with `meshcore-cli 1.6.9 ver infos` for the companions, as on 4 November.

## Result

Phases: 1 = starting state (base), 2 = candidate flashed over it, 3 = base flashed again
over that. "Identical" means `diff` of the complete read-back text of phase 1 against
phase 2 and against phase 3 printed nothing. A server read-back has 30 lines (`ver`, 27
`get` values, `region`, `region list allowed`), a companion read-back 25 lines.

| Board | Role / env | 1 to 2 (upgrade) | 1 to 3 (downgrade) |
|---|---|---|---|
| faketec-6060 | repeater, ProMicro_repeater | identical | identical |
| faketec-0224 | room server, ProMicro_room_server | identical | identical |
| faketec-6502 | sensor, ProMicro_sensor | identical | identical |
| faketec-80da | companion USB, ProMicro_companion_radio_usb | identical | identical |
| heltecv3-jork | sensor, Heltec_v3_sensor | identical | identical |
| heltecv4-80f1 | companion USB, heltec_v4_companion_radio_usb | identical | identical |

The values read were the ones listed in the 4 November note (radio `869.5250244,125,10,6`,
`tx 7`, `af 1.5`, `flood.max 40`, `flood.max.advert 20`, `agc.reset.interval 16`,
`guest.password hwguest`, `owner.info hwrun-owner`, `loop.detect minimal`, `repeat on`,
`radio.rxgain`, `cad off`, region `*` only, and so on), plus `public.key` and `role`.

Two things differ from the 4 November table, both because of the starting state:

- `flood.max.unscoped` was already `64` on both sensors in phase 1, because the one-time
  migration of step 0006 ran on 4 November and the value stayed after the downgrade. So the
  migration cannot show up again, and this run says nothing new about it.
- `radio.rxgain` reads `on` on the three ProMicro server boards and `off` on the V3
  sensor. That is the stored value from the starting state and it did not change in
  either direction.

The 6060 went through a third firmware between phases 2 and 3 (the KISS modem
described below). Its phase 3 read-back is still identical to phase 1.

### Behaviour between boards

Candidate firmware, all boards on `869.525/125/10/6`. First the four nRF52 boards with the
companion 80da, then the V4 as companion against the V3 sensor:

```
$ meshcore-cli -s <80da> contacts | grep HWRUN
HWRUN-sens   SENS ee98752c9107   41s 0 hop
HWRUN-room   ROOM 82dd59eef33a   41s 0 hop
HWRUN-rep    REP  b9a6eff00615   41s 0 hop
$ ... login HWRUN-room hwrunpw    -> Login success
$ ... login HWRUN-room wrongpw    -> Login failed : Error or Timeout waiting response
$ ... login HWRUN-rep hwrunpw     -> Login success
$ ... login HWRUN-sens hwrunpw    -> Login success
$ ... req_telemetry HWRUN-sens    -> voltage 3.94 (channel 1)
$ ... req_status HWRUN-rep        -> bat 4352, noise_floor -106, last_rssi -44, nb_recv 19, nb_sent 6
$ meshcore-cli -s <V4> ...
$ ... login HWRUN-v3sens hwrunpw  -> Login success
$ ... login HWRUN-v3sens wrongpw  -> Login failed : Error or Timeout waiting response
$ ... req_telemetry HWRUN-v3sens  -> voltage 4.19 (channel 1)
$ ... login HWRUN-rep hwrunpw     -> Login success
```

After the downgrade, with the base build on all six boards, the same login checks
(room, repeater, sensor, V3 sensor, one wrong password each for room and V3 sensor),
`req_telemetry` for both sensors and `req_status` for the repeater gave the same kinds of
answers (`Login success`, `Login failed` for the wrong password, telemetry and status
JSON). The console captures are not stored in the repository.

On 4 November `req_status` against the sensor returned an error on the candidate. It was
not repeated here.

## Upstream #3496 on SX126x boards

What the diff changes for SX1262 and SX1268 (read from `gh pr diff 3496` and from the
merged files):

1. `sx126xResetAGC()` now calls `setRxBoostedGainMode(rx_boost_gain)` every time. Before,
   the call sat behind `#ifdef SX126X_RX_BOOSTED_GAIN`. The value passed in is
   `getRxBoostedGainMode()`, which `CustomSX126x` reads from the chip register 0x8AC
   over SPI.
2. `supportsRxBoostedGain()` is new and returns true for SX1262 and SX1268. Only the KISS
   modem uses it, to set a capability bit.
3. KISS protocol v2: capability bits, `SetAgcResetInterval`, `SetFemState`,
   `SetRxBoostedGain` (0x20) and `GetRxBoostedGain` (0x21).
4. The `radio.fem.*` handlers of several Heltec boards write to the key store only if one
   is attached.

Which of these a board in this run could show:

| Change | Observable here? | How |
|---|---|---|
| Gain reapplied at AGC reset | The chip state, yes, but only through a KISS modem build. The CLI cannot show it: `get radio.rxgain` answers from the stored preference, not from the chip. | KISS `GetRxBoostedGain` reads the chip register. |
| Capability bit | yes | KISS `GetCapabilities` |
| AGC interval setting | yes | KISS `SetAgcResetInterval` and its reply |
| FEM gain over KISS | not on these units | see below |
| `_prefs` guard in the FEM handlers | not observable | no code path in these roles reaches it with a missing store |

None of the three SX126x environments in this run (`ProMicro_*`, `Heltec_v3_*`,
`heltec_v4_*`) defines `SX126X_RX_BOOSTED_GAIN`, so they are the case where the old code
skipped the call.

### Measurement

The candidate KISS modem was flashed on the 6060 (`ProMicro_kiss_modem`, nRF52 with
SX1262) and on the V4 (`heltec_v4_kiss_modem`, ESP32-S3 with SX1262). A probe client
sent the KISS frames of `docs/kiss_modem_protocol.md` over USB. The "before" value comes
from a control build: the candidate tree with `SX126xReset.h` put back to the
`#ifdef SX126X_RX_BOOSTED_GAIN` form (temporary worktree, not committed). Upstream's KISS
code before #3496 has no gain command, so no older build could be used for this.

```
Candidate, both boards (frames: C0 06 <sub> <data> C0)
GetVersion          -> 02 00          protocol version 2
GetCapabilities     -> 09 00 00 00    bit 0 (AGC interval) and bit 3 (chip boosted gain)
GetAgcResetInterval -> 20 00          default 32 s
GetFemState         -> 00 00
GetRxBoostedGain    -> 01             power-on state: boosted
SetRadio 869.525/125/10/6, SetAgcResetInterval 4 s
SetRxBoostedGain(1) -> 01, 14 s later GetRxBoostedGain -> 01
SetRxBoostedGain(0) -> 00, 14 s later GetRxBoostedGain -> 00
SetRxBoostedGain(1) -> 01, 14 s later GetRxBoostedGain -> 01
```

The control builds gave the same replies on both boards (ProMicro candidate zip
f092d9beb809, control zip 5c1c3f90c51a; V4 candidate bin 951b15c4b69a, control bin
cce6b90e91df). So with the old and the new reset code the chip keeps its gain setting
over the interval, and #3496 makes no difference on SX1262 that these tests can show.

Limits of this measurement:

- That AGC resets ran during the 14 second waits is inferred from the 4 second timer, not
  proven. A probe of `GetNoiseFloor` (which `resetAGC()` zeroes) found no zero value in 14
  seconds, which does not prove either way. NOT VERIFIED.
- Why the gain survives in the control is not established. The likely reason is that the
  warm sleep keeps the gain register, but I did not read a datasheet or the RadioLib source
  for it. This is a guess.
- The `SX126X_RX_BOOSTED_GAIN` build default path was not exercised, because none of these
  environments sets it.
- FEM: the V4 unit reports no controllable FEM. `GetFemState` returned `00 00`,
  `GetCapabilities` has no FEM bit, and `SetFemState` (RX and TX bits, and a reserved bit)
  answered with error 0x08 (Unsupported). The V4 board handler returns `Error:
  unsupported` when `isLnaCanControl()` is false, and upstream's handler has the same
  condition, so the result is consistent with upstream. I did not read that CLI reply on
  this unit and did not check which FEM part is fitted. So FEM
  RX gain over KISS is not observable on this unit.
- No LR11x0 or LR2021 board exists in the inventory. The #3496 changes for those chips
  (flag kept only after the chip accepted the call, per-path boost level) are covered by
  the host tests only.

## Incidents during the run

1. Device list entries are stale for two boards. `heltecv3-jork` is listed on
   `/dev/cu.usbserial-0001`. That port holds the other Heltec V3 (MAC
   34:b7:da:56:91:7c, the board called `heltecv3-dutch` in the list). The jork MAC
   48:ca:43:59:74:38 answered on `/dev/cu.usbserial-12`. `heltecv4-80f1` is listed as not
   connected on `/dev/cu.usbmodem80F1B29FB0AC1`, but the board is connected as
   `/dev/cu.usbmodem202334301` (esptool MAC and ioreg serial match). I claimed both by
   their real port. Nothing was flashed to the wrong board this time.
2. Two other boards were read, not written. To tell the three CP2102 boards apart I
   ran `esptool.py chip_id` on `/dev/cu.usbserial-0001` (the other V3) and on
   `/dev/cu.usbserial-18` (MAC 30:ed:a0:cb:4c:7c, not in the inventory). Opening the
   port resets such a board; nothing was written to either.
3. One transient on the V4 companion (candidate). About three minutes after the
   candidate flash and a successful read-back, two `meshcore-cli` calls got `No response from
   meshcore node` and a third did too. After `esptool.py ... chip_id` (which resets the
   board) it answered again. Afterwards three opens in a row and an open after about ten
   minutes idle all worked. I do not know the cause and could not reproduce it. I did not
   check whether the base build does the same. The calls failed right after the V3 flash
   on another USB port.
4. Runtime state changed. I set the clocks of all boards with `time <epoch>` (the
   repeaters, room server and sensors lose it on reset anyway) and sent flood adverts. The
   contact lists on the two companions changed as a result and from real mesh traffic
   (the V4 sees public repeaters). Contact lists were not compared and are not part of the
   "identical" claims above.

## End state

All six boards run the base build again (`81dbae59`, the hashes in the base column), and
the phase 3 read-back equals phase 1 for each. The candidate is not left on any board.
The device claims for the six boards were released after the last check.

## Not verified

- RP2350 (Pico 2 W) and the ESP32-C6 KISS modem (#3774, #3099): no such board is in
  the inventory. Not run.
- Heltec V3 repeater and V4 room server roles: built, not flashed. The V3 ran as
  sensor and the V4 as companion, the roles of the starting state.
- Boosted gain on SX126x: see the limits above. Whether AGC resets ran during the
  KISS test is not proven; the build default path and the FEM path were not exercised.
- LR11x0 and LR2021: no board.
- Region and flood scope: `region` and `region list allowed` were read (default `*`
  only); no named region was configured, so the region store was not exercised.
- Byte content of `/prefs.json` and the identity file was not dumped; the comparison
  goes through the CLI and `infos`, which cover the values listed, not every field.
- Bluetooth and WiFi companions, RP2040, STM32: not run. The T-Echo, T114 and T1000-E
  boards paired with the Android phones, the phones and the iPhone were not touched.
- Whether the V4 transient also happens on the base build.
- Soak: no long-running test, no over-the-air traffic beyond the short checks above.
- Why the base hashes differ from the 4 November note.
