# 0002: Let the core read its tuning values from the radio settings

Status: ready for review. Behaviour change: none. Builds on 0001.

## Context

The core needs a handful of values that users set through the CLI or the
companion app: the duty cycle budget, the RX delay base, the flood and direct
retransmit delay factors, the interference threshold, CAD, the AGC reset interval
and the number of extra ACKs. It asked for each of them through a virtual getter
(`src/Dispatcher.h`, `src/Mesh.h`), and every role answered with a one-line
override that returned its own `_prefs` field:

| Hook | Repeater | Room server | Companion | Sensor |
|------|----------|-------------|-----------|--------|
| `getAirtimeBudgetFactor()` | same | same | same | same |
| `calcRxDelay()` | same | same | same | same |
| `getInterferenceThreshold()` | same | same | same | same |
| `getCADEnabled()` | same | same | same | same |
| `getAGCResetInterval()` | same | same | same | same |
| `getRetransmitDelay()` | same | same | same | **own formula** |
| `getDirectRetransmitDelay()` | same | same | same | **own formula** |
| `getExtraAckTransmitCount()` | same | same | same | **not overridden** |

31 overrides, 29 of them identical. A new core setting needed a new virtual and
an override in every role: making CAD a setting (`7c8e0924`) touched 15 files.

Since 0001 both `NodePrefs` classes share these fields through
`RadioPrefsFields`, so the core can read them from one place.

## Decision

1. `RadioPrefsFields` moves into the core as `src/RadioPrefsFields.h`, a header
   without dependencies. `helpers/CommonRadioPrefs.h` includes it.
2. `Dispatcher::useRadioPrefs(const RadioPrefsFields*)` attaches a node's
   settings. The default implementations of the eight hooks read them on every
   call, so a CLI change applies at once, exactly like the role overrides did.
   The formulas moved verbatim from the role overrides.
3. Without attached settings, every hook returns the built-in default it returned
   before. All hooks stay virtual.
4. Repeater, room server, sensor and companion attach their `_prefs` in their
   constructor and drop the 29 identical overrides.
5. The sensor keeps what differs: its two retransmit delay formulas (it draws
   from six discrete steps where the other roles draw from a continuous range),
   and it now states explicitly that it sends no extra ACKs. It never did,
   although the CLI accepts `multi.acks` there. Aligning the sensor with the other
   roles changes its behaviour on air and is left to a separate change.

## Alternatives considered

- **Pass the settings to the constructor of `Dispatcher` / `Mesh`.** Rejected:
  it changes the constructor every downstream subclass calls. A setter is
  additive.
- **Copy the values into the core on change.** Rejected: every code path that
  changes a setting (CLI, companion frames, UI, board key/value access) would
  have to notify the core, and a missed one would silently keep the old value.
  Reading through a pointer keeps the current behaviour of the overrides.
- **Remove the virtual hooks.** Rejected: downstream firmware overrides them.

## Compatibility

- Behaviour of all roles is unchanged (see verification).
- Downstream subclasses that override any of the hooks keep working. Code that
  does not call `useRadioPrefs()` gets the same defaults as before.
- `RadioPrefsFields` is now declared in `src/RadioPrefsFields.h`; it is still
  available through `helpers/CommonRadioPrefs.h`.

## Verification

### Host tests

A new native environment, `native_core`, builds the real `src/Dispatcher.cpp` and
`src/Mesh.cpp` for the host for the first time, against fakes for the radio,
clock, RNG, packet manager and tables, and link-time stand-ins for the four
`Identity` members `Mesh.cpp` references (they need the board crypto libraries;
the tests never call them). The unit test workflow runs it.

The step is two commits. The first only adds the tests; they pin what the core
returns without settings and pass on the code before the change:

- airtime budget factor 1.0, RX delay with base 10, retransmit delay from the raw
  packet length, no direct delay, no extra ACKs, interference threshold, CAD and
  AGC reset off.

The second commit adds tests that, with settings attached, the core returns
exactly what the v1.18.0 role overrides returned (their formulas are copied into
the test with the source lines), for four sets of settings, two kinds of packets
(with and without transport codes) and a range of scores and airtimes; that
settings changed at run time apply immediately; and that detaching restores the
defaults. The tests from the first commit pass unchanged.

Mutation tests, each of which must fail:

| Mutation | Result |
|----------|--------|
| airtime budget factor ignores the settings | 2 tests fail |
| RX delay without the "base <= 0 disables" check | 1 test fails |
| flood retransmit delay counts the raw packet length | 1 test fails |
| direct retransmit delay uses the flood factor | 1 test fails |
| extra ACKs ignore the settings | 2 tests fail |

