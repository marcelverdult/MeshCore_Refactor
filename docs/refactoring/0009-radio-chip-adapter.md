# 0009: One RadioLib adapter per chip family, part one: the receive detection

Status: step 9a implemented. Behaviour change: none. Steps 9b to 9d are planned
below and need their own branches.

## Context

Finding F4 of the review: the chip classes in `src/helpers/radiolib` are near
copies. Measured on `integration` 2e456f74 with a script that cuts every method
out of `Custom<chip>.h` and `Custom<chip>Wrapper.h`, removes comments and white
space, replaces the chip name with a placeholder and folds the family prefixes of
the IRQ macros (`RADIOLIB_SX126X_`, `RADIOLIB_LR11X0_`, `RADIOLIB_LR2021_`) into
one. Equal hashes mean equal code after that normalization.

| Method | Groups of identical copies (lines) |
|--------|------------------------------------|
| chip `isReceiving()` | SX1262, SX1268, LLCC68, STM32WLx (42) / LR1110, LR1121 (41) / LR2021 (41) / SX1276 (6, a different method) |
| chip `startReceive()` | the six SX126x and LR11x0 chips / LR2021 |
| chip `setPreambleMillis()`, `setMaxPayloadMillis()` | all seven chips with a preamble IRQ |
| chip `begin()`, `getPacketLength()` | LR1110, LR1121 |
| wrapper `setParams()` | the six SX126x and LR11x0 chips / LR2021 / SX1276 |
| wrapper `isReceivingPacket()`, `getLastRSSI()`, `getLastSNR()` | all eight |
| wrapper `getCurrentRSSI()`, `getSpreadingFactor()` | SX126x family and SX1276 / LR1110, LR1121, LR2021 |
| wrapper `doResetAGC()` | SX126x family / LR11x0 family |
| wrapper `onSendFinished()` | LR1110, LR1121, LR2021 |

