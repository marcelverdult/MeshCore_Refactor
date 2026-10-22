# 0009: One RadioLib adapter per chip family, part one: the receive detection

Status: steps 9a and 9b implemented. Behaviour change: none. Steps 9c and 9d are
planned below and need their own branches.

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

## Step 9b: one template for SX1262, SX1268 and LLCC68

Status: implemented. Behaviour change: none.

### Measured inventory

`tools/refactoring/scripts/sx126x_inventory.py` cuts every member out of
`CustomSX1262.h`, `CustomSX1268.h` and `CustomLLCC68.h`, removes comments and
white space, replaces the chip name with a placeholder and compares the copies.
On `integration` 68e0fcdc:

| Member | Lines (normalized, SX1262) | Groups of identical copies |
|--------|----------------------------|----------------------------|
| includes | 4 | SX1262 / SX1268, LLCC68 |
| `_rxActivity`, constructor | 1 each | all three |
| `std_init()` | 75 | SX1262 / SX1268, LLCC68 |
| `startReceive()` | 3 | all three |
| `isReceiving()` | 14 | all three |
| `setPreambleMillis()`, `setMaxPayloadMillis()` | 1 each | all three |
| `getRxBoostedGainMode()` | 5 | all three |

SX1268 and LLCC68 are identical to each other in every member. Against SX1262
they differ in two places only, both in or for `std_init()`:

1. `#include "MeshCore.h"` (SX1262 only; it brings `MESH_DEBUG_PRINTLN`).
2. In `std_init()`, SX1262 alone has
   - the `SX126X_USE_REGULATOR_LDO` option, passed as the last argument of both
     `begin()` calls; SX1268 and LLCC68 call `begin()` with eight arguments, so
     RadioLib's default `useRegulatorLDO = false` applies. At the commit
     `platformio.ini` pins (6d8934836678d8894e3d556550475b37dce3e2b6, version
     7.7.1), the eight argument `begin()` of all three ends in
     `float tcxoVoltage = ..., bool useRegulatorLDO = false)`:
     `src/modules/SX126x/SX1262.h:58`, `src/modules/SX126x/SX1268.h:56`,
     `src/modules/LLCC68/LLCC68.h:52`;
   - the `SX126X_REGISTER_PATCH` block (read register 0x8B5, set bit 0, write
     it back);
   - three `MESH_DEBUG_PRINTLN` lines: regulator requested, the retry with the
     TCXO at 0 V, and status and device errors at the end.

Everything else in `std_init()` (TCXO voltage, coding rate, SPI pins per
platform, the retry on -706/-707, `setCRC(1)`, current limit, DIO2, boosted
gain, RF switch pins) is the same text in all three.

In the tree, no variant that builds SX1268 (`generic-e22`, `meshadventurer`,
`tenstar_c3`) sets `SX126X_USE_REGULATOR_LDO` or `SX126X_REGISTER_PATCH`, and no
variant uses LLCC68 at all. So taking SX1262's `std_init()` for all three would
change no in-tree release image; it would change debug builds (three more lines)
and any downstream board that sets one of the two macros with an SX1268 or
LLCC68. That is still a behaviour change and needs a decision of its own, so
9b keeps the differences.

### Template form

A new header `src/helpers/radiolib/CustomSX126x.h`:

```cpp
template <class Base, class Traits>
class CustomSX126x : public Base {
  RxActivityTracker _rxActivity;
public:
  explicit CustomSX126x(Module* mod) : Base(mod) { }
  bool std_init(SPIClass* spi = NULL);     // SX1262's body, see below
  int16_t startReceive() override;         // Base::startReceive(...)
  bool isReceiving();
  void setPreambleMillis(uint32_t);
  void setMaxPayloadMillis(uint32_t);
  bool getRxBoostedGainMode();
};
```

The three chip headers keep their class, now a thin subclass:

```cpp
struct CustomSX1262Traits {
  static constexpr bool regulatorOption = true;   // honour SX126X_USE_REGULATOR_LDO
  static constexpr bool registerPatch = true;     // honour SX126X_REGISTER_PATCH
  static constexpr bool initDebug = true;         // the three MESH_DEBUG lines
  static const char* name() { return "SX1262"; }
};
class CustomSX1262 : public CustomSX126x<SX1262, CustomSX1262Traits> {
public:
  CustomSX1262(Module* mod) : CustomSX126x(mod) { }
};
```