### Builds

The same 18 environments as in 0001 were built from this step and from 0001,
with the same toolchain. All build. Compared with 0001, the step is between 136
bytes smaller and 464 bytes larger in flash, and RAM grows by 0 or 8 bytes. The
only new data member is the settings pointer in `Dispatcher`; the core now holds
both the built-in default and the settings-based formula of each hook, while
the identical overrides are gone from the roles. Together with 0001, every firmware is still
smaller than `dev`.

| Environment | Platform | Flash vs 0001 | RAM vs 0001 | Flash vs `dev` | RAM vs `dev` |
|---|---|---:|---:|---:|---:|
| `Heltec_v3_repeater` | ESP32-S3 | -100 | +0 | -1064 | +0 |
| `Heltec_v3_room_server` | ESP32-S3 | -76 | +0 | -1012 | +0 |
| `Heltec_v3_companion_radio_ble` | ESP32-S3 | -136 | +8 | -1040 | +8 |
| `Heltec_v3_companion_radio_wifi` | ESP32-S3 | -116 | +8 | -1048 | +8 |
| `Heltec_v3_sensor` | ESP32-S3 | +160 | +8 | -800 | +8 |
| `RAK_4631_repeater` | nRF52840 | +160 | +0 | -336 | +0 |
| `RAK_4631_room_server` | nRF52840 | +208 | +0 | -304 | +0 |
| `RAK_4631_companion_radio_ble` | nRF52840 | +192 | +8 | -288 | +8 |
| `RAK_4631_sensor` | nRF52840 | +464 | +8 | -64 | +8 |
| `PicoW_repeater` | RP2040 | -32 | +0 | -688 | +0 |
| `wio-e5-mini_repeater` | STM32WL | -32 | +0 | -584 | +0 |
| `wio-e5-mini_sensor` | STM32WL | +120 | +8 | -448 | +8 |
| `RAK_3112_repeater_bridge_espnow` | ESP32-S3 | -92 | +0 | -1048 | +0 |
| `waveshare_rp2040_lora_repeater_bridge_rs232` | RP2040 | -32 | +0 | -680 | +0 |
| `ProMicro_repeater` | nRF52840 | +160 | +0 | -352 | +0 |
| `ProMicro_room_server` | nRF52840 | +208 | +0 | -288 | +0 |
| `ProMicro_companion_radio_usb` | nRF52840 | +192 | +8 | -288 | +8 |
| `ProMicro_sensor` | nRF52840 | +464 | +8 | -64 | +8 |

The RX delay formula moved from the roles into `Dispatcher.cpp`, which is compiled
with different headers. In all 18 environments the role object files of 0001
call `powf` for it, and `Dispatcher.cpp` of this step calls `powf` too (checked
with `nm` on every object file), so the computed delays are bit for bit the same.
`Dispatcher.cpp` still calls `pow` (ESP32, RP2040, STM32) or `exp` (nRF52) for
the built-in default, as before.

### Hardware

The hooks only change values inside the running core, which no command shows.
For the hardware run, one identical diagnostic patch (a `diag tuning` CLI command
that prints what the running core returns for the duty cycle budget, the RX delay
at score 0.3 and 1000 ms airtime, the interference threshold, CAD, the AGC reset
interval and the extra ACKs; not part of any commit) was applied to `dev` and to
this step, and both were run on two LilyGo T-Echo boards (nRF52840, SX1262):
room server (server roles' `NodePrefs`) and USB companion (companion `NodePrefs`).

Sequence on each board: flash `dev`, change the settings through the CLI, read
the core values, reboot, read; flash this step, read; change the settings again
without a reboot, read; reboot, read; flash `dev`, read. On both boards:

- `dev` and this step returned the same core values for the same settings, in
  both directions: `af100=200 rx=991 thr=12 cad=1 agc=40000 acks=1` for the first
  set and `af100=150 rx=562 thr=7 cad=0 agc=24000 acks=0` for the second.
- These are the values the settings imply (for example `rx=991` is
  `(3.5^0.55 - 1) x 1000` for an RX delay base of 3.5).
- A change through the CLI showed up in the core immediately, without a reboot.

The retransmit delays are random and were covered by the host tests only. The
sensor role, the BLE and WiFi companions and the ESP32, RP2040 and STM32
platforms were built, not run on hardware.

## Follow-ups

- Decide whether the sensor should use the same retransmit delays and honour
  `multi.acks` like the other roles (behaviour change, own pull request).
- Remove the 2 second polling of CAD and the interference threshold in
  `Dispatcher::loop()` by pushing changes to the radio when they happen.