A plain diff of the three `isReceiving()` groups shows that they are one state
machine. They differ only in how they reach the chip: the getter
(`getIrqFlags()` or `getIrqStatus()`), the clearer (`clearIrqFlags()` or
`clearIrqState()`), the three IRQ bits, and the set of bits cleared when a
reception ends (the SX126x copies also clear the sync word bit). The history
shows the cost: upstream fixed the logic in SX1262 alone (#3000, #3062), then
copied it by hand into SX1268, LLCC68, STM32WLx and LR1110 (#3063, "Unify IRQ
timeout logic across all applicable radio types"), fixed LR1110 on its own
afterwards (#3076), and added LR2021 as one more copy (#3146).

The full chip classes are not identical. Among the SX126x copies, `std_init()` of
SX1268 and LLCC68 lacks the LDO regulator option, the Heltec register patch and two
debug lines that SX1262 has, and STM32WLx has no `std_init()` at all. Merging
those is a behaviour change for some boards and is kept out of this step.

## What changes (step 9a)

`src/helpers/radiolib/RxActivityTracker.h` holds the state machine. It sees three
booleans (preamble, header valid, header error) and the time, and returns one of
`IDLE`, `RECEIVING`, `CLEAR_PREAMBLE`, `CLEAR_ALL`. It also owns the two deadlines
and the setters the wrappers call. Each of the seven chip classes keeps its own
register access:

```cpp
bool isReceiving() {
  uint32_t irq = getIrqFlags();
  RxActivityTracker::Result r = _rxActivity.update(
      irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED,
      irq & RADIOLIB_SX126X_IRQ_HEADER_VALID,
      irq & RADIOLIB_SX126X_IRQ_HEADER_ERR,
      millis());
  if (r == RxActivityTracker::CLEAR_PREAMBLE) {
    clearIrqFlags(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED);
  } else if (r == RxActivityTracker::CLEAR_ALL) {
    clearIrqFlags(/* the chip's own set, unchanged */);
  }
  return r == RxActivityTracker::RECEIVING;
}
```

`setPreambleMillis()` and `setMaxPayloadMillis()` forward to the tracker. In
`src`, 340 lines go and 161 come in.

## What deliberately does not change

- Every quirk of the state machine is kept, including that a timer value of 0
  means "not started", so a preamble first seen at `millis() == 0` starts its
  timer one poll later. A test pins it.
- Each chip clears exactly the bits it cleared before. Whether the LR chips should
  also clear their sync word bit, as the SX126x chips do, is a question for a
  separate fix, not for a move.
- SX1276 has no preamble IRQ and reads the modem status instead. It does not use
  the tracker.
- `std_init()`, `begin()`, the wrappers and `RadioLibWrapper` are untouched.

The one observable difference is in `MESH_DEBUG` builds: both "Clearing ... IRQ"
lines are now printed before the SPI call that clears the bits; the preamble line
used to come after it. Release builds contain neither line.

## Alternatives considered

- **A CRTP mixin** (`RxActivity<Chip>` calling `Chip::getIrq()` and
  `Chip::clearIrq()` through the derived class). It would also absorb the getter
  and clearer, but needs a traits block per chip for the bit masks and the method
  names, ties the logic to RadioLib types, and cannot be tested without a fake
  chip class. The plain tracker does the same with less machinery and is tested
  with three booleans.
- **A template on the RadioLib base class** (`CustomSX126x<SX1262>` and so on)
  for the whole chip class. That is the goal of step 9b, but done now it would
  mix the move with the `std_init()` differences above, which are behaviour.
- **A common base class with virtual IRQ accessors.** Adds a vtable call per poll
  in the radio loop and a second inheritance path next to RadioLib's own; nothing
  gained over the tracker.
- **A `.cpp` file for the tracker.** A firmware links one chip class, so the code
  exists once either way. Header only lets the compiler inline it as before.

## Compatibility

Source compatible. The public methods of the chip classes keep their names and
signatures. The removed members `_preambleMillis`, `_maxPayloadMillis`,
`_activityAt`, `_headerSeen` were private. A downstream class derived from a chip
class that read them would no longer compile; none in the tree does.

## Verification

### Characterization tests

`test/test_radio_chips` builds the seven real chip classes on the host against a
stand-in for the RadioLib chip classes (`test/test_radio_chips/stubs/RadioLib.h`,
IRQ bit values copied from RadioLib 7.7.1). One typed suite runs 16 expectations
against every chip, 112 cases. It was committed first and passes unchanged
before and after the move:

```sh
pio test -e native_radio_chips
```

Covered: both deadlines including the deadline millisecond itself, the timer
restart after a clear, a quiet poll resetting the timers, the payload deadline
starting at the header and not the preamble, a header error with and without a
header, a header flag cleared elsewhere, the setters, the `millis()` wrap, the
zero quirk, the bits each chip clears, and the IRQ flags `startReceive()` asks
for.

All host suites pass on the step commit: `native`, `native_ofast`,
`native_server`, `native_sensor`, `native_repeater`, `native_repeater_full`,
`native_core`, `native_chat`, `native_identity_store`, `native_node_prefs`,
`native_radio_wrappers`, `native_region`, `native_kiss_modem`,
`native_radio_chips`, 379 cases.

### Mutation tests

Each mutation was applied to the step, the suite run, and the change reverted.

| # | Mutation | Failing cases |
|---|----------|---------------|
| M1 | payload deadline `>` becomes `>=` | 28, all seven chips |
| M2 | preamble deadline `>` becomes `>=` | 49, all seven chips |
| M3 | preamble timeout keeps its timer | 7, all seven chips |
| M4 | header error clears only the preamble bit | 14, all seven chips |
| M5 | a header flag cleared elsewhere goes unnoticed | 7, all seven chips |
| M6 | a quiet poll keeps the timers | 7, all seven chips |
| M7 | payload timer starts at the preamble | 7, all seven chips |
| M8 | timer value 0 no longer means "not started" | 48, all seven chips |
| M9 | SX1268 forgets the sync word bit in its clear | 3, SX1268 only |
| M10 | LR2021 swaps its preamble and header bits | 7, LR2021 only |
| M11 | default preamble deadline 66 becomes 67 ms | 28, all seven chips |

### Build matrix

The 18 environments of the standard matrix plus six for the chip classes the
step touches (SX1268, LR1110, LR1121, LR2021), each built twice with the same
toolchain: in a worktree on `integration` 2e456f74 and in one on the step commit ebb14737. All 48 builds succeed. Flash and
RAM as PlatformIO reports them, in bytes:

| Environment | Platform, chip | Flash before | Flash after | Flash | RAM |
|---|---|---|---|---|---|
| Heltec_v3_repeater | ESP32-S3, SX1262 | 1174193 | 1174225 | +32 | +0 |
| Heltec_v3_room_server | ESP32-S3, SX1262 | 1169481 | 1169513 | +32 | +0 |
| Heltec_v3_companion_radio_ble | ESP32-S3, SX1262 | 1666081 | 1666113 | +32 | +0 |
| Heltec_v3_companion_radio_wifi | ESP32-S3, SX1262 | 1102253 | 1102285 | +32 | +0 |
| Heltec_v3_sensor | ESP32-S3, SX1262 | 1180701 | 1180733 | +32 | +0 |
| RAK_4631_repeater | nRF52, SX1262 | 441384 | 441336 | -48 | +0 |
| RAK_4631_room_server | nRF52, SX1262 | 434552 | 434520 | -32 | +0 |
| RAK_4631_companion_radio_ble | nRF52, SX1262 | 462084 | 462036 | -48 | +0 |
| RAK_4631_sensor | nRF52, SX1262 | 438152 | 438104 | -48 | +0 |
| PicoW_repeater | RP2040, SX1262 | 492420 | 492388 | -32 | +0 |
| wio-e5-mini_repeater | STM32WL, STM32WLx | 203832 | 203824 | -8 | +0 |
| wio-e5-mini_sensor | STM32WL, STM32WLx | 203304 | 203296 | -8 | +0 |
| RAK_3112_repeater_bridge_espnow | ESP32-S3, SX1262 | 1175509 | 1175537 | +28 | +0 |
| waveshare_rp2040_lora_repeater_bridge_rs232 | RP2040, SX1262 | 240832 | 240800 | -32 | +0 |
| ProMicro_repeater | nRF52, SX1262 | 345824 | 345776 | -48 | +0 |
| ProMicro_room_server | nRF52, SX1262 | 338920 | 338888 | -32 | +0 |
| ProMicro_companion_radio_usb | nRF52, SX1262 | 356836 | 356804 | -32 | +0 |
| ProMicro_sensor | nRF52, SX1262 | 342476 | 342428 | -48 | +0 |
| Generic_E22_sx1268_repeater | ESP32, SX1268 | 1142769 | 1142797 | +28 | +0 |
| t1000e_repeater | nRF52, LR1110 | 294360 | 294280 | -80 | +0 |
| t1000e_companion_radio_ble | nRF52, LR1110 | 308668 | 308588 | -80 | +8 |
| Waveshare_S3_LR1121_repeater | ESP32-S3, LR1121 | 1084309 | 1084357 | +48 | +0 |
| meshnology_w12_repeater | ESP32, LR2021 | 1182665 | 1182697 | +32 | +8 |
| MeshTracker_X1_repeater | nRF52, LR2021 | 300752 | 300704 | -48 | +0 |

Flash moves by -80 to +48 bytes: the compiler inlines and orders the code a
little differently, smaller on the ARM builds, larger on the Xtensa ones.
RAM is unchanged except in two LR builds (+8). The reason is padding: the
tracker is a 16 byte object of its own, and the `bool` members the LR classes
declare after it no longer share its last word. Measured on the host with
`sizeof` against the stubs, the LR1110, LR1121 and LR2021 classes grow by 8 bytes
(16 to 24 above the RadioLib base), the SX126x classes not at all. A firmware
holds one chip object, so the cost is one padding word, accepted rather than
bending the tracker's layout to the members of its users. Why the other four LR
builds report no change was not examined; a likely cause is that the reported
size is rounded to the alignment of the data sections, which is unverified.

### Board test

Two boards that each run a USB companion image, marked with
`FIRMWARE_BUILD_DATE`: a Seeed T1000-E (LR1110) and a LilyGo T-Echo (SX1262),
on the same desk, 869.618 MHz, 62.5 kHz, SF8, CR8, as stored on the boards.
Each board in turn sent a zero hop advert (`CMD_SEND_SELF_ADVERT`) while the
other listened for `PUSH_CODE_LOG_RX_DATA` and `PUSH_CODE_NEW_ADVERT` carrying the
sender's public key. Three rounds per direction, first with the step
(`step9a-ebb14737`), then with `integration` (`step9a-before-2e456f74`):

| Image | Direction | Sent | Received with sender key | SNR / RSSI per round |
|-------|-----------|------|--------------------------|----------------------|
| step | LR1110 to SX1262 | 3/3 | 3/3 | 12.5/-5, 12.5/-5, 13.0/-5 |
| step | SX1262 to LR1110 | 3/3 | 3/3 | 14.0/-4, 14.0/-4, 14.0/-4 |
| integration | LR1110 to SX1262 | 3/3 | 3/3 | 12.5/-5, 12.25/-5, 12.75/-5 |
| integration | SX1262 to LR1110 | 3/3 | 3/3 | 14.25/-4, 14.0/-4, 14.0/-4 |

Settings, the 16 `get` values of `tools/refactoring/scripts/comp_gets.txt` and
the contact lists (350 and 68 keys) read the same at the start and at the end.
Both boards went back to the official 1.18.0 BLE companion release, and both
paired phones reconnected over BLE.

This shows that reception and transmission work on both families with the step.
It does not show the receive detection itself at work on air (a transmission held
back while a packet arrives); that part rests on the host tests.

### What was not verified on hardware

No hardware evidence for SX1268, LLCC68, STM32WLx, LR1121 and LR2021. They
build (matrix above, except LLCC68, which no variant in the tree uses and which
only the host suite compiles) and pass the host suite.

## Upstream collisions

Checked on 20 October 2026 against open pull requests on `meshcore-dev/MeshCore`
that touch `src/helpers/radiolib`:

- **#3512** (draft, LR2021 continuous mode race) edits the LR2021 copy of
  `isReceiving()`: it drops the header error branch and the early return after a
  cleared header flag. If it lands, LR2021 diverges from the other chips on
  purpose, and the tracker needs either an option for it or LR2021 keeps its own
  copy. Semantic collision with this step.
- **#3638** (dual radio profiles) adds about 380 lines to `CustomSX1262.h` and
  about 240 to `CustomLR1110.h` but leaves `isReceiving()` alone. Textual
  collision here, a heavy one for steps 9b to 9d.
- **#2131** (error aware SX126x receive hook) adds a helper to all four SX126x
  chip classes. Textual here, relevant to 9b.
- **#3496** (KISS modem gain control) changes `setRxBoostedGainMode()` in the LR
  chips and the reset helpers. Relevant to 9c and 9d.
- **#2682**, **#2999**, **#2944** add further copies of the four members in new
  or older chip classes; they would need the tracker instead.

## Cut plan for the rest of F4

Each step is its own branch with characterization first, as here.

- **9b: SX126x chip family.** One template `CustomSX126x<Base>` for SX1262,
  SX1268, LLCC68, with `std_init()` taken from SX1262. The LDO option, register
  patch and debug lines that SX1268 and LLCC68 lack are a behaviour change for
  those boards and need a decision first; until then the template keeps them
  per chip behind a flag. `CustomSTM32WLx` stays a class of its own (different
  module type, no `std_init()`), using the tracker as now.
- **9c: one wrapper per family.** `setParams()`, `isReceivingPacket()`,
  `getLastRSSI()`, `getLastSNR()`, `getCurrentRSSI()`, `getSpreadingFactor()`,
  `doResetAGC()` are identical per family. A wrapper template on the chip class
  replaces the casts in every method. Needs host tests through the wrappers,
  which `test/test_radio_chips/stubs` can carry.
- **9d: LR11x0 chip family.** `begin()` and `getPacketLength()` of LR1110 and
  LR1121 are identical; LR1121 adds its band dependent power handling. A template
  for the shared part, LR1121 deriving from it.

Flash cost expected for 9b to 9d: none or a few bytes, because a firmware still
links exactly one chip class; the gain is in the source, not in the image.