`CustomSX1268Traits` and `CustomLLCC68Traits` set all three flags to `false`. In
`std_init()` the flags are plain `if (Traits::flag)` on compile time constants
(C++11, no `if constexpr`: the firmware environments set no `-std` flag, so each
toolchain's default applies), so the compiler drops the dead branches:

- regulator: `constexpr bool useRegulatorLDO = Traits::regulatorOption && SX126X_USE_REGULATOR_LDO;`
  and `begin()` always gets it as the ninth argument. For SX1268 and LLCC68 that
  is `false`, the value RadioLib's default gave them before.
- register patch: the `#ifdef SX126X_REGISTER_PATCH` block stays, its body under
  `if (Traits::registerPatch)`.
- debug lines: under `if (Traits::initDebug)`, printing `Traits::name()` through
  `%s`, which prints the same text as the literal "SX1262" before.

`CustomSX126x.h` includes `MeshCore.h` for all three; for SX1268 and LLCC68 that
is one more include, no new symbol a firmware uses. The wrappers, `SX126xReset.h`
and `CustomSTM32WLx` are untouched (wrappers are step 9c; STM32WLx has a different
module type and no `std_init()`, and keeps its own class with the tracker).

Deciding later to give SX1268 and LLCC68 the LDO option, the patch or the debug
lines is then a one line change of a trait, reviewable as the behaviour change
it is.

### Alternatives considered for 9b

- **Take SX1262's `std_init()` as is for all three.** Smallest code, but the
  behaviour change described above. Rejected for this step, offered as a
  separate decision.
- **`using CustomSX1262 = CustomSX126x<SX1262, ...>;` instead of a subclass.**
  Breaks a downstream forward declaration `class CustomSX1262;`, and puts every
  SX1262 only addition (such as #3638's) into the shared template. The thin
  subclass keeps the class name a real class and gives chip only code a place.
- **One `bool` template parameter instead of a traits struct.** The three
  differences happen to coincide today, but one flag would make "give SX1268
  the LDO option" impossible without also giving it the patch. Three named
  flags say what each one guards.
- **A non template base class over `SX126x`.** `begin()` is not virtual in
  `SX1268` (it is in `SX1262`), so `std_init()` must call the concrete chip's
  `begin()`; that needs the chip type, i.e. a template.
- **A macro that stamps out the class body.** Unreadable in a debugger and in
  compiler errors; nothing gained over a template.

### Upstream collisions (checked 21 October 2026)

All open pull requests on `meshcore-dev/MeshCore` (499) were listed with their
files; these touch `CustomSX1262.h`, `CustomSX1268.h` or `CustomLLCC68.h`:

| PR | Touches | Collision with 9b |
|----|---------|-------------------|
| #3638 dual radio profiles | `CustomSX1262.h` +380 -2: new members (profile switching, `standby()`/`sleep()`/`setBandwidth()`... overrides, TCXO restore) and a new `startReceive()` body; `std_init()` and `isReceiving()` unchanged | Heavy textual, light semantic. Its members are SX1262 only and land in the thin `CustomSX1262` subclass unchanged; its `startReceive()` there overrides the template's. A rebase moves its hunks from the old class body into the subclass. No line of the shared template needs changing for it. |
| #2131 error aware receive hook | +6 -1 in each of the three | Textual; with the template it becomes one helper in `CustomSX126x` instead of three. |
| #2869 nRF52 radio init recovery | +27 -2 in each of the three `std_init()` | Textual; again one edit instead of three after 9b. |
| #2912 bump RadioLib to 7.7.1 | +14 to +17 in each `std_init()` (`ConfigLoRa_t`) | Textual; one edit instead of three. |
| #2933 noise floor | `CustomSX1262.h` +3 -1, the status debug line | Textual, one line in the template. |
| #3530 nRF54L platform | `CustomSX1262.h` +2, one more SPI branch | Textual, two lines in the template; would then also reach SX1268 and LLCC68 boards on that platform, which is that PR's concern. |
| #2624 DX-LR20 variant | `CustomLLCC68.h` +5 -5 | Textual; to be checked on rebase whether its change is LLCC68 only (then a trait or the subclass) or general. |

#2682, #2999, #2944 (named in the 9a section) do not touch these three files.
#3638 is the heavy one, but because 9b keeps `CustomSX1262` as a class and its
additions are SX1262 only, the collision is a hunk move, not a redesign. This
does not make 9b pointless; four of the seven PRs edit three copies identically
and would get smaller with it.

### What changed

`src/helpers/radiolib/CustomSX126x.h` holds the template as designed above.
`CustomSX1262.h`, `CustomSX1268.h` and `CustomLLCC68.h` are each a traits struct
and a subclass with the constructor. In `src`, 339 lines go and 188 come in.
Callers, the wrappers, `SX126xReset.h` and `CustomSTM32WLx.h` are unchanged.

### Characterization tests

Committed first (61d3a4f4), passing unchanged before and after the move:

- `test/test_radio_chips` gets a second typed suite over the three classes,
  built without any option macro: the `begin()` arguments (coding rate 5, sync
  word 0x12, preamble 16, TCXO 1.6 V, LDO off), `setCRC(1)` as the only further
  call, a passed SPI left alone without `P_LORA_SCLK`, the retry with TCXO 0 V on
  -707 and on -706, the error line after a failed retry and after another error
  (no retry), and `getRxBoostedGainMode()` reading register 0x8AC. 21 cases.
- `test/test_radio_chips_init` is a separate test program in the same
  environment (the classes are header only, so a build with other macro values
  must not be linked into the same binary). It sets `MESH_DEBUG`,
  `SX126X_USE_REGULATOR_LDO`, `SX126X_REGISTER_PATCH`, the TCXO voltage, `LORA_CR`,
  current limit, DIO2, boosted gain, RX/TX enable pins and the SPI pins, and pins
  the full ordered call log, the register 0x8B5 content and the debug output per
  chip: SX1262 passes LDO, patches 0x8B5 and prints its three lines; SX1268 and
  LLCC68 pass `false`, leave 0x8B5 alone and print nothing. 7 cases.

The stubs in `test/test_radio_chips/stubs/RadioLib.h` record the calls for this:
a `begin()` with RadioLib's parameters and defaults for the SX126x family, a
register map, and a call log; `getStatus()` and `getDeviceErrors()` return
distinct values so the status line is pinned with them.

All 17 host environments pass, 605 cases, on the test commit 61d3a4f4 and on the
code commit (577 before, 28 new):

```sh
pio test -e native_radio_chips
```

### Mutation tests

Each mutation was applied to the code commit, `native_radio_chips` run, and the
change reverted.

| # | Mutation | Failing cases |
|---|----------|---------------|
| M1 | SX1268 `regulatorOption` true | 2, SX1268 only |
| M2 | LLCC68 `registerPatch` true | 2, LLCC68 only |
| M3 | SX1268 `initDebug` true | 2, SX1268 only |
| M4 | SX1262 `regulatorOption` false | 3, SX1262 only |
| M5 | SX1262 `registerPatch` false | 2, SX1262 only |
| M6 | SX1262 `initDebug` false | 3, SX1262 only |
| M7 | no retry on -706 | 3, all three chips |
| M8 | retry keeps the TCXO voltage | 12, all three chips |
| M9 | `setCRC(2)` instead of `setCRC(1)` | 15, all three chips |
| M10 | register patch sets bit 1 instead of bit 0 | 2, SX1262 only |
| M11 | boosted gain check inverted | 3, all three chips |
| M12 | SPI pins passed in the wrong order | 3, all three chips |
| M13 | LLCC68 `regulatorOption` true | 2, LLCC68 only |
| M14 | SX1268 `registerPatch` true | 2, SX1268 only |

### Build matrix

Every firmware environment whose `RADIO_CLASS` is `CustomSX1262` (493) or
`CustomSX1268` (12), plus 22 controls that do not use the three classes: all 16
`CustomSTM32WLx` environments (STM32), `t1000e_repeater` (nRF52, LR1110),
`MeshTracker_X1_repeater` (nRF52, LR2021), `Waveshare_S3_LR1121_repeater` and
`meshnology_w12_repeater` (ESP32, LR1121 and LR2021), `Heltec_v2_repeater` and
`Heltec_v2_repeater_bridge_espnow` (ESP32, SX1276). No environment uses
`CustomLLCC68`. 527 environments, each built in the same worktree path on
`integration` 68e0fcdc and on the code commit 2228a5d6, same toolchain.

512 build on both sides. The same 15 fail on both sides, none in a radio header:
the `setTxBufferSize` error of the ESP32 `kiss_modem` builds (`Heltec_E213`,
`Heltec_E290`, `Heltec_T190`, `LilyGo_TBeam_1W`, `Station_G3_ESP32`,
`heltec_tracker_v2`), the ESP-NOW callback signature on ESP32-C3
(`Heltec_ct62`, `Tenstar_C3_sx1262`, `Tenstar_C3_sx1268`, each
`_repeater_bridge_espnow`), image too large (`Station_G2_companion_radio_ble`,
`Station_G3_ESP32_companion_radio_ble`), `D7` undeclared
(`ThinkNode_M5_companion_radio_serial`) and the `EnvironmentSensorManager` link
error (`ThinkNode_M8_repeater`, `_room_server`, `_kiss_modem`).

RAM is unchanged in all 512. Flash, after minus before, in bytes:

| Platform | Radio class | Environments | Flash difference (environments) |
|----------|-------------|--------------|---------------------------------|
| ESP32 | SX1262 | 252 | +0 (92), +4 (144), +8 (8), -2 (6), -16 (1), -40 (1) |
| ESP32 | SX1268 | 11 | +0 (2), +4 (9) |
| nRF52 | SX1262 | 202 | +0 (197), +16 (5) |
| RP2040 | SX1262 | 25 | +0 (25) |
| STM32 | STM32WLx (control) | 16 | +0 (16) |
| ESP32, nRF52 | LR1110, LR1121, LR2021, SX1276 (controls) | 6 | +0 (6) |

Measured for `Heltec_v3_repeater` (+4) with `nm` and `size -A` on both ELF files:
`.flash.text` grows by 4 bytes, and the only code symbol that changes size is
`_GLOBAL__sub_I_board`, the static initializer of the variant's globals, from 0x194
to 0x198 bytes, where the radio object is now built through one more base class
constructor. Every chip and wrapper method has the same size as before (compared
with the template name folded into `CustomSX1262`). The -2 builds are the ESP32-C3
and C6 ones.

#### Image comparison and where the bytes go

The image hashes of the matrix above differ in all 512, controls included,
because RadioLib puts the build time into every image: `Module.cpp:47` keeps
`static volatile const char rlb_info[] = RADIOLIB_INFO;`, and `BuildOpt.h:647`
ends `RADIOLIB_INFO` with `__DATE__ " " __TIME__`. Two builds of the same commit
one minute apart differ in exactly those three bytes. With `SOURCE_DATE_EPOCH`
set, which GCC uses for both macros, two builds of the same commit are byte
identical.

65 environments were then built again on both sides with
`SOURCE_DATE_EPOCH=1792540800`, keeping the ELF files: all 22 controls, all 21
environments with a flash difference other than +0 or +4, and 22 picked from the
+0 and +4 groups across the platforms and both chips.

- 23 images are byte identical: all 22 controls and `RAK_4631_repeater`.
- 42 differ. In each of them, `nm -S` of both ELF files was compared symbol by
  symbol, with the template name folded back into the class name. In 12 every
  symbol has the same size; only the order of the template members in flash
  changed (in `PicoW_repeater`, 23 of 2132 code symbols moved, for example
  `isReceiving()` from 0x10006214 to 0x100063b0). In the other 30 only these
  change size: `_GLOBAL__sub_I_board`, the static initializer of the variant
  globals that builds the radio object (22 environments, +1 to +8 bytes);
  `radio_init()`, into which `std_init()` is inlined (12, -2 to +12); and in a
  few environments functions the step does not touch (`sx126xResetAGC()` -4,
  `MicroNMEALocationProvider::waitFor()` -1, `::loop()` +4, `start_cpu0` -8,
  `esp_vfs_open` +4, `ESP32Board::getResetReasonString()` -2).

Three of them were disassembled and compared instruction by instruction:

- `_GLOBAL__sub_I_board` in `Heltec_v3_repeater` (+4): 156 instructions on both
  sides, the same stores in a different order, and four of them emitted in the
  wide instead of the narrow Xtensa encoding (`s32i.n` to `s32i`, `mov.n` to
  `or`), one byte each.
- `std_init()` of SX1268 in `Generic_E22_sx1268_repeater` (+4): the same
  instruction sequence; one `ill` padding word after a jump and one `mov.n`
  emitted as `or`, both alignment of a branch target.
- `MicroNMEALocationProvider::waitFor()` (-1): identical up to its final
  `retw.n`; the difference is the padding after it, which depends on where the
  function lands.

So the flash differences are scheduling, instruction width and alignment around
the object construction and the inlined `std_init()`. The -40 of
`RAK_3112_room_server` is not a code change of its own: its changed symbols add
up to -6 (`_GLOBAL__sub_I_board` +3, `start_cpu0` -8, `waitFor` -1), the rest
is section padding.

<details>
<summary>Flash and RAM per environment (512)</summary>

| Environment | Platform | Radio class | Flash before | Flash after | Flash | RAM |
|---|---|---|---|---|---|---|
| Ebyte_EoRa-S3_Repeater | ESP32 | SX1262 | 1108433 | 1108433 | +0 | +0 |
| Ebyte_EoRa-S3_companion_radio_ble | ESP32 | SX1262 | 1215629 | 1215629 | +0 | +0 |
| Ebyte_EoRa-S3_companion_radio_usb | ESP32 | SX1262 | 590197 | 590197 | +0 | +0 |
| Ebyte_EoRa-S3_kiss_modem | ESP32 | SX1262 | 507821 | 507821 | +0 | +0 |
| Ebyte_EoRa-S3_room_server | ESP32 | SX1262 | 1103745 | 1103745 | +0 | +0 |
| Ebyte_EoRa-S3_terminal_chat | ESP32 | SX1262 | 535737 | 535737 | +0 | +0 |
| GAT562_30S_Mesh_Kit_companion_radio_ble | nRF52 | SX1262 | 436052 | 436052 | +0 | +0 |
| GAT562_30S_Mesh_Kit_companion_radio_usb | nRF52 | SX1262 | 427028 | 427028 | +0 | +0 |
| GAT562_30S_Mesh_Kit_kiss_modem | nRF52 | SX1262 | 341280 | 341280 | +0 | +0 |
| GAT562_30S_Mesh_Kit_repeater | nRF52 | SX1262 | 411568 | 411568 | +0 | +0 |
| GAT562_30S_Mesh_Kit_room_server | nRF52 | SX1262 | 404656 | 404656 | +0 | +0 |
| GAT562_Mesh_EVB_Pro_kiss_modem | nRF52 | SX1262 | 310916 | 310916 | +0 | +0 |
| GAT562_Mesh_EVB_Pro_repeater | nRF52 | SX1262 | 365764 | 365764 | +0 | +0 |
| GAT562_Mesh_EVB_Pro_room_server | nRF52 | SX1262 | 359260 | 359260 | +0 | +0 |
| GAT562_Mesh_Tracker_Pro_companion_radio_ble | nRF52 | SX1262 | 403224 | 403224 | +0 | +0 |
| GAT562_Mesh_Tracker_Pro_companion_radio_usb | nRF52 | SX1262 | 394216 | 394216 | +0 | +0 |
| GAT562_Mesh_Tracker_Pro_kiss_modem | nRF52 | SX1262 | 310900 | 310900 | +0 | +0 |
| GAT562_Mesh_Tracker_Pro_repeater | nRF52 | SX1262 | 381276 | 381276 | +0 | +0 |
| GAT562_Mesh_Tracker_Pro_room_server | nRF52 | SX1262 | 374364 | 374364 | +0 | +0 |
| GAT562_Mesh_Watch13_companion_radio_ble | nRF52 | SX1262 | 399608 | 399608 | +0 | +0 |
| GAT562_Mesh_Watch13_kiss_modem | nRF52 | SX1262 | 304048 | 304048 | +0 | +0 |
| Generic_E22_sx1262_repeater | ESP32 | SX1262 | 1143573 | 1143573 | +0 | +0 |
| Generic_E22_sx1262_repeater_bridge_espnow | ESP32 | SX1262 | 1154277 | 1154277 | +0 | +0 |
| Generic_E22_sx1268_repeater | ESP32 | SX1268 | 1143357 | 1143361 | +4 | +0 |
| Generic_E22_sx1268_repeater_bridge_espnow | ESP32 | SX1268 | 1154053 | 1154057 | +4 | +0 |
| Heltec_E213_companion_radio_ble | ESP32 | SX1262 | 1272953 | 1272957 | +4 | +0 |
| Heltec_E213_companion_radio_usb | ESP32 | SX1262 | 668529 | 668533 | +4 | +0 |
| Heltec_E213_repeater | ESP32 | SX1262 | 1162229 | 1162233 | +4 | +0 |
| Heltec_E213_repeater_bridge_espnow | ESP32 | SX1262 | 1172673 | 1172677 | +4 | +0 |
| Heltec_E213_room_server | ESP32 | SX1262 | 1157473 | 1157477 | +4 | +0 |
| Heltec_E290_companion_ble | ESP32 | SX1262 | 1270781 | 1270789 | +8 | +0 |
| Heltec_E290_companion_usb | ESP32 | SX1262 | 666265 | 666269 | +4 | +0 |
| Heltec_E290_repeater | ESP32 | SX1262 | 1160181 | 1160185 | +4 | +0 |
| Heltec_E290_repeater_bridge_espnow | ESP32 | SX1262 | 1170609 | 1170613 | +4 | +0 |
| Heltec_E290_room_server | ESP32 | SX1262 | 1155397 | 1155401 | +4 | +0 |
| Heltec_T190_companion_radio_ble_ | ESP32 | SX1262 | 1250085 | 1250085 | +0 | +0 |
| Heltec_T190_companion_radio_usb_ | ESP32 | SX1262 | 646181 | 646181 | +0 | +0 |
| Heltec_T190_repeater_ | ESP32 | SX1262 | 1139397 | 1139397 | +0 | +0 |
| Heltec_T190_repeater_bridge_espnow_ | ESP32 | SX1262 | 1149833 | 1149833 | +0 | +0 |
| Heltec_T190_room_server_ | ESP32 | SX1262 | 1134653 | 1134653 | +0 | +0 |
| Heltec_WSL3_companion_radio_ble | ESP32 | SX1262 | 1647705 | 1647709 | +4 | +0 |
| Heltec_WSL3_companion_radio_usb | ESP32 | SX1262 | 636669 | 636673 | +4 | +0 |
| Heltec_WSL3_companion_radio_wifi | ESP32 | SX1262 | 1086181 | 1086185 | +4 | +0 |
| Heltec_WSL3_repeater | ESP32 | SX1262 | 1161333 | 1161337 | +4 | +0 |
| Heltec_WSL3_repeater_bridge_espnow | ESP32 | SX1262 | 1171761 | 1171765 | +4 | +0 |
| Heltec_WSL3_repeater_bridge_rs232 | ESP32 | SX1262 | 1163781 | 1163785 | +4 | +0 |
| Heltec_WSL3_room_server | ESP32 | SX1262 | 1156817 | 1156821 | +4 | +0 |
| Heltec_WSL3_sensor | ESP32 | SX1262 | 1168229 | 1168233 | +4 | +0 |
| Heltec_Wireless_Paper_companion_radio_ble | ESP32 | SX1262 | 1614793 | 1614793 | +0 | +0 |
| Heltec_Wireless_Paper_companion_radio_usb | ESP32 | SX1262 | 625189 | 625189 | +0 | +0 |
| Heltec_Wireless_Paper_kiss_modem | ESP32 | SX1262 | 517461 | 517461 | +0 | +0 |
| Heltec_Wireless_Paper_repeater | ESP32 | SX1262 | 1121873 | 1121873 | +0 | +0 |
| Heltec_Wireless_Paper_repeater_bridge_espnow | ESP32 | SX1262 | 1132325 | 1132325 | +0 | +0 |
| Heltec_Wireless_Paper_room_server | ESP32 | SX1262 | 1117125 | 1117125 | +0 | +0 |
| Heltec_Wireless_Tracker_companion_radio_ble | ESP32 | SX1262 | 1264633 | 1264637 | +4 | +0 |
| Heltec_Wireless_Tracker_companion_radio_usb | ESP32 | SX1262 | 661373 | 661377 | +4 | +0 |
| Heltec_Wireless_Tracker_kiss_modem | ESP32 | SX1262 | 526705 | 526709 | +4 | +0 |
| Heltec_Wireless_Tracker_repeater | ESP32 | SX1262 | 1157617 | 1157621 | +4 | +0 |
| Heltec_Wireless_Tracker_repeater_bridge_espnow | ESP32 | SX1262 | 1168105 | 1168109 | +4 | +0 |
| Heltec_Wireless_Tracker_room_server | ESP32 | SX1262 | 1152889 | 1152893 | +4 | +0 |
| Heltec_ct62_companion_radio_ble | ESP32 | SX1262 | 1275586 | 1275586 | +0 | +0 |
| Heltec_ct62_companion_radio_usb | ESP32 | SX1262 | 563516 | 563516 | +0 | +0 |
| Heltec_ct62_kiss_modem | ESP32 | SX1262 | 497028 | 497028 | +0 | +0 |
| Heltec_ct62_repeater | ESP32 | SX1262 | 1151874 | 1151874 | +0 | +0 |
| Heltec_ct62_sensor | ESP32 | SX1262 | 1157740 | 1157740 | +0 | +0 |
| Heltec_mesh_solar_companion_radio_ble | nRF52 | SX1262 | 374060 | 374060 | +0 | +0 |
| Heltec_mesh_solar_companion_radio_usb | nRF52 | SX1262 | 369060 | 369060 | +0 | +0 |
| Heltec_mesh_solar_kiss_modem | nRF52 | SX1262 | 309024 | 309024 | +0 | +0 |
| Heltec_mesh_solar_repeater | nRF52 | SX1262 | 365600 | 365600 | +0 | +0 |
| Heltec_mesh_solar_room_server | nRF52 | SX1262 | 359072 | 359072 | +0 | +0 |
| Heltec_t096_companion_radio_ble | nRF52 | SX1262 | 433556 | 433556 | +0 | +0 |
| Heltec_t096_companion_radio_usb | nRF52 | SX1262 | 426820 | 426820 | +0 | +0 |
| Heltec_t096_kiss_modem | nRF52 | SX1262 | 340776 | 340776 | +0 | +0 |
| Heltec_t096_repeater | nRF52 | SX1262 | 414464 | 414464 | +0 | +0 |
| Heltec_t096_repeater_bridge_rs232 | nRF52 | SX1262 | 416736 | 416736 | +0 | +0 |
| Heltec_t096_room_server | nRF52 | SX1262 | 407584 | 407584 | +0 | +0 |
| Heltec_t096_sensor | nRF52 | SX1262 | 411348 | 411348 | +0 | +0 |
| Heltec_t114_companion_radio_ble | nRF52 | SX1262 | 417624 | 417624 | +0 | +0 |
| Heltec_t114_companion_radio_usb | nRF52 | SX1262 | 410888 | 410888 | +0 | +0 |
| Heltec_t114_kiss_modem | nRF52 | SX1262 | 311268 | 311268 | +0 | +0 |
| Heltec_t114_repeater | nRF52 | SX1262 | 398460 | 398460 | +0 | +0 |
| Heltec_t114_repeater_bridge_rs232 | nRF52 | SX1262 | 368532 | 368532 | +0 | +0 |
| Heltec_t114_room_server | nRF52 | SX1262 | 391580 | 391580 | +0 | +0 |
| Heltec_t114_without_display_companion_radio_ble | nRF52 | SX1262 | 391872 | 391872 | +0 | +0 |
| Heltec_t114_without_display_companion_radio_usb | nRF52 | SX1262 | 385040 | 385040 | +0 | +0 |
| Heltec_t114_without_display_repeater | nRF52 | SX1262 | 366260 | 366260 | +0 | +0 |
| Heltec_t114_without_display_repeater_bridge_rs232 | nRF52 | SX1262 | 368532 | 368532 | +0 | +0 |
| Heltec_t114_without_display_room_server | nRF52 | SX1262 | 359740 | 359740 | +0 | +0 |
| Heltec_t1_companion_radio_ble | nRF52 | SX1262 | 435056 | 435056 | +0 | +0 |
| Heltec_t1_companion_radio_usb | nRF52 | SX1262 | 428352 | 428352 | +0 | +0 |
| Heltec_t1_kiss_modem | nRF52 | SX1262 | 339884 | 339884 | +0 | +0 |
| Heltec_t1_repeater | nRF52 | SX1262 | 413508 | 413508 | +0 | +0 |
| Heltec_t1_room_server | nRF52 | SX1262 | 406644 | 406644 | +0 | +0 |
| Heltec_tower_v2_companion_radio_ble | nRF52 | SX1262 | 334924 | 334924 | +0 | +0 |
| Heltec_tower_v2_companion_radio_usb | nRF52 | SX1262 | 328084 | 328084 | +0 | +0 |
| Heltec_tower_v2_kiss_modem | nRF52 | SX1262 | 251864 | 251864 | +0 | +0 |
| Heltec_tower_v2_repeater | nRF52 | SX1262 | 309528 | 309528 | +0 | +0 |
| Heltec_tower_v2_room_server | nRF52 | SX1262 | 303000 | 303000 | +0 | +0 |
| Heltec_v2_repeater | ESP32 | SX1276 | 1151913 | 1151913 | +0 | +0 |
| Heltec_v2_repeater_bridge_espnow | ESP32 | SX1276 | 1162757 | 1162757 | +0 | +0 |
| Heltec_v3_companion_radio_ble | ESP32 | SX1262 | 1670229 | 1670233 | +4 | +0 |
| Heltec_v3_companion_radio_usb | ESP32 | SX1262 | 656829 | 656833 | +4 | +0 |
| Heltec_v3_companion_radio_wifi | ESP32 | SX1262 | 1106381 | 1106385 | +4 | +0 |
| Heltec_v3_kiss_modem | ESP32 | SX1262 | 581249 | 581253 | +4 | +0 |
| Heltec_v3_repeater | ESP32 | SX1262 | 1175021 | 1175025 | +4 | +0 |
| Heltec_v3_repeater_bridge_espnow | ESP32 | SX1262 | 1185489 | 1185493 | +4 | +0 |
| Heltec_v3_repeater_bridge_rs232 | ESP32 | SX1262 | 1177489 | 1177493 | +4 | +0 |
| Heltec_v3_room_server | ESP32 | SX1262 | 1170169 | 1170173 | +4 | +0 |
| Heltec_v3_sensor | ESP32 | SX1262 | 1181557 | 1181561 | +4 | +0 |
| Heltec_v3_terminal_chat | ESP32 | SX1262 | 602377 | 602381 | +4 | +0 |
| KeepteenLT1_companion_radio_ble | nRF52 | SX1262 | 352656 | 352672 | +16 | +0 |
| KeepteenLT1_companion_radio_usb | nRF52 | SX1262 | 334760 | 334760 | +0 | +0 |
| KeepteenLT1_kiss_modem | nRF52 | SX1262 | 248868 | 248868 | +0 | +0 |
| KeepteenLT1_repeater | nRF52 | SX1262 | 322028 | 322028 | +0 | +0 |
| KeepteenLT1_room_server | nRF52 | SX1262 | 315108 | 315108 | +0 | +0 |
| LilyGo_T-Echo-Lite_companion_radio_ble | nRF52 | SX1262 | 356456 | 356456 | +0 | +0 |
| LilyGo_T-Echo-Lite_companion_radio_usb | nRF52 | SX1262 | 351552 | 351552 | +0 | +0 |
| LilyGo_T-Echo-Lite_kiss_modem | nRF52 | SX1262 | 276892 | 276892 | +0 | +0 |
| LilyGo_T-Echo-Lite_non_shell_companion_radio_ble | nRF52 | SX1262 | 315544 | 315544 | +0 | +0 |
| LilyGo_T-Echo-Lite_non_shell_companion_radio_usb | nRF52 | SX1262 | 310416 | 310416 | +0 | +0 |
| LilyGo_T-Echo-Lite_repeater | nRF52 | SX1262 | 335772 | 335772 | +0 | +0 |
| LilyGo_T-Echo-Lite_room_server | nRF52 | SX1262 | 328964 | 328964 | +0 | +0 |
| LilyGo_T-Echo_Card_companion_radio_ble | nRF52 | SX1262 | 349248 | 349248 | +0 | +0 |
| LilyGo_T-Echo_Card_companion_radio_usb | nRF52 | SX1262 | 343488 | 343488 | +0 | +0 |
| LilyGo_T-Echo_Card_kiss_modem | nRF52 | SX1262 | 261536 | 261536 | +0 | +0 |
| LilyGo_T-Echo_Card_repeater | nRF52 | SX1262 | 322368 | 322368 | +0 | +0 |
| LilyGo_T-Echo_Card_room_server | nRF52 | SX1262 | 314112 | 314112 | +0 | +0 |
| LilyGo_T-Echo_companion_radio_ble | nRF52 | SX1262 | 375680 | 375680 | +0 | +0 |
| LilyGo_T-Echo_companion_radio_usb | nRF52 | SX1262 | 368288 | 368288 | +0 | +0 |
| LilyGo_T-Echo_kiss_modem | nRF52 | SX1262 | 291496 | 291496 | +0 | +0 |
| LilyGo_T-Echo_repeater | nRF52 | SX1262 | 350096 | 350096 | +0 | +0 |
| LilyGo_T-Echo_room_server | nRF52 | SX1262 | 343288 | 343288 | +0 | +0 |
| LilyGo_T3S3_sx1262_companion_radio_ble | ESP32 | SX1262 | 1215677 | 1215677 | +0 | +0 |
| LilyGo_T3S3_sx1262_companion_radio_usb | ESP32 | SX1262 | 590245 | 590245 | +0 | +0 |
| LilyGo_T3S3_sx1262_kiss_modem | ESP32 | SX1262 | 507869 | 507869 | +0 | +0 |
| LilyGo_T3S3_sx1262_repeater | ESP32 | SX1262 | 1108533 | 1108533 | +0 | +0 |
| LilyGo_T3S3_sx1262_repeater_bridge_espnow | ESP32 | SX1262 | 1118825 | 1118825 | +0 | +0 |
| LilyGo_T3S3_sx1262_room_server | ESP32 | SX1262 | 1103801 | 1103801 | +0 | +0 |
| LilyGo_T3S3_sx1262_terminal_chat | ESP32 | SX1262 | 535785 | 535785 | +0 | +0 |
| LilyGo_TBeam_1W_companion_radio_ble | ESP32 | SX1262 | 1266201 | 1266205 | +4 | +0 |
| LilyGo_TBeam_1W_companion_radio_usb | ESP32 | SX1262 | 641217 | 641221 | +4 | +0 |
| LilyGo_TBeam_1W_companion_radio_wifi | ESP32 | SX1262 | 1087349 | 1087353 | +4 | +0 |
| LilyGo_TBeam_1W_repeater | ESP32 | SX1262 | 1154969 | 1154973 | +4 | +0 |
| LilyGo_TBeam_1W_repeater_bridge_espnow | ESP32 | SX1262 | 1165457 | 1165461 | +4 | +0 |
| LilyGo_TBeam_1W_room_server | ESP32 | SX1262 | 1150137 | 1150141 | +4 | +0 |
| LilyGo_TDeck_companion_radio_ble | ESP32 | SX1262 | 1251765 | 1251769 | +4 | +0 |
| LilyGo_TDeck_companion_radio_usb | ESP32 | SX1262 | 648409 | 648413 | +4 | +0 |
| LilyGo_TDeck_kiss_modem | ESP32 | SX1262 | 563321 | 563325 | +4 | +0 |
| LilyGo_TDeck_repeater | ESP32 | SX1262 | 1144729 | 1144733 | +4 | +0 |
| LilyGo_TETH_Elite_sx1262_companion_radio_ble | ESP32 | SX1262 | 1177209 | 1177209 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_companion_radio_usb | ESP32 | SX1262 | 552553 | 552553 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_kiss_modem | ESP32 | SX1262 | 489269 | 489269 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_repeater | ESP32 | SX1262 | 1076289 | 1076289 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_room_server | ESP32 | SX1262 | 1071653 | 1071653 | +0 | +0 |
| LilyGo_T_Impulse_Plus_companion_radio_ble | nRF52 | SX1262 | 405988 | 406004 | +16 | +0 |
| LilyGo_T_Impulse_Plus_kiss_modem | nRF52 | SX1262 | 311364 | 311364 | +0 | +0 |
| LilyGo_T_Impulse_Plus_repeater | nRF52 | SX1262 | 366508 | 366508 | +0 | +0 |
| LilyGo_Tlora_C6_companion_radio_ble_ | ESP32 | SX1262 | 1444915 | 1444915 | +0 | +0 |
| LilyGo_Tlora_C6_kiss_modem | ESP32 | SX1262 | 472809 | 472809 | +0 | +0 |
| LilyGo_Tlora_C6_repeater_ | ESP32 | SX1262 | 542449 | 542447 | -2 | +0 |
| LilyGo_Tlora_C6_room_server_ | ESP32 | SX1262 | 536981 | 536981 | +0 | +0 |
| M5Stack_Unit_C6L_companion_radio_ble | ESP32 | SX1262 | 1452475 | 1452475 | +0 | +0 |
| M5Stack_Unit_C6L_companion_radio_usb | ESP32 | SX1262 | 544593 | 544593 | +0 | +0 |
| M5Stack_Unit_C6L_kiss_modem | ESP32 | SX1262 | 480175 | 480175 | +0 | +0 |
| M5Stack_Unit_C6L_repeater | ESP32 | SX1262 | 552823 | 552821 | -2 | +0 |
| M5Stack_Unit_C6L_room_server | ESP32 | SX1262 | 547355 | 547355 | +0 | +0 |
| MeshTracker_X1_repeater | nRF52 | LR2021 | 301152 | 301152 | +0 | +0 |
| Mesh_pocket_companion_radio_ble | nRF52 | SX1262 | 345036 | 345036 | +0 | +0 |
| Mesh_pocket_companion_radio_usb | nRF52 | SX1262 | 338220 | 338220 | +0 | +0 |
| Mesh_pocket_kiss_modem | nRF52 | SX1262 | 268408 | 268408 | +0 | +0 |
| Mesh_pocket_repeater | nRF52 | SX1262 | 326840 | 326840 | +0 | +0 |
| Mesh_pocket_room_server | nRF52 | SX1262 | 320096 | 320096 | +0 | +0 |
| Meshadventurer_sx1262_companion_radio_ble | ESP32 | SX1262 | 1440789 | 1440797 | +8 | +0 |
| Meshadventurer_sx1262_companion_radio_usb | ESP32 | SX1262 | 601921 | 601925 | +4 | +0 |
| Meshadventurer_sx1262_kiss_modem | ESP32 | SX1262 | 529925 | 529933 | +8 | +0 |
| Meshadventurer_sx1262_repeater | ESP32 | SX1262 | 1162093 | 1162097 | +4 | +0 |
| Meshadventurer_sx1262_repeater_bridge_espnow | ESP32 | SX1262 | 1172877 | 1172881 | +4 | +0 |
| Meshadventurer_sx1262_room_server | ESP32 | SX1262 | 1157349 | 1157353 | +4 | +0 |
| Meshadventurer_sx1262_terminal_chat | ESP32 | SX1262 | 558453 | 558457 | +4 | +0 |
| Meshadventurer_sx1268_companion_radio_ble | ESP32 | SX1268 | 1440541 | 1440545 | +4 | +0 |
| Meshadventurer_sx1268_companion_radio_usb | ESP32 | SX1268 | 601697 | 601701 | +4 | +0 |
| Meshadventurer_sx1268_kiss_modem | ESP32 | SX1268 | 529697 | 529701 | +4 | +0 |
| Meshadventurer_sx1268_repeater | ESP32 | SX1268 | 1161865 | 1161869 | +4 | +0 |
| Meshadventurer_sx1268_repeater_bridge_espnow | ESP32 | SX1268 | 1172657 | 1172661 | +4 | +0 |
| Meshadventurer_sx1268_room_server | ESP32 | SX1268 | 1157129 | 1157133 | +4 | +0 |
| Meshadventurer_sx1268_terminal_chat | ESP32 | SX1268 | 558229 | 558233 | +4 | +0 |
| Meshimi_companion_radio_ble_ | ESP32 | SX1262 | 1444987 | 1444987 | +0 | +0 |
| Meshimi_repeater_ | ESP32 | SX1262 | 542537 | 542535 | -2 | +0 |
| Meshtiny_companion_radio_ble | nRF52 | SX1262 | 337552 | 337568 | +16 | +0 |
| Meshtiny_companion_radio_usb | nRF52 | SX1262 | 330152 | 330168 | +16 | +0 |
| Meshtiny_kiss_modem | nRF52 | SX1262 | 243268 | 243268 | +0 | +0 |
| Nano_G2_Ultra_companion_radio_ble | nRF52 | SX1262 | 345724 | 345724 | +0 | +0 |
| Nano_G2_Ultra_companion_radio_usb | nRF52 | SX1262 | 338316 | 338316 | +0 | +0 |
| Nano_G2_Ultra_kiss_modem | nRF52 | SX1262 | 249476 | 249476 | +0 | +0 |
| Nano_G2_Ultra_repeater | nRF52 | SX1262 | 319500 | 319500 | +0 | +0 |
| Nano_G2_Ultra_room_server | nRF52 | SX1262 | 312580 | 312580 | +0 | +0 |
| PicoW_companion_radio | RP2040 | SX1262 | 665800 | 665800 | +0 | +0 |
| PicoW_companion_radio_ble | RP2040 | SX1262 | 633604 | 633604 | +0 | +0 |
| PicoW_companion_radio_usb | RP2040 | SX1262 | 495284 | 495284 | +0 | +0 |
| PicoW_companion_radio_wifi | RP2040 | SX1262 | 528732 | 528732 | +0 | +0 |
| PicoW_kiss_modem | RP2040 | SX1262 | 439572 | 439572 | +0 | +0 |
| PicoW_repeater | RP2040 | SX1262 | 492668 | 492668 | +0 | +0 |
| PicoW_room_server | RP2040 | SX1262 | 488612 | 488612 | +0 | +0 |
| PicoW_terminal_chat | RP2040 | SX1262 | 469148 | 469148 | +0 | +0 |
| ProMicro_companion_radio_ble | nRF52 | SX1262 | 367860 | 367860 | +0 | +0 |
| ProMicro_companion_radio_usb | nRF52 | SX1262 | 358836 | 358836 | +0 | +0 |
| ProMicro_kiss_modem | nRF52 | SX1262 | 273400 | 273400 | +0 | +0 |
| ProMicro_repeater | nRF52 | SX1262 | 346240 | 346240 | +0 | +0 |
| ProMicro_repeater_bridge_rs232_serial1 | nRF52 | SX1262 | 342544 | 342544 | +0 | +0 |
| ProMicro_room_server | nRF52 | SX1262 | 339288 | 339288 | +0 | +0 |
| ProMicro_sensor | nRF52 | SX1262 | 342956 | 342956 | +0 | +0 |
| ProMicro_terminal_chat | nRF52 | SX1262 | 298384 | 298384 | +0 | +0 |
| R1Neo_companion_radio_ble | nRF52 | SX1262 | 385336 | 385336 | +0 | +0 |
| R1Neo_companion_radio_usb | nRF52 | SX1262 | 377928 | 377928 | +0 | +0 |
| R1Neo_kiss_modem | nRF52 | SX1262 | 312020 | 312020 | +0 | +0 |
| R1Neo_repeater | nRF52 | SX1262 | 368916 | 368916 | +0 | +0 |
| R1Neo_room_server | nRF52 | SX1262 | 361420 | 361420 | +0 | +0 |
| R1Neo_sensor | nRF52 | SX1262 | 365104 | 365104 | +0 | +0 |
| R1Neo_terminal_chat | nRF52 | SX1262 | 334572 | 334572 | +0 | +0 |
| RAK_11310_companion_radio_usb | RP2040 | SX1262 | 238796 | 238796 | +0 | +0 |
| RAK_11310_kiss_modem | RP2040 | SX1262 | 183084 | 183084 | +0 | +0 |
| RAK_11310_repeater | RP2040 | SX1262 | 236172 | 236172 | +0 | +0 |
| RAK_11310_repeater_bridge_rs232 | RP2040 | SX1262 | 240664 | 240664 | +0 | +0 |
| RAK_11310_room_server | RP2040 | SX1262 | 232124 | 232124 | +0 | +0 |
| RAK_11310_terminal_chat | RP2040 | SX1262 | 212668 | 212668 | +0 | +0 |
| RAK_3112_companion_radio_ble | ESP32 | SX1262 | 1653089 | 1653093 | +4 | +0 |
| RAK_3112_companion_radio_usb | ESP32 | SX1262 | 642157 | 642161 | +4 | +0 |
| RAK_3112_companion_radio_wifi | ESP32 | SX1262 | 1091457 | 1091461 | +4 | +0 |
| RAK_3112_kiss_modem | ESP32 | SX1262 | 586069 | 586073 | +4 | +0 |
| RAK_3112_repeater | ESP32 | SX1262 | 1165825 | 1165829 | +4 | +0 |
| RAK_3112_repeater_bridge_espnow | ESP32 | SX1262 | 1176329 | 1176333 | +4 | +0 |
| RAK_3112_repeater_bridge_rs232 | ESP32 | SX1262 | 1168325 | 1168329 | +4 | +0 |
| RAK_3112_room_server | ESP32 | SX1262 | 1161381 | 1161341 | -40 | +0 |
| RAK_3112_sensor | ESP32 | SX1262 | 1172769 | 1172777 | +8 | +0 |
| RAK_3112_terminal_chat | ESP32 | SX1262 | 607189 | 607193 | +4 | +0 |
| RAK_3401_companion_radio_ble | nRF52 | SX1262 | 432956 | 432956 | +0 | +0 |
| RAK_3401_companion_radio_usb | nRF52 | SX1262 | 423948 | 423948 | +0 | +0 |
| RAK_3401_kiss_modem | nRF52 | SX1262 | 340928 | 340928 | +0 | +0 |
| RAK_3401_repeater | nRF52 | SX1262 | 411040 | 411040 | +0 | +0 |
| RAK_3401_room_server | nRF52 | SX1262 | 404192 | 404192 | +0 | +0 |
| RAK_3401_sensor | nRF52 | SX1262 | 407888 | 407888 | +0 | +0 |
| RAK_3401_terminal_chat | nRF52 | SX1262 | 363424 | 363424 | +0 | +0 |
| RAK_3x72_companion_radio_usb | STM32 | STM32WLx | 197544 | 197544 | +0 | +0 |
| RAK_3x72_kiss_modem | STM32 | STM32WLx | 143164 | 143164 | +0 | +0 |
| RAK_3x72_repeater | STM32 | STM32WLx | 196776 | 196776 | +0 | +0 |
| RAK_3x72_sensor | STM32 | STM32WLx | 196376 | 196376 | +0 | +0 |
| RAK_4631_companion_radio_ble | nRF52 | SX1262 | 464212 | 464212 | +0 | +0 |
| RAK_4631_companion_radio_ethernet | nRF52 | SX1262 | 469752 | 469752 | +0 | +0 |
| RAK_4631_companion_radio_usb | nRF52 | SX1262 | 455188 | 455188 | +0 | +0 |
| RAK_4631_kiss_modem | nRF52 | SX1262 | 371976 | 371976 | +0 | +0 |
| RAK_4631_repeater | nRF52 | SX1262 | 441944 | 441944 | +0 | +0 |
| RAK_4631_repeater_bridge_rs232_serial1 | nRF52 | SX1262 | 408052 | 408052 | +0 | +0 |
| RAK_4631_repeater_bridge_rs232_serial2 | nRF52 | SX1262 | 408052 | 408052 | +0 | +0 |
| RAK_4631_repeater_ethernet | nRF52 | SX1262 | 459588 | 459588 | +0 | +0 |
| RAK_4631_room_server | nRF52 | SX1262 | 435096 | 435096 | +0 | +0 |
| RAK_4631_room_server_ethernet | nRF52 | SX1262 | 452732 | 452732 | +0 | +0 |
| RAK_4631_sensor | nRF52 | SX1262 | 438776 | 438776 | +0 | +0 |
| RAK_4631_terminal_chat | nRF52 | SX1262 | 394376 | 394376 | +0 | +0 |
| RAK_WisMesh_Tag_companion_radio_ble | nRF52 | SX1262 | 385640 | 385640 | +0 | +0 |
| RAK_WisMesh_Tag_companion_radio_usb | nRF52 | SX1262 | 378216 | 378216 | +0 | +0 |
| RAK_WisMesh_Tag_kiss_modem | nRF52 | SX1262 | 312180 | 312180 | +0 | +0 |
| RAK_WisMesh_Tag_repeater | nRF52 | SX1262 | 369076 | 369076 | +0 | +0 |
| RAK_WisMesh_Tag_room_server | nRF52 | SX1262 | 361548 | 361548 | +0 | +0 |
| RAK_WisMesh_Tag_sensor | nRF52 | SX1262 | 365232 | 365232 | +0 | +0 |
| SenseCap_Solar_companion_radio_ble | nRF52 | SX1262 | 374080 | 374080 | +0 | +0 |
| SenseCap_Solar_companion_radio_usb | nRF52 | SX1262 | 368944 | 368944 | +0 | +0 |
| SenseCap_Solar_kiss_modem | nRF52 | SX1262 | 311028 | 311028 | +0 | +0 |
| SenseCap_Solar_repeater | nRF52 | SX1262 | 366108 | 366108 | +0 | +0 |
| SenseCap_Solar_room_server | nRF52 | SX1262 | 359372 | 359372 | +0 | +0 |
| Station_G2_companion_radio_usb | ESP32 | SX1262 | 714561 | 714565 | +4 | +0 |
| Station_G2_companion_radio_wifi | ESP32 | SX1262 | 1139593 | 1139597 | +4 | +0 |
| Station_G2_kiss_modem | ESP32 | SX1262 | 598645 | 598649 | +4 | +0 |
| Station_G2_logging_repeater | ESP32 | SX1262 | 1207905 | 1207909 | +4 | +0 |
| Station_G2_logging_repeater_bridge_espnow | ESP32 | SX1262 | 1218361 | 1218365 | +4 | +0 |
| Station_G2_repeater | ESP32 | SX1262 | 1207881 | 1207885 | +4 | +0 |
| Station_G2_repeater_bridge_espnow | ESP32 | SX1262 | 1218337 | 1218341 | +4 | +0 |
| Station_G2_room_server | ESP32 | SX1262 | 1203065 | 1203069 | +4 | +0 |
| Station_G3_ESP32_companion_radio_usb | ESP32 | SX1262 | 716137 | 716141 | +4 | +0 |
| Station_G3_ESP32_companion_radio_wifi | ESP32 | SX1262 | 1141217 | 1141221 | +4 | +0 |
| Station_G3_ESP32_logging_repeater | ESP32 | SX1262 | 1209505 | 1209509 | +4 | +0 |
| Station_G3_ESP32_repeater | ESP32 | SX1262 | 1209465 | 1209469 | +4 | +0 |
| Station_G3_ESP32_room_server | ESP32 | SX1262 | 1204677 | 1204681 | +4 | +0 |
| T_Beam_S3_Supreme_SX1262_companion_radio_ble | ESP32 | SX1262 | 1262721 | 1262725 | +4 | +0 |
| T_Beam_S3_Supreme_SX1262_companion_radio_wifi | ESP32 | SX1262 | 1084233 | 1084241 | +8 | +0 |
| T_Beam_S3_Supreme_SX1262_kiss_modem | ESP32 | SX1262 | 570317 | 570321 | +4 | +0 |
| T_Beam_S3_Supreme_SX1262_repeater | ESP32 | SX1262 | 1155513 | 1155517 | +4 | +0 |
| T_Beam_S3_Supreme_SX1262_repeater_bridge_espnow | ESP32 | SX1262 | 1165997 | 1166001 | +4 | +0 |
| T_Beam_S3_Supreme_SX1262_room_server | ESP32 | SX1262 | 1150713 | 1150717 | +4 | +0 |
| Tbeam_SX1262_companion_radio_ble | ESP32 | SX1262 | 1483501 | 1483501 | +0 | +0 |
| Tbeam_SX1262_kiss_modem | ESP32 | SX1262 | 567437 | 567437 | +0 | +0 |
| Tbeam_SX1262_repeater | ESP32 | SX1262 | 1205213 | 1205213 | +0 | +0 |
| Tbeam_SX1262_repeater_bridge_espnow | ESP32 | SX1262 | 1216165 | 1216165 | +0 | +0 |
| Tbeam_SX1262_room_server | ESP32 | SX1262 | 1200065 | 1200065 | +0 | +0 |
| Tenstar_C3_sx1262_kiss_modem | ESP32 | SX1262 | 497480 | 497480 | +0 | +0 |
| Tenstar_C3_sx1262_repeater | ESP32 | SX1262 | 1152392 | 1152392 | +0 | +0 |
| Tenstar_C3_sx1268_kiss_modem | ESP32 | SX1268 | 497412 | 497412 | +0 | +0 |
| Tenstar_C3_sx1268_repeater | ESP32 | SX1268 | 1152294 | 1152294 | +0 | +0 |
| ThinkNode_M1_companion_radio_ble | nRF52 | SX1262 | 364572 | 364572 | +0 | +0 |
| ThinkNode_M1_companion_radio_usb | nRF52 | SX1262 | 353788 | 353788 | +0 | +0 |
| ThinkNode_M1_kiss_modem | nRF52 | SX1262 | 249444 | 249444 | +0 | +0 |
| ThinkNode_M1_repeater | nRF52 | SX1262 | 306180 | 306180 | +0 | +0 |
| ThinkNode_M1_room_server | nRF52 | SX1262 | 299636 | 299636 | +0 | +0 |
| ThinkNode_M2_Repeater | ESP32 | SX1262 | 1110177 | 1110181 | +4 | +0 |
| ThinkNode_M2_Repeater_bridge_espnow | ESP32 | SX1262 | 1120505 | 1120509 | +4 | +0 |
| ThinkNode_M2_companion_radio_ble | ESP32 | SX1262 | 1218593 | 1218597 | +4 | +0 |
| ThinkNode_M2_companion_radio_serial | ESP32 | SX1262 | 615829 | 615833 | +4 | +0 |
| ThinkNode_M2_companion_radio_usb | ESP32 | SX1262 | 615753 | 615757 | +4 | +0 |
| ThinkNode_M2_companion_radio_wifi | ESP32 | SX1262 | 1043577 | 1043581 | +4 | +0 |
| ThinkNode_M2_kiss_modem | ESP32 | SX1262 | 520613 | 520617 | +4 | +0 |
| ThinkNode_M2_room_server | ESP32 | SX1262 | 1105469 | 1105477 | +8 | +0 |
| ThinkNode_M2_terminal_chat | ESP32 | SX1262 | 548565 | 548569 | +4 | +0 |
| ThinkNode_M5_Repeater | ESP32 | SX1262 | 1127461 | 1127465 | +4 | +0 |
| ThinkNode_M5_Repeater_bridge_espnow | ESP32 | SX1262 | 1137965 | 1137969 | +4 | +0 |
| ThinkNode_M5_companion_radio_ble | ESP32 | SX1262 | 1236461 | 1236465 | +4 | +0 |
| ThinkNode_M5_companion_radio_usb | ESP32 | SX1262 | 633545 | 633549 | +4 | +0 |
| ThinkNode_M5_companion_radio_wifi | ESP32 | SX1262 | 1061209 | 1061213 | +4 | +0 |
| ThinkNode_M5_kiss_modem | ESP32 | SX1262 | 538409 | 538413 | +4 | +0 |
| ThinkNode_M5_room_server | ESP32 | SX1262 | 1122809 | 1122813 | +4 | +0 |
| ThinkNode_M5_terminal_chat | ESP32 | SX1262 | 566681 | 566685 | +4 | +0 |
| ThinkNode_M6_companion_radio_ble | nRF52 | SX1262 | 376464 | 376464 | +0 | +0 |
| ThinkNode_M6_companion_radio_usb | nRF52 | SX1262 | 368432 | 368432 | +0 | +0 |
| ThinkNode_M6_kiss_modem | nRF52 | SX1262 | 311444 | 311444 | +0 | +0 |
| ThinkNode_M6_repeater | nRF52 | SX1262 | 366212 | 366212 | +0 | +0 |
| ThinkNode_M6_room_server | nRF52 | SX1262 | 359676 | 359676 | +0 | +0 |
| ThinkNode_M8_companion_radio_ble | nRF52 | SX1262 | 370108 | 370124 | +16 | +0 |
| ThinkNode_M8_companion_radio_usb | nRF52 | SX1262 | 345896 | 345896 | +0 | +0 |
| Tiny_Relay_companion_radio_usb | STM32 | STM32WLx | 197552 | 197552 | +0 | +0 |
| Tiny_Relay_kiss_modem | STM32 | STM32WLx | 143164 | 143164 | +0 | +0 |
| Tiny_Relay_repeater | STM32 | STM32WLx | 196784 | 196784 | +0 | +0 |
| Tiny_Relay_sensor | STM32 | STM32WLx | 196376 | 196376 | +0 | +0 |
| WHY2025_badge_companion_radio_ble_ | ESP32 | SX1262 | 1444887 | 1444887 | +0 | +0 |
| WHY2025_badge_repeater_ | ESP32 | SX1262 | 542437 | 542435 | -2 | +0 |
| Waveshare_S3_LR1121_repeater | ESP32 | LR1121 | 1084869 | 1084869 | +0 | +0 |
| WioTrackerL1-1W_companion_radio_ble | nRF52 | SX1262 | 407772 | 407772 | +0 | +0 |
| WioTrackerL1-1W_companion_radio_usb | nRF52 | SX1262 | 398092 | 398092 | +0 | +0 |
| WioTrackerL1-1W_kiss_modem | nRF52 | SX1262 | 312084 | 312084 | +0 | +0 |
| WioTrackerL1-1W_repeater | nRF52 | SX1262 | 378716 | 378716 | +0 | +0 |
| WioTrackerL1-1W_room_server | nRF52 | SX1262 | 371812 | 371812 | +0 | +0 |
| WioTrackerL1Eink_companion_radio_ble | nRF52 | SX1262 | 424180 | 424180 | +0 | +0 |
| WioTrackerL1Eink_kiss_modem | nRF52 | SX1262 | 311236 | 311236 | +0 | +0 |
| WioTrackerL1_companion_radio_ble | nRF52 | SX1262 | 406716 | 406716 | +0 | +0 |
| WioTrackerL1_companion_radio_usb | nRF52 | SX1262 | 397036 | 397036 | +0 | +0 |
| WioTrackerL1_kiss_modem | nRF52 | SX1262 | 311140 | 311140 | +0 | +0 |
| WioTrackerL1_repeater | nRF52 | SX1262 | 377724 | 377724 | +0 | +0 |
| WioTrackerL1_room_server | nRF52 | SX1262 | 370804 | 370804 | +0 | +0 |
| Xiao_C3_companion_radio_ble | ESP32 | SX1262 | 1255482 | 1255482 | +0 | +0 |
| Xiao_C3_companion_radio_usb | ESP32 | SX1262 | 543422 | 543422 | +0 | +0 |
| Xiao_C3_companion_radio_wifi | ESP32 | SX1262 | 1010858 | 1010858 | +0 | +0 |
| Xiao_C3_kiss_modem | ESP32 | SX1262 | 473692 | 473692 | +0 | +0 |
| Xiao_C3_repeater | ESP32 | SX1262 | 1158048 | 1158046 | -2 | +0 |
| Xiao_C3_room_server | ESP32 | SX1262 | 1152902 | 1152902 | +0 | +0 |
| Xiao_C6_companion_radio_ble_ | ESP32 | SX1262 | 1444931 | 1444931 | +0 | +0 |
| Xiao_C6_kiss_modem | ESP32 | SX1262 | 472835 | 472835 | +0 | +0 |
| Xiao_C6_repeater_ | ESP32 | SX1262 | 542481 | 542479 | -2 | +0 |
| Xiao_S3_WIO_companion_radio_ble | ESP32 | SX1262 | 1642437 | 1642437 | +0 | +0 |
| Xiao_S3_WIO_companion_radio_serial | ESP32 | SX1262 | 671473 | 671473 | +0 | +0 |
| Xiao_S3_WIO_companion_radio_usb | ESP32 | SX1262 | 652653 | 652653 | +0 | +0 |
| Xiao_S3_WIO_companion_radio_wifi | ESP32 | SX1262 | 1054393 | 1054393 | +0 | +0 |
| Xiao_S3_WIO_kiss_modem | ESP32 | SX1262 | 549837 | 549837 | +0 | +0 |
| Xiao_S3_WIO_repeater | ESP32 | SX1262 | 1127969 | 1127969 | +0 | +0 |
| Xiao_S3_WIO_repeater_bridge_espnow | ESP32 | SX1262 | 1138525 | 1138525 | +0 | +0 |
| Xiao_S3_WIO_room_server | ESP32 | SX1262 | 1123353 | 1123353 | +0 | +0 |
| Xiao_S3_WIO_sensor | ESP32 | SX1262 | 1134745 | 1134745 | +0 | +0 |
| Xiao_S3_WIO_terminal_chat | ESP32 | SX1262 | 571045 | 571045 | +0 | +0 |
| Xiao_S3_companion_radio_ble | ESP32 | SX1262 | 1645153 | 1645153 | +0 | +0 |
| Xiao_S3_companion_radio_usb | ESP32 | SX1262 | 655381 | 655381 | +0 | +0 |
| Xiao_S3_kiss_modem | ESP32 | SX1262 | 559569 | 559569 | +0 | +0 |
| Xiao_S3_repeater | ESP32 | SX1262 | 1137605 | 1137605 | +0 | +0 |
| Xiao_S3_repeater_bridge_espnow | ESP32 | SX1262 | 1148121 | 1148121 | +0 | +0 |
| Xiao_S3_room_server | ESP32 | SX1262 | 1132949 | 1132949 | +0 | +0 |
| Xiao_S3_sensor | ESP32 | SX1262 | 1144397 | 1144397 | +0 | +0 |
| Xiao_nrf52_companion_radio_ble | nRF52 | SX1262 | 379608 | 379608 | +0 | +0 |
| Xiao_nrf52_companion_radio_usb | nRF52 | SX1262 | 373912 | 373912 | +0 | +0 |
| Xiao_nrf52_kiss_modem | nRF52 | SX1262 | 305840 | 305840 | +0 | +0 |
| Xiao_nrf52_repeater | nRF52 | SX1262 | 361800 | 361800 | +0 | +0 |
| Xiao_nrf52_room_server | nRF52 | SX1262 | 354264 | 354264 | +0 | +0 |
| Xiao_rp2040_companion_radio_usb | RP2040 | SX1262 | 238412 | 238412 | +0 | +0 |
| Xiao_rp2040_kiss_modem | RP2040 | SX1262 | 182708 | 182708 | +0 | +0 |
| Xiao_rp2040_repeater | RP2040 | SX1262 | 235796 | 235796 | +0 | +0 |
| Xiao_rp2040_room_server | RP2040 | SX1262 | 231740 | 231740 | +0 | +0 |
| Xiao_rp2040_terminal_chat | RP2040 | SX1262 | 212284 | 212284 | +0 | +0 |
| heltec_rc32_companion_radio_ble | ESP32 | SX1262 | 1280369 | 1280373 | +4 | +0 |
| heltec_rc32_companion_radio_usb | ESP32 | SX1262 | 675521 | 675525 | +4 | +0 |
| heltec_rc32_companion_radio_wifi | ESP32 | SX1262 | 1103413 | 1103417 | +4 | +0 |
| heltec_rc32_kiss_modem | ESP32 | SX1262 | 582193 | 582197 | +4 | +0 |
| heltec_rc32_repeater | ESP32 | SX1262 | 1169065 | 1169069 | +4 | +0 |
| heltec_rc32_repeater_bridge_espnow | ESP32 | SX1262 | 1179561 | 1179565 | +4 | +0 |
| heltec_rc32_room_server | ESP32 | SX1262 | 1164397 | 1164401 | +4 | +0 |
| heltec_rc32_sensor | ESP32 | SX1262 | 1175809 | 1175813 | +4 | +0 |
| heltec_rc32_terminal_chat | ESP32 | SX1262 | 603281 | 603285 | +4 | +0 |
| heltec_rc32_without_display_companion_radio_ble | ESP32 | SX1262 | 1275521 | 1275525 | +4 | +0 |
| heltec_rc32_without_display_companion_radio_usb | ESP32 | SX1262 | 670313 | 670317 | +4 | +0 |
| heltec_rc32_without_display_companion_radio_wifi | ESP32 | SX1262 | 1098265 | 1098269 | +4 | +0 |
| heltec_rc32_without_display_repeater | ESP32 | SX1262 | 1162165 | 1162169 | +4 | +0 |
| heltec_rc32_without_display_repeater_bridge_espnow | ESP32 | SX1262 | 1172541 | 1172545 | +4 | +0 |
| heltec_rc32_without_display_room_server | ESP32 | SX1262 | 1157581 | 1157585 | +4 | +0 |
| heltec_rc32_without_display_sensor | ESP32 | SX1262 | 1169053 | 1169053 | +0 | +0 |
| heltec_tracker_v2_companion_radio_ble | ESP32 | SX1262 | 1337821 | 1337825 | +4 | +0 |
| heltec_tracker_v2_companion_radio_usb | ESP32 | SX1262 | 711273 | 711277 | +4 | +0 |
| heltec_tracker_v2_companion_radio_wifi | ESP32 | SX1262 | 1157789 | 1157793 | +4 | +0 |
| heltec_tracker_v2_repeater | ESP32 | SX1262 | 1226253 | 1226257 | +4 | +0 |
| heltec_tracker_v2_repeater_bridge_espnow | ESP32 | SX1262 | 1236709 | 1236713 | +4 | +0 |
| heltec_tracker_v2_room_server | ESP32 | SX1262 | 1221501 | 1221505 | +4 | +0 |
| heltec_tracker_v2_sensor | ESP32 | SX1262 | 1232873 | 1232881 | +8 | +0 |
| heltec_tracker_v2_terminal_chat | ESP32 | SX1262 | 628189 | 628193 | +4 | +0 |
| heltec_v4_companion_radio_ble | ESP32 | SX1262 | 1668529 | 1668533 | +4 | +0 |
| heltec_v4_companion_radio_usb | ESP32 | SX1262 | 655233 | 655237 | +4 | +0 |
| heltec_v4_companion_radio_wifi | ESP32 | SX1262 | 1104737 | 1104741 | +4 | +0 |
| heltec_v4_expansionkit_repeater | ESP32 | SX1262 | 1173185 | 1173189 | +4 | +0 |
| heltec_v4_kiss_modem | ESP32 | SX1262 | 579641 | 579645 | +4 | +0 |
| heltec_v4_r8_companion_radio_ble | ESP32 | SX1262 | 1672049 | 1672053 | +4 | +0 |
| heltec_v4_r8_companion_radio_usb | ESP32 | SX1262 | 658709 | 658713 | +4 | +0 |
| heltec_v4_r8_companion_radio_wifi | ESP32 | SX1262 | 1108197 | 1108201 | +4 | +0 |
| heltec_v4_r8_kiss_modem | ESP32 | SX1262 | 583085 | 583089 | +4 | +0 |
| heltec_v4_r8_repeater | ESP32 | SX1262 | 1176625 | 1176629 | +4 | +0 |
| heltec_v4_r8_room_server | ESP32 | SX1262 | 1171909 | 1171913 | +4 | +0 |
| heltec_v4_r8_sensor | ESP32 | SX1262 | 1183241 | 1183245 | +4 | +0 |
| heltec_v4_r8_terminal_chat | ESP32 | SX1262 | 604185 | 604189 | +4 | +0 |
| heltec_v4_r8_tft_companion_radio_ble | ESP32 | SX1262 | 1289081 | 1289085 | +4 | +0 |
| heltec_v4_r8_tft_companion_radio_usb | ESP32 | SX1262 | 662557 | 662561 | +4 | +0 |
| heltec_v4_r8_tft_companion_radio_wifi | ESP32 | SX1262 | 1112049 | 1112053 | +4 | +0 |
| heltec_v4_r8_tft_kiss_modem | ESP32 | SX1262 | 583477 | 583481 | +4 | +0 |
| heltec_v4_r8_tft_repeater | ESP32 | SX1262 | 1178733 | 1178737 | +4 | +0 |
| heltec_v4_r8_tft_room_server | ESP32 | SX1262 | 1173917 | 1173921 | +4 | +0 |
| heltec_v4_r8_tft_sensor | ESP32 | SX1262 | 1185293 | 1185297 | +4 | +0 |
| heltec_v4_r8_tft_terminal_chat | ESP32 | SX1262 | 604573 | 604577 | +4 | +0 |
| heltec_v4_repeater | ESP32 | SX1262 | 1173133 | 1173137 | +4 | +0 |
| heltec_v4_repeater_bridge_espnow | ESP32 | SX1262 | 1183669 | 1183673 | +4 | +0 |
| heltec_v4_room_server | ESP32 | SX1262 | 1168417 | 1168421 | +4 | +0 |
| heltec_v4_sensor | ESP32 | SX1262 | 1179781 | 1179785 | +4 | +0 |
| heltec_v4_terminal_chat | ESP32 | SX1262 | 600733 | 600737 | +4 | +0 |
| heltec_v4_tft_companion_radio_ble | ESP32 | SX1262 | 1283401 | 1283405 | +4 | +0 |
| heltec_v4_tft_companion_radio_usb | ESP32 | SX1262 | 656917 | 656921 | +4 | +0 |
| heltec_v4_tft_companion_radio_wifi | ESP32 | SX1262 | 1106325 | 1106329 | +4 | +0 |
| heltec_v4_tft_repeater | ESP32 | SX1262 | 1174865 | 1174869 | +4 | +0 |
| heltec_v4_tft_repeater_bridge_espnow | ESP32 | SX1262 | 1185345 | 1185349 | +4 | +0 |
| heltec_v4_tft_room_server | ESP32 | SX1262 | 1170029 | 1170013 | -16 | +0 |
| heltec_v4_tft_sensor | ESP32 | SX1262 | 1181433 | 1181441 | +8 | +0 |
| heltec_v4_tft_terminal_chat | ESP32 | SX1262 | 600733 | 600737 | +4 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_companion_radio_ble | nRF52 | SX1262 | 397616 | 397616 | +0 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_companion_radio_usb | nRF52 | SX1262 | 390224 | 390224 | +0 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_rotated_companion_radio_ble | nRF52 | SX1262 | 397664 | 397664 | +0 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_rotated_companion_radio_usb | nRF52 | SX1262 | 390288 | 390288 | +0 | +0 |
| ikoka_handheld_nrf_e22_30dbm_repeater | nRF52 | SX1262 | 358296 | 358296 | +0 | +0 |
| ikoka_handheld_nrf_e22_30dbm_room_server | nRF52 | SX1262 | 351752 | 351752 | +0 | +0 |
| ikoka_handheld_nrf_kiss_modem | nRF52 | SX1262 | 304272 | 304272 | +0 | +0 |
| ikoka_nano_nrf_22dbm_companion_radio_ble | nRF52 | SX1262 | 386752 | 386752 | +0 | +0 |
| ikoka_nano_nrf_22dbm_companion_radio_usb | nRF52 | SX1262 | 379824 | 379824 | +0 | +0 |
| ikoka_nano_nrf_22dbm_kiss_modem | nRF52 | SX1262 | 305264 | 305264 | +0 | +0 |
| ikoka_nano_nrf_22dbm_repeater | nRF52 | SX1262 | 361176 | 361176 | +0 | +0 |
| ikoka_nano_nrf_22dbm_room_server | nRF52 | SX1262 | 353720 | 353720 | +0 | +0 |
| ikoka_nano_nrf_30dbm_companion_radio_ble | nRF52 | SX1262 | 386752 | 386752 | +0 | +0 |
| ikoka_nano_nrf_30dbm_companion_radio_usb | nRF52 | SX1262 | 379824 | 379824 | +0 | +0 |
| ikoka_nano_nrf_30dbm_kiss_modem | nRF52 | SX1262 | 305264 | 305264 | +0 | +0 |
| ikoka_nano_nrf_30dbm_repeater | nRF52 | SX1262 | 361176 | 361176 | +0 | +0 |
| ikoka_nano_nrf_30dbm_room_server | nRF52 | SX1262 | 353720 | 353720 | +0 | +0 |
| ikoka_nano_nrf_33dbm_companion_radio_ble | nRF52 | SX1262 | 386752 | 386752 | +0 | +0 |
| ikoka_nano_nrf_33dbm_companion_radio_usb | nRF52 | SX1262 | 379824 | 379824 | +0 | +0 |
| ikoka_nano_nrf_33dbm_kiss_modem | nRF52 | SX1262 | 305264 | 305264 | +0 | +0 |
| ikoka_nano_nrf_33dbm_repeater | nRF52 | SX1262 | 361176 | 361176 | +0 | +0 |
| ikoka_nano_nrf_33dbm_room_server | nRF52 | SX1262 | 353720 | 353720 | +0 | +0 |
| ikoka_stick_nrf_22dbm_companion_radio_ble | nRF52 | SX1262 | 397680 | 397680 | +0 | +0 |
| ikoka_stick_nrf_22dbm_companion_radio_usb | nRF52 | SX1262 | 390880 | 390880 | +0 | +0 |
| ikoka_stick_nrf_22dbm_kiss_modem | nRF52 | SX1262 | 318112 | 318112 | +0 | +0 |
| ikoka_stick_nrf_22dbm_repeater | nRF52 | SX1262 | 373768 | 373768 | +0 | +0 |
| ikoka_stick_nrf_22dbm_room_server | nRF52 | SX1262 | 366816 | 366816 | +0 | +0 |
| ikoka_stick_nrf_30dbm_companion_radio_ble | nRF52 | SX1262 | 397680 | 397680 | +0 | +0 |
| ikoka_stick_nrf_30dbm_companion_radio_usb | nRF52 | SX1262 | 390880 | 390880 | +0 | +0 |
| ikoka_stick_nrf_30dbm_kiss_modem | nRF52 | SX1262 | 318112 | 318112 | +0 | +0 |
| ikoka_stick_nrf_30dbm_repeater | nRF52 | SX1262 | 373768 | 373768 | +0 | +0 |
| ikoka_stick_nrf_30dbm_room_server | nRF52 | SX1262 | 366816 | 366816 | +0 | +0 |
| ikoka_stick_nrf_33dbm_companion_radio_ble | nRF52 | SX1262 | 397680 | 397680 | +0 | +0 |
| ikoka_stick_nrf_33dbm_companion_radio_usb | nRF52 | SX1262 | 390880 | 390880 | +0 | +0 |
| ikoka_stick_nrf_33dbm_kiss_modem | nRF52 | SX1262 | 318112 | 318112 | +0 | +0 |
| ikoka_stick_nrf_33dbm_repeater | nRF52 | SX1262 | 373768 | 373768 | +0 | +0 |
| ikoka_stick_nrf_33dbm_room_server | nRF52 | SX1262 | 366816 | 366816 | +0 | +0 |
| meshnology_w12_repeater | ESP32 | LR2021 | 1183449 | 1183449 | +0 | +0 |
| muzi_base_uno_companion_radio_ble | nRF52 | SX1262 | 330220 | 330220 | +0 | +0 |
| muzi_base_uno_companion_radio_usb | nRF52 | SX1262 | 325180 | 325180 | +0 | +0 |
| muzi_base_uno_repeater | nRF52 | SX1262 | 301236 | 301236 | +0 | +0 |
| muzi_base_uno_room_server | nRF52 | SX1262 | 294724 | 294724 | +0 | +0 |
| muzi_base_uno_superIO_companion_radio_ble | nRF52 | SX1262 | 349280 | 349280 | +0 | +0 |
| nibble_screen_connect_companion_radio_ble_ | ESP32 | SX1262 | 1210293 | 1210293 | +0 | +0 |
| nibble_screen_connect_companion_radio_usb_ | ESP32 | SX1262 | 584941 | 584941 | +0 | +0 |
| nibble_screen_connect_companion_radio_wifi_ | ESP32 | SX1262 | 1033905 | 1033905 | +0 | +0 |
| nibble_screen_connect_kiss_modem_ | ESP32 | SX1262 | 495625 | 495625 | +0 | +0 |
| nibble_screen_connect_repeater_ | ESP32 | SX1262 | 1103205 | 1103205 | +0 | +0 |
| nibble_screen_connect_repeater_bridge_espnow_ | ESP32 | SX1262 | 1113529 | 1113529 | +0 | +0 |
| nibble_screen_connect_room_server_ | ESP32 | SX1262 | 1098497 | 1098497 | +0 | +0 |
| nibble_screen_connect_terminal_chat_ | ESP32 | SX1262 | 523457 | 523457 | +0 | +0 |
| nibble_zero_connect_companion_radio_ble_ | ESP32 | SX1262 | 1210537 | 1210537 | +0 | +0 |
| nibble_zero_connect_companion_radio_usb_ | ESP32 | SX1262 | 585149 | 585149 | +0 | +0 |
| nibble_zero_connect_companion_radio_wifi_ | ESP32 | SX1262 | 1034133 | 1034133 | +0 | +0 |
| nibble_zero_connect_repeater_ | ESP32 | SX1262 | 1103309 | 1103309 | +0 | +0 |
| nibble_zero_connect_repeater_bridge_espnow_ | ESP32 | SX1262 | 1113613 | 1113613 | +0 | +0 |
| nibble_zero_connect_room_server_ | ESP32 | SX1262 | 1098589 | 1098589 | +0 | +0 |
| nibble_zero_connect_terminal_chat_ | ESP32 | SX1262 | 523537 | 523537 | +0 | +0 |
| t1000e_repeater | nRF52 | LR1110 | 294712 | 294712 | +0 | +0 |
| waveshare_rp2040_lora_companion_radio_usb | RP2040 | SX1262 | 239212 | 239212 | +0 | +0 |
| waveshare_rp2040_lora_kiss_modem | RP2040 | SX1262 | 183500 | 183500 | +0 | +0 |
| waveshare_rp2040_lora_repeater | RP2040 | SX1262 | 236596 | 236596 | +0 | +0 |
| waveshare_rp2040_lora_repeater_bridge_rs232 | RP2040 | SX1262 | 241080 | 241080 | +0 | +0 |
| waveshare_rp2040_lora_room_server | RP2040 | SX1262 | 232548 | 232548 | +0 | +0 |
| waveshare_rp2040_lora_terminal_chat | RP2040 | SX1262 | 213084 | 213084 | +0 | +0 |
| wio-e5-mini_companion_radio_usb | STM32 | STM32WLx | 209016 | 209016 | +0 | +0 |
| wio-e5-mini_kiss_modem | STM32 | STM32WLx | 151208 | 151208 | +0 | +0 |
| wio-e5-mini_repeater | STM32 | STM32WLx | 204136 | 204136 | +0 | +0 |
| wio-e5-mini_sensor | STM32 | STM32WLx | 203664 | 203664 | +0 | +0 |
| wio-e5-repeater_bridge_rs232 | STM32 | STM32WLx | 199064 | 199064 | +0 | +0 |
| wio-e5_companion_radio_usb | STM32 | STM32WLx | 197688 | 197688 | +0 | +0 |
| wio-e5_kiss_modem | STM32 | STM32WLx | 143248 | 143248 | +0 | +0 |
| wio-e5_repeater | STM32 | STM32WLx | 196848 | 196848 | +0 | +0 |

</details>

### What was not verified on hardware

No board test for 9b. The release images of SX1262 and SX1268 boards run the same
`std_init()` calls in the same order as before (host suites), and differ in flash
by the few bytes above; the debug build output is pinned on the host. LLCC68 has no
firmware environment and is covered by the host suites only.
