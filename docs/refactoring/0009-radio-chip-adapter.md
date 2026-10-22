# 0009: One RadioLib adapter per chip family, part one: the receive detection

Status: steps 9a, 9b and 9c implemented. Behaviour change: none. Step 9d is
designed below.

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

## Step 9c: one wrapper template per chip family

Status: implemented. Behaviour change: none.

### Measured inventory

`tools/refactoring/scripts/wrapper_inventory.py` cuts every member out of the
eight `Custom<chip>Wrapper.h`, removes comments and white space, replaces the
chip name with a placeholder and compares the copies. On `integration`
e0d05fde (normalized lines per copy in brackets):

| Member | Groups of identical copies |
|--------|----------------------------|
| constructor | all eight (1) |
| `setParams()` | SX1262, SX1268, LLCC68, STM32WLx, LR1110, LR1121 (10) / LR2021 (11) / SX1276 (7) |
| `isReceivingPacket()`, `getLastRSSI()`, `getLastSNR()` | all eight (3, 1, 1) |
| `getCurrentRSSI()` | SX1262, SX1268, LLCC68, STM32WLx, SX1276 (3) / LR1110, LR1121, LR2021 (5) |
| `getSpreadingFactor()` | SX1262, SX1268, LLCC68, STM32WLx, SX1276 (1) / LR1110, LR1121, LR2021 (1) |
| `doResetAGC()` | SX1262, SX1268, LLCC68, STM32WLx (1) / LR1110, LR1121 (1) / LR2021, SX1276 absent |
| `setRxBoostedGainMode()` | SX1262, SX1268, LLCC68, LR1110, LR1121 (3) / LR2021 (6) / STM32WLx, SX1276 absent |
| `getRxBoostedGainMode()` | SX1262, SX1268, LLCC68, LR1110, LR1121, LR2021 (3) / STM32WLx, SX1276 absent |
| `onSendFinished()` | LR1110, LR1121, LR2021 (4) / the rest absent |
| `getEstAirtimeFor()` | LR1110, LR1121 (4) / the rest absent |
| `powerOff()` | SX1262 (3) / the rest absent |
| `configSideDetectors()`, `applySideDetectorConfig()`, `calcTsym()`, `_sideDet`, `_numSideDet` | LR2021 only |

Whole wrappers, identical after normalization: SX1268 and LLCC68; LR1110 and
LR1121. The setParams() differences: LR2021 calls `applySideDetectorConfig()`
between `updatePreamble()` and the deadlines, SX1276 sets no deadlines (its chip
class has no preamble IRQ, see 9a).

Every cast is `(Custom<chip> *)_radio` on a `PhysicalLayer*`. In RadioLib 7.7.1
every chip class reaches `PhysicalLayer` by single inheritance (`SX126x`,
`SX127x` and `LRxxxx` derive from it directly; `SX1262`, `SX1268`, `LLCC68`,
`STM32WLx`, `SX1276`, `LR1110`, `LR1121`, `LR2021` below those), and the
`Custom` classes add no base, so the C cast is a `static_cast` without pointer
adjustment. `doResetAGC()` casts to `SX126x*` or `LR11x0*` for the reset helper;
both are bases of the chip, so the implicit upcast replaces that cast.

No class in `src`, `variants` or `examples` derives from a wrapper; the variants
name them as `WRAPPER_CLASS` (86 SX1262, 15 SX1268, 7 LR1110, 4 STM32WLx, 4
SX1276, 2 LR1121, 2 LR2021 lines in the `.ini` files) and in `target.h`.

### Template form

Three templates, each in its own header next to the wrappers:

```cpp
// CustomChipWrapper.h: what all eight share
template <class Chip>
class CustomChipWrapper : public RadioLibWrapper {
protected:
  Chip* chip() const { return static_cast<Chip*>(_radio); }
  // first half of every setParams(): the four modem setters, then the preamble
  void setModemParams(float freq, float bw, uint8_t sf, uint8_t cr);
  // second half for the chips with the receive tracker of 9a
  void setRxDeadlines(float bw, uint8_t sf, uint8_t cr);
public:
  CustomChipWrapper(Chip& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override { return chip()->isReceiving(); }
  float getLastRSSI() const override { return chip()->getRSSI(); }
  float getLastSNR() const override { return chip()->getSNR(); }
};

// CustomSX126xWrapper.h: SX1262, SX1268, LLCC68, STM32WLx
template <class Chip>
class CustomSX126xWrapper : public CustomChipWrapper<Chip> {
public:
  void setParams(...) override;          // setModemParams + setRxDeadlines
  float getCurrentRSSI() override;       // chip()->getRSSI(false)
  uint8_t getSpreadingFactor() const override;   // chip()->spreadingFactor
  void doResetAGC() override;            // sx126xResetAGC(chip(), getRxBoostedGainMode())
};

// CustomLR11x0Wrapper.h: LR1110, LR1121
template <class Chip>
class CustomLR11x0Wrapper : public CustomChipWrapper<Chip> {
public:
  void setParams(...) override;          // setModemParams + setRxDeadlines
  float getCurrentRSSI() override;       // getRssiInst, -110 if it writes nothing
  uint8_t getSpreadingFactor() const override;   // chip()->getSpreadingFactor()
  void doResetAGC() override;            // lr11x0ResetAGC(chip(), chip()->getFreqMHz(), ...)
  uint32_t getEstAirtimeFor(int) override;       // at least 200 ms
  void onSendFinished() override;        // preamble length again
  bool setRxBoostedGainMode(bool) override;
  bool getRxBoostedGainMode() const override;
};
```

`setModemParams()` and `setRxDeadlines()` are plain (not virtual) member
functions of a class template, so they are only instantiated where a wrapper
calls them: SX1276 never calls `setRxDeadlines()` and needs no
`setPreambleMillis()`. Members of the dependent base are named through
`this->`. Everything is C++11 (no `if constexpr`), as in 9b.

The eight wrapper classes stay real classes with the same names, headers,
constructors and `USE_<chip>` defines:

| Wrapper | Derives from | Keeps in its own body |
|---------|--------------|------------------------|
| `CustomSX1262Wrapper` | `CustomSX126xWrapper<CustomSX1262>` | `powerOff()`, boosted gain set/get |
| `CustomSX1268Wrapper` | `CustomSX126xWrapper<CustomSX1268>` | boosted gain set/get |
| `CustomLLCC68Wrapper` | `CustomSX126xWrapper<CustomLLCC68>` | boosted gain set/get |
| `CustomSTM32WLxWrapper` | `CustomSX126xWrapper<CustomSTM32WLx>` | nothing |
| `CustomLR1110Wrapper` | `CustomLR11x0Wrapper<CustomLR1110>` | nothing |
| `CustomLR1121Wrapper` | `CustomLR11x0Wrapper<CustomLR1121>` | nothing |
| `CustomLR2021Wrapper` | `CustomChipWrapper<CustomLR2021>` | `setParams()` (with the side detectors), `getCurrentRSSI()`, `getSpreadingFactor()`, `onSendFinished()`, boosted gain, the side detector members |
| `CustomSX1276Wrapper` | `CustomChipWrapper<CustomSX1276>` | `setParams()` (no deadlines), `getCurrentRSSI()`, `getSpreadingFactor()` |

`LR2021::setParams()` becomes `setModemParams(); applySideDetectorConfig();
setRxDeadlines();`, `SX1276::setParams()` becomes `setModemParams()`: same calls,
same order. Every method keeps its virtual signature, so the vtables have the
same slots; only the class that defines a slot changes.

### Exceptions, kept as they are

- **STM32WLx has no boosted gain override.** Its wrapper inherits
  `RadioLibWrapper::getRxBoostedGainMode()`, which returns `false`, and passes
  that to `sx126xResetAGC()`. Moving the SX1262 boosted gain pair into the SX126x
  template would make `rxboost` start working on the 16 STM32WL environments:
  a behaviour change. So the pair stays in the three SX126x subclasses (three
  copies of six lines), and a host test pins that STM32WLx still reports `false`.
- **LR2021 shares with LR11x0** `getCurrentRSSI()`, `getSpreadingFactor()` and
  `onSendFinished()`, and **SX1276 shares with SX126x** `getCurrentRSSI()` and
  `getSpreadingFactor()`. Each of these is one to five lines; a third template
  level (an `LRxxxx` layer, an "RSSI by status" layer) for them would cost more
  reading than the copies. They stay in the subclass.
- **`powerOff()`** of SX1262 (`sleep(false)`) is SX1262 only and stays there.
  Whether SX1268 and LLCC68 should do the same is a behaviour question.
- **LR2021 boosted gain** goes through standby and a level, a different method.
- `RadioLibWrapper`, `SX126xReset.h`, `LR11x0Reset.h` and the chip classes are
  untouched.

### Alternatives considered for 9c

- **One template with a traits struct, as in 9b.** The family differences are
  different calls (`getRSSI(false)` against `getRssiInst()`, a member against a
  getter), not flags. Without `if constexpr`, both branches of `if
  (Traits::x)` must compile for every chip, and `getRssiInst()` does not exist on
  an SX126x. It would need tag dispatch per call, more machinery than two family
  templates.
- **Free functions overloaded on the RadioLib family base** (`rssiNow(SX126x&)`,
  `rssiNow(LR11x0&)`), picked by derived to base conversion. Works, but which
  overload a chip gets is implicit (LLCC68 and STM32WLx derive from SX1262 in
  RadioLib), and a wrong pick compiles. The family templates say it in the class
  head.
- **Override in the subclass what differs, keep everything in one template.**
  Virtual members of a class template are instantiated with the class, so a
  `setParams()` calling `setPreambleMillis()` would not compile for SX1276 even
  if the subclass overrides it.
- **`using CustomSX1262Wrapper = CustomSX126xWrapper<CustomSX1262>;`** Breaks a
  downstream forward declaration and leaves no place for SX1262 only members
  (`powerOff()`, #3638's profile hooks). Rejected as in 9b.
- **Storing a `Chip*` next to `_radio`.** Saves the cast at run time, which costs
  nothing anyway (no pointer adjustment), and grows every wrapper object by a
  pointer.
- **Moving the boosted gain pair into the SX126x template and overriding it back
  to `false` in STM32WLx.** Same behaviour, but reads as if STM32WLx turned a
  feature off on purpose. The copies say what is true today.

### Upstream collisions (checked 21 October 2026)

All open pull requests on `meshcore-dev/MeshCore` (499) were listed with their
files; these touch one of the eight wrapper headers:

| PR | Touches | Collision with 9c |
|----|---------|-------------------|
| #3638 dual radio profiles | all eight wrappers (+7 to +22): `setParams()` becomes `bool applyParams()` with a check per setter; LR1110 uses `setLoRaModulationParams()`, LR1121 passes a high bandwidth flag; profile hooks and `minimumAirtimeMillis()` in LR1110 and LR1121; `getLastRSSI()`/`getLastSNR()` removed in all | Heavy, semantic. Its `applyParams()` differs per chip in the LR11x0 family, so on rebase LR1110 and LR1121 override it in their subclass, or the template grows a hook. Its removal of `getLastRSSI()`/`getLastSNR()` becomes one deletion in `CustomChipWrapper`. |
| #3496 KISS gain control | `supportsRxBoostedGain()` in six wrappers, one line each | Textual; with 9c it lands in the three SX126x subclasses and once in `CustomLR11x0Wrapper`, plus LR2021. |
| #2933 noise floor | `verifyRxChipMode()` in four SX126x wrappers | Textual; one method in `CustomSX126xWrapper` instead of four. |
| #2670 repeated direct send | `verifyRxChipMode()` in four SX126x wrappers | As #2933. |
| #2131 error aware receive hook | `tryReadRawWithMeta()` in four SX126x wrappers, 10 lines each | Textual; one method in `CustomSX126xWrapper`. |
| #2842 RSSI and noise floor | removes `getLastRSSI()`/`getLastSNR()` from six wrappers | Textual; one deletion in `CustomChipWrapper`. |
| #1923, #2367 coding rate | `setCodingRate()` in six wrappers | Textual; one method in `CustomChipWrapper`. |
| #2916 dynamic CAD | `getCADDetPeakBase()`: SX126x formula in four, a table in LR1110 | Textual; one method per family template. |
| #2986 sleep fails | `CustomSX1262Wrapper::powerOff()` +1 | Textual, stays in the SX1262 subclass. |
| #2624 DX-LR20 | `CustomLLCC68Wrapper.h` +4 | Textual, LLCC68 subclass. |
| #2999, #2696, #2682, #2198 LR1121 boards | create `CustomLR1121Wrapper.h` as a new file | Already conflict with the file the tree has; 9c changes nothing for them. |
| #2944 LR2021 kit | creates `CustomLR2021Wrapper.h` | As above. |

#3638 is the one with a semantic collision. The other ten add the same member to
four to six wrappers by hand and would each shrink to one edit with 9c.

### Verification plan

- Characterization first, committed before the move and passing unchanged on
  both sides: a third test program `test/test_radio_chip_wrappers` in the
  `native_radio_chips` environment (no new environment, still 17) builds all
  eight wrappers over the real chip classes and the chip stubs, which gain a
  `PhysicalLayer` with a call log, `getRSSI(bool)`, `getRssiInst()`, a
  `getSpreadingFactor()` that returns a value other than the `spreadingFactor`
  member, the calls of both reset helpers, and the LR2021 side detector call.
  Pinned per wrapper: the ordered calls of `setParams()` and the deadlines that
  reach the chip, `isReceivingPacket()`, last and current RSSI including the
  -110 default, SNR, which spreading factor is read, the calls of `doResetAGC()`
  (with the boosted gain macros set, so the value passed is pinned, `false` for
  STM32WLx), boosted gain set/get, `onSendFinished()`, the 200 ms floor of
  `getEstAirtimeFor()`, `powerOff()`.
- Mutation tests on the code commit.
- Build matrix: every firmware environment whose `WRAPPER_CLASS` is one of the
  eight, on the start commit and the code commit in the same worktree, flash
  and RAM per environment; image comparison of a sample built with
  `SOURCE_DATE_EPOCH` set.
- All 17 host environments.

### What changed

`CustomChipWrapper.h`, `CustomSX126xWrapper.h` and `CustomLR11x0Wrapper.h` hold
the three templates as designed above. The eight wrapper headers keep their
class, now derived from them, with only the members listed under "Keeps in its
own body". No `(Custom<chip> *)_radio` cast is left in `src/helpers/radiolib`.
In `src`, 255 lines go and 153 come in. Callers, `RadioLibWrapper`, the reset
helpers and the chip classes are unchanged.

After the step, `wrapper_inventory.py` finds no member that is identical in two
wrapper bodies except the constructors and the boosted gain pair of SX1262,
SX1268 and LLCC68 (the exception above).

The design held with one finding: moving the boosted gain getter into the SX126x
template is not only a behaviour change for STM32WLx, it does not compile
(`CustomSTM32WLx` has no `getRxBoostedGainMode()`; mutation M10b below).

### Characterization tests

Committed first (fb0003c2), passing unchanged on that commit and on the code
commit 1234eb98: `test/test_radio_chip_wrappers`, a third test program in the
`native_radio_chips` environment (header only wrappers, and it sets the boosted
gain macros of the reset helpers, so it must not share a binary with
`test_radio_chips`). The environment now also links `RadioLibWrappers.cpp`;
`native` ignores the new program, as it does the other two. One typed suite of
10 tests runs against all eight wrappers, 80 cases (one skipped: SX1276 has no
receive deadlines):

| Test | Pins |
|------|------|
| `SetParamsCallsTheChipInOrder` | frequency, spreading factor, bandwidth, coding rate, preamble length 32/16; LR2021 then side detectors and standby |
| `IsReceivingPacketAsksTheChip` | IRQ flags, SX1276 the modem status |
| `SetParamsGivesTheChipItsDeadlines` | both deadlines of the new parameters reach the chip (preamble and payload, at and one millisecond after) |
| `SignalReadings` | last RSSI and SNR, instant RSSI `getRSSI(false)` or `getRssiInst()` with the -110 default |
| `SpreadingFactorIsTheChips` | the chip's value, not `LORA_SF` |
| `BoostedGain` | SX126x register 0x8AC, LR11x0 flag, LR2021 level 7/0 from standby, STM32WLx and SX1276 `false` without a chip call |
| `ResetAGC` | the calls of `sx126xResetAGC()` and `lr11x0ResetAGC()` with the boosted gain value passed (`false` for STM32WLx), LR2021 and SX1276 warm sleep only |
| `OnSendFinished` | LR chips set the preamble length again |
| `EstimatedAirtime` | the 200 ms floor of LR1110 and LR1121 |
| `PowerOff` | SX1262 `sleep(false)`, the others `sleep()` |

For this the chip stubs in `test/test_radio_chips/stubs/RadioLib.h` gain a
`PhysicalLayer` (as in `test/mocks`, with a call log shared with the chip calls),
a module with a HAL for the SX126x reset, `getRSSI(bool)`, `getRssiInst()`, the
modem setters, the calls of both reset helpers, `setSideDetector()` and the
SX127x classes. The 140 existing chip cases pass unchanged with them.

All 17 host environments pass, 696 cases (616 before, 80 new, one skipped), on
fb0003c2 and on 1234eb98.

### Mutation tests

Each mutation was applied to the code commit, `test_radio_chip_wrappers` run,
and the change reverted, one after the other by a script.

| # | Mutation | Failing cases |
|---|----------|---------------|
| M1 | modem setters: bandwidth before spreading factor | 8, all eight |
| M2 | preamble and payload deadline swapped | 7, all with deadlines |
| M3 | SX126x `setParams()` without deadlines | 4, SX126x family |
| M4 | SX126x instant RSSI reads the packet RSSI | 4, SX126x family |
| M5 | LR11x0 instant RSSI default -100 | 2, LR1110, LR1121 |
| M6 | LR11x0 air time floor 199 ms | 2, LR1110, LR1121 |
| M7 | LR11x0 `onSendFinished()` without the preamble length | 2, LR1110, LR1121 |
| M8 | SX126x reset passes `false` for boosted gain | 3, SX1262, SX1268, LLCC68 |
| M9 | LR11x0 reset passes `false` for boosted gain | 2, LR1110, LR1121 |
| M10 | STM32WLx gets the SX126x boosted gain setter | 1, STM32WLx |
| M10b | STM32WLx gets the getter too | does not compile |
| M11 | LR2021 `setParams()` without side detectors | 1, LR2021 |
| M12 | last RSSI reads the instant RSSI | 8, all eight |
| M13 | SX1276 without its spreading factor override | 1, SX1276 |
| M14 | SX1262 powers off with `sleep(true)` | 1, SX1262 |
| M15 | LR11x0 reset at 0 MHz instead of `getFreqMHz()` | 2, LR1110, LR1121 |
| M16 | SX1276 `setParams()` sets the preamble length twice | 1, SX1276 |

### Build matrix

Every firmware environment whose `WRAPPER_CLASS` is one of the eight wrappers:
612 (no environment uses `CustomLLCC68Wrapper`; it is covered by the host suite
only). Each built in the same worktree on `integration` e0d05fde and on the code
commit 1234eb98, same toolchain, with `SOURCE_DATE_EPOCH=1792540800` for every
build, so that equal sources give equal images (see 9b).

597 build on both sides. The same 15 fail on both sides, none in a radio header:
the `setTxBufferSize` error of the ESP32 `kiss_modem` builds (`Heltec_E213`,
`Heltec_E290`, `Heltec_T190`, `LilyGo_TBeam_1W`, `Station_G3_ESP32`,
`heltec_tracker_v2`), the ESP-NOW callback signature on ESP32-C3
(`Heltec_ct62`, `Tenstar_C3_sx1262`, `Tenstar_C3_sx1268`, each
`_repeater_bridge_espnow`), image too large (`Station_G2_companion_radio_ble`,
`Station_G3_ESP32_companion_radio_ble`), `D7` undeclared
(`ThinkNode_M5_companion_radio_serial`) and the `EnvironmentSensorManager` link
error (`ThinkNode_M8_repeater`, `_room_server`, `_kiss_modem`). The first error
line of each log is the same on both sides.

RAM is unchanged in all 597. Flash, after minus before, in bytes:

| Platform | Wrapper | Environments | Flash difference (environments) |
|----------|---------|--------------|---------------------------------|
| ESP32 | SX1262 | 252 | +0 (171), -4 (22), +4 (14), +12 (11), -52 (8), +8 (8), -56 (4), -16 (4), -44 (2), -20 (2), -8 (2), -2 (2), -60 (1), -48 (1) |
| RP2040 | SX1262 | 25 | +0 (24), +8 (1) |
| nRF52 | SX1262 | 202 | -48 (107), -64 (95) |
| ESP32 | SX1268 | 11 | +0 (8), -48 (1), +4 (1), +8 (1) |
| STM32 | STM32WLx | 16 | +0 (14), +8 (2) |
| ESP32 | SX1276 | 29 | +84 (6), +0 (5), +76 (3), +56 (1), +72 (1), +80 (1), +88 (1), +108 (1), +144 (1), +308 (1), +320 (1), +328 (1), +332 (1), +352 (1), +364 (1), +368 (1), +372 (1), +384 (1) |
| ESP32 | LR1110 | 13 | +0 (6), +4 (5), -12 (2) |
| nRF52 | LR1110 | 25 | +0 (20), -16 (5) |
| ESP32 | LR1121 | 5 | +0 (3), -20 (2) |
| nRF52 | LR1121 | 5 | +0 (4), -16 (1) |
| ESP32 | LR2021 | 9 | +0 (7), -64 (1), +72 (1) |
| nRF52 | LR2021 | 5 | +0 (3), -16 (2) |

#### Image comparison and where the bytes go

With `SOURCE_DATE_EPOCH` set for every build, no image of the 597 is byte
identical: the template members have other symbol names, so the linker places
them in another order even where nothing changes size. 20 environments were
built again on both sides keeping the ELF files, at least one per wrapper and
platform and the extremes of each group: `RAK_4631_repeater`,
`GAT562_30S_Mesh_Kit_repeater` (-64), `Heltec_v3_repeater`,
`heltec_v4_tft_companion_radio_usb` (+12), `Ebyte_EoRa-S3_Repeater`,
`waveshare_rp2040_lora_repeater`, `Generic_E22_sx1268_repeater`,
`Meshadventurer_sx1268_terminal_chat` (-48), `wio-e5-mini_repeater`,
`Tiny_Relay_companion_radio_usb` (+8), `Heltec_v2_repeater`,
`Heltec_v2_kiss_modem`, `LilyGo_TLora_V2_1_1_6_terminal_chat` (+384),
`t1000e_repeater`, `ThinkNode_M9_companion_radio_wifi_` (+4),
`Waveshare_S3_LR1121_repeater`, `Waveshare_S3_LR1121_companion_radio_ble` (-20),
`meshnology_w12_repeater`, `meshnology_w12_kiss_modem` (+72),
`MeshTracker_X1_repeater`. All 20 reproduce the image hash of the matrix on both
sides. `nm -S -C` of both ELF files was compared symbol by symbol, with the
template names folded back into the wrapper name and the numbers of compiler
clones (`$isra$N`) folded:

- **9 have no symbol of another size** (`Heltec_v3_repeater`,
  `Ebyte_EoRa-S3_Repeater`, `waveshare_rp2040_lora_repeater`,
  `Generic_E22_sx1268_repeater`, `wio-e5-mini_repeater`,
  `Tiny_Relay_companion_radio_usb`, `Heltec_v2_kiss_modem`,
  `Waveshare_S3_LR1121_repeater`, `meshnology_w12_repeater`; the +8 of
  `Tiny_Relay` is section padding). Only the order changed.
- **nRF52 SX1262 (-48, -64)**: `doResetAGC()` shrinks from 208 to 154 bytes,
  and `ServerMesh::begin()` and `checkPendingRadioParams()` by 4 each.
  Disassembled in `RAK_4631_repeater`: before, GCC speculatively devirtualized
  `getRxBoostedGainMode()` (compare the vtable slot at offset 96 with
  `CustomSX1262Wrapper::getRxBoostedGainMode`, then inline the read of register
  0x8AC); after, it is a plain virtual call through the same slot (`ldr r3,
  [r2, #96]; blx r3`), which reaches the same override. The calls that follow
  (`sleep`, `standby`, `SPIwriteStream`, `calibrateImage`, `setDio2AsRfSwitch`,
  `setRxBoostedGainMode`) are the same in the same order. The template does not
  know the SX1262 override, so the guess is no longer offered.
- **ESP32 SX1262, SX1268, LR chips (-64 to +12)**: only callers of
  `setParams()` and the companion command handlers change by -6 to +4 bytes each
  (`MyMesh::begin()`, `cmdSetRadioParams()`, `setup()` of the terminal chat
  -76, `onSetRadio()` of the LR2021 KISS modem +94, where `setParams()` is now
  inlined), plus padding. No wrapper method changes size.
- **ESP32 SX1276 at `-Os` (+56 to +144, 19 environments)**: `setModemParams()`
  is emitted out of line (78 bytes), and the code that used to inline the four
  setters calls it.
- **LilyGo T-Lora V2.1 1.6 (+308 to +384, 9 environments)**: these build
  without optimization. The variant's `build_unflags = -Os` also removes the
  `-Os` its own `build_flags` add; the DWARF producer string of the ELF shows no
  `-O` option, so GCC's default `-O0` applies. Nothing is inlined there, so each
  template level is a real function: `chip()` (17 bytes), the constructor of
  `CustomChipWrapper<CustomSX1276>` and its vtable (112 bytes), and
  `setModemParams()`. The same code at `-Os` costs the 56 to 144 bytes above.

So the flash differences are inlining and devirtualization decisions and, on the
one unoptimized board, the cost of the extra class level. No behaviour depends on
them; the host suite pins the calls each wrapper makes.

<details>
<summary>Flash and RAM per environment (597)</summary>

| Environment | Platform | Wrapper | Flash before | Flash after | Flash | RAM |
|---|---|---|---|---|---|---|
| Ebyte_EoRa-S3_Repeater | ESP32 | SX1262 | 1108433 | 1108433 | +0 | +0 |
| Ebyte_EoRa-S3_companion_radio_ble | ESP32 | SX1262 | 1217029 | 1217021 | -8 | +0 |
| Ebyte_EoRa-S3_companion_radio_usb | ESP32 | SX1262 | 591533 | 591529 | -4 | +0 |
| Ebyte_EoRa-S3_kiss_modem | ESP32 | SX1262 | 507821 | 507821 | +0 | +0 |
| Ebyte_EoRa-S3_room_server | ESP32 | SX1262 | 1103745 | 1103745 | +0 | +0 |
| Ebyte_EoRa-S3_terminal_chat | ESP32 | SX1262 | 535737 | 535685 | -52 | +0 |
| GAT562_30S_Mesh_Kit_companion_radio_ble | nRF52 | SX1262 | 437012 | 436964 | -48 | +0 |
| GAT562_30S_Mesh_Kit_companion_radio_usb | nRF52 | SX1262 | 427988 | 427940 | -48 | +0 |
| GAT562_30S_Mesh_Kit_kiss_modem | nRF52 | SX1262 | 341280 | 341232 | -48 | +0 |
| GAT562_30S_Mesh_Kit_repeater | nRF52 | SX1262 | 411568 | 411504 | -64 | +0 |
| GAT562_30S_Mesh_Kit_room_server | nRF52 | SX1262 | 404656 | 404592 | -64 | +0 |
| GAT562_Mesh_EVB_Pro_kiss_modem | nRF52 | SX1262 | 310916 | 310868 | -48 | +0 |
| GAT562_Mesh_EVB_Pro_repeater | nRF52 | SX1262 | 365764 | 365716 | -48 | +0 |
| GAT562_Mesh_EVB_Pro_room_server | nRF52 | SX1262 | 359260 | 359196 | -64 | +0 |
| GAT562_Mesh_Tracker_Pro_companion_radio_ble | nRF52 | SX1262 | 404184 | 404136 | -48 | +0 |
| GAT562_Mesh_Tracker_Pro_companion_radio_usb | nRF52 | SX1262 | 395176 | 395128 | -48 | +0 |
| GAT562_Mesh_Tracker_Pro_kiss_modem | nRF52 | SX1262 | 310900 | 310852 | -48 | +0 |
| GAT562_Mesh_Tracker_Pro_repeater | nRF52 | SX1262 | 381276 | 381212 | -64 | +0 |
| GAT562_Mesh_Tracker_Pro_room_server | nRF52 | SX1262 | 374364 | 374300 | -64 | +0 |
| GAT562_Mesh_Watch13_companion_radio_ble | nRF52 | SX1262 | 400552 | 400504 | -48 | +0 |
| GAT562_Mesh_Watch13_kiss_modem | nRF52 | SX1262 | 304048 | 304000 | -48 | +0 |
| Generic_E22_sx1262_repeater | ESP32 | SX1262 | 1143573 | 1143573 | +0 | +0 |
| Generic_E22_sx1262_repeater_bridge_espnow | ESP32 | SX1262 | 1154277 | 1154277 | +0 | +0 |
| Generic_E22_sx1268_repeater | ESP32 | SX1268 | 1143361 | 1143361 | +0 | +0 |
| Generic_E22_sx1268_repeater_bridge_espnow | ESP32 | SX1268 | 1154057 | 1154057 | +0 | +0 |
| Heltec_E213_companion_radio_ble | ESP32 | SX1262 | 1274401 | 1274397 | -4 | +0 |
| Heltec_E213_companion_radio_usb | ESP32 | SX1262 | 669857 | 669853 | -4 | +0 |
| Heltec_E213_repeater | ESP32 | SX1262 | 1162233 | 1162233 | +0 | +0 |
| Heltec_E213_repeater_bridge_espnow | ESP32 | SX1262 | 1172677 | 1172677 | +0 | +0 |
| Heltec_E213_room_server | ESP32 | SX1262 | 1157477 | 1157477 | +0 | +0 |
| Heltec_E290_companion_ble | ESP32 | SX1262 | 1272313 | 1272309 | -4 | +0 |
| Heltec_E290_companion_usb | ESP32 | SX1262 | 667645 | 667641 | -4 | +0 |
| Heltec_E290_repeater | ESP32 | SX1262 | 1160185 | 1160185 | +0 | +0 |
| Heltec_E290_repeater_bridge_espnow | ESP32 | SX1262 | 1170613 | 1170613 | +0 | +0 |
| Heltec_E290_room_server | ESP32 | SX1262 | 1155401 | 1155401 | +0 | +0 |
| Heltec_T190_companion_radio_ble_ | ESP32 | SX1262 | 1251489 | 1251485 | -4 | +0 |
| Heltec_T190_companion_radio_usb_ | ESP32 | SX1262 | 647581 | 647577 | -4 | +0 |
| Heltec_T190_repeater_ | ESP32 | SX1262 | 1139397 | 1139397 | +0 | +0 |
| Heltec_T190_repeater_bridge_espnow_ | ESP32 | SX1262 | 1149833 | 1149833 | +0 | +0 |
| Heltec_T190_room_server_ | ESP32 | SX1262 | 1134653 | 1134653 | +0 | +0 |
| Heltec_WSL3_companion_radio_ble | ESP32 | SX1262 | 1649165 | 1649161 | -4 | +0 |
| Heltec_WSL3_companion_radio_usb | ESP32 | SX1262 | 638037 | 638041 | +4 | +0 |
| Heltec_WSL3_companion_radio_wifi | ESP32 | SX1262 | 1087557 | 1087553 | -4 | +0 |
| Heltec_WSL3_repeater | ESP32 | SX1262 | 1161337 | 1161337 | +0 | +0 |
| Heltec_WSL3_repeater_bridge_espnow | ESP32 | SX1262 | 1171765 | 1171765 | +0 | +0 |
| Heltec_WSL3_repeater_bridge_rs232 | ESP32 | SX1262 | 1163785 | 1163785 | +0 | +0 |
| Heltec_WSL3_room_server | ESP32 | SX1262 | 1156821 | 1156821 | +0 | +0 |
| Heltec_WSL3_sensor | ESP32 | SX1262 | 1168233 | 1168233 | +0 | +0 |
| Heltec_Wireless_Paper_companion_radio_ble | ESP32 | SX1262 | 1616301 | 1616301 | +0 | +0 |
| Heltec_Wireless_Paper_companion_radio_usb | ESP32 | SX1262 | 626549 | 626545 | -4 | +0 |
| Heltec_Wireless_Paper_kiss_modem | ESP32 | SX1262 | 517461 | 517461 | +0 | +0 |
| Heltec_Wireless_Paper_repeater | ESP32 | SX1262 | 1121873 | 1121873 | +0 | +0 |
| Heltec_Wireless_Paper_repeater_bridge_espnow | ESP32 | SX1262 | 1132325 | 1132325 | +0 | +0 |
| Heltec_Wireless_Paper_room_server | ESP32 | SX1262 | 1117125 | 1117125 | +0 | +0 |
| Heltec_Wireless_Tracker_companion_radio_ble | ESP32 | SX1262 | 1266077 | 1266081 | +4 | +0 |
| Heltec_Wireless_Tracker_companion_radio_usb | ESP32 | SX1262 | 662693 | 662697 | +4 | +0 |
| Heltec_Wireless_Tracker_kiss_modem | ESP32 | SX1262 | 526709 | 526709 | +0 | +0 |
| Heltec_Wireless_Tracker_repeater | ESP32 | SX1262 | 1157621 | 1157621 | +0 | +0 |
| Heltec_Wireless_Tracker_repeater_bridge_espnow | ESP32 | SX1262 | 1168109 | 1168109 | +0 | +0 |
| Heltec_Wireless_Tracker_room_server | ESP32 | SX1262 | 1152893 | 1152893 | +0 | +0 |
| Heltec_ct62_companion_radio_ble | ESP32 | SX1262 | 1277060 | 1277060 | +0 | +0 |
| Heltec_ct62_companion_radio_usb | ESP32 | SX1262 | 564990 | 564990 | +0 | +0 |
| Heltec_ct62_kiss_modem | ESP32 | SX1262 | 497028 | 497028 | +0 | +0 |
| Heltec_ct62_repeater | ESP32 | SX1262 | 1151874 | 1151874 | +0 | +0 |
| Heltec_ct62_sensor | ESP32 | SX1262 | 1157740 | 1157740 | +0 | +0 |
| Heltec_mesh_solar_companion_radio_ble | nRF52 | SX1262 | 375004 | 374956 | -48 | +0 |
| Heltec_mesh_solar_companion_radio_usb | nRF52 | SX1262 | 370004 | 369956 | -48 | +0 |
| Heltec_mesh_solar_kiss_modem | nRF52 | SX1262 | 309024 | 308976 | -48 | +0 |
| Heltec_mesh_solar_repeater | nRF52 | SX1262 | 365600 | 365536 | -64 | +0 |
| Heltec_mesh_solar_room_server | nRF52 | SX1262 | 359072 | 359008 | -64 | +0 |
| Heltec_t096_companion_radio_ble | nRF52 | SX1262 | 434516 | 434452 | -64 | +0 |
| Heltec_t096_companion_radio_usb | nRF52 | SX1262 | 427780 | 427732 | -48 | +0 |
| Heltec_t096_kiss_modem | nRF52 | SX1262 | 340776 | 340728 | -48 | +0 |
| Heltec_t096_repeater | nRF52 | SX1262 | 414464 | 414416 | -48 | +0 |
| Heltec_t096_repeater_bridge_rs232 | nRF52 | SX1262 | 416736 | 416688 | -48 | +0 |
| Heltec_t096_room_server | nRF52 | SX1262 | 407584 | 407520 | -64 | +0 |
| Heltec_t096_sensor | nRF52 | SX1262 | 411348 | 411300 | -48 | +0 |
| Heltec_t114_companion_radio_ble | nRF52 | SX1262 | 418584 | 418536 | -48 | +0 |
| Heltec_t114_companion_radio_usb | nRF52 | SX1262 | 411848 | 411800 | -48 | +0 |
| Heltec_t114_kiss_modem | nRF52 | SX1262 | 311268 | 311204 | -64 | +0 |
| Heltec_t114_repeater | nRF52 | SX1262 | 398460 | 398396 | -64 | +0 |
| Heltec_t114_repeater_bridge_rs232 | nRF52 | SX1262 | 368532 | 368468 | -64 | +0 |
| Heltec_t114_room_server | nRF52 | SX1262 | 391580 | 391516 | -64 | +0 |
| Heltec_t114_without_display_companion_radio_ble | nRF52 | SX1262 | 392832 | 392784 | -48 | +0 |
| Heltec_t114_without_display_companion_radio_usb | nRF52 | SX1262 | 386000 | 385952 | -48 | +0 |
| Heltec_t114_without_display_repeater | nRF52 | SX1262 | 366260 | 366196 | -64 | +0 |
| Heltec_t114_without_display_repeater_bridge_rs232 | nRF52 | SX1262 | 368532 | 368468 | -64 | +0 |
| Heltec_t114_without_display_room_server | nRF52 | SX1262 | 359740 | 359676 | -64 | +0 |
| Heltec_t1_companion_radio_ble | nRF52 | SX1262 | 436016 | 435952 | -64 | +0 |
| Heltec_t1_companion_radio_usb | nRF52 | SX1262 | 429312 | 429248 | -64 | +0 |
| Heltec_t1_kiss_modem | nRF52 | SX1262 | 339884 | 339836 | -48 | +0 |
| Heltec_t1_repeater | nRF52 | SX1262 | 413508 | 413460 | -48 | +0 |
| Heltec_t1_room_server | nRF52 | SX1262 | 406644 | 406596 | -48 | +0 |
| Heltec_tower_v2_companion_radio_ble | nRF52 | SX1262 | 335884 | 335836 | -48 | +0 |
| Heltec_tower_v2_companion_radio_usb | nRF52 | SX1262 | 329044 | 328996 | -48 | +0 |
| Heltec_tower_v2_kiss_modem | nRF52 | SX1262 | 251864 | 251800 | -64 | +0 |
| Heltec_tower_v2_repeater | nRF52 | SX1262 | 309528 | 309480 | -48 | +0 |
| Heltec_tower_v2_room_server | nRF52 | SX1262 | 303000 | 302952 | -48 | +0 |
| Heltec_v2_companion_radio_ble | ESP32 | SX1276 | 1431145 | 1431229 | +84 | +0 |
| Heltec_v2_companion_radio_usb | ESP32 | SX1276 | 613269 | 613349 | +80 | +0 |
| Heltec_v2_companion_radio_wifi | ESP32 | SX1276 | 1084281 | 1084365 | +84 | +0 |
| Heltec_v2_kiss_modem | ESP32 | SX1276 | 506169 | 506169 | +0 | +0 |
| Heltec_v2_repeater | ESP32 | SX1276 | 1151913 | 1151997 | +84 | +0 |
| Heltec_v2_repeater_bridge_espnow | ESP32 | SX1276 | 1162757 | 1162813 | +56 | +0 |
| Heltec_v2_room_server | ESP32 | SX1276 | 1147261 | 1147337 | +76 | +0 |
| Heltec_v2_terminal_chat | ESP32 | SX1276 | 534429 | 534429 | +0 | +0 |
| Heltec_v3_companion_radio_ble | ESP32 | SX1262 | 1671657 | 1671653 | -4 | +0 |
| Heltec_v3_companion_radio_usb | ESP32 | SX1262 | 658197 | 658201 | +4 | +0 |
| Heltec_v3_companion_radio_wifi | ESP32 | SX1262 | 1107825 | 1107821 | -4 | +0 |
| Heltec_v3_kiss_modem | ESP32 | SX1262 | 581253 | 581253 | +0 | +0 |
| Heltec_v3_repeater | ESP32 | SX1262 | 1175025 | 1175025 | +0 | +0 |
| Heltec_v3_repeater_bridge_espnow | ESP32 | SX1262 | 1185493 | 1185493 | +0 | +0 |
| Heltec_v3_repeater_bridge_rs232 | ESP32 | SX1262 | 1177493 | 1177493 | +0 | +0 |
| Heltec_v3_room_server | ESP32 | SX1262 | 1170173 | 1170173 | +0 | +0 |
| Heltec_v3_sensor | ESP32 | SX1262 | 1181561 | 1181561 | +0 | +0 |
| Heltec_v3_terminal_chat | ESP32 | SX1262 | 602381 | 602337 | -44 | +0 |
| KeepteenLT1_companion_radio_ble | nRF52 | SX1262 | 353648 | 353600 | -48 | +0 |
| KeepteenLT1_companion_radio_usb | nRF52 | SX1262 | 335720 | 335656 | -64 | +0 |
| KeepteenLT1_kiss_modem | nRF52 | SX1262 | 248868 | 248820 | -48 | +0 |
| KeepteenLT1_repeater | nRF52 | SX1262 | 322028 | 321964 | -64 | +0 |
| KeepteenLT1_room_server | nRF52 | SX1262 | 315108 | 315044 | -64 | +0 |
| LilyGo_T-Echo-Lite_companion_radio_ble | nRF52 | SX1262 | 357416 | 357352 | -64 | +0 |
| LilyGo_T-Echo-Lite_companion_radio_usb | nRF52 | SX1262 | 352512 | 352448 | -64 | +0 |
| LilyGo_T-Echo-Lite_kiss_modem | nRF52 | SX1262 | 276892 | 276844 | -48 | +0 |
| LilyGo_T-Echo-Lite_non_shell_companion_radio_ble | nRF52 | SX1262 | 316504 | 316440 | -64 | +0 |
| LilyGo_T-Echo-Lite_non_shell_companion_radio_usb | nRF52 | SX1262 | 311360 | 311312 | -48 | +0 |
| LilyGo_T-Echo-Lite_repeater | nRF52 | SX1262 | 335772 | 335708 | -64 | +0 |
| LilyGo_T-Echo-Lite_room_server | nRF52 | SX1262 | 328964 | 328900 | -64 | +0 |
| LilyGo_T-Echo_Card_companion_radio_ble | nRF52 | SX1262 | 350208 | 350160 | -48 | +0 |
| LilyGo_T-Echo_Card_companion_radio_usb | nRF52 | SX1262 | 344448 | 344400 | -48 | +0 |
| LilyGo_T-Echo_Card_kiss_modem | nRF52 | SX1262 | 261536 | 261472 | -64 | +0 |
| LilyGo_T-Echo_Card_repeater | nRF52 | SX1262 | 322368 | 322304 | -64 | +0 |
| LilyGo_T-Echo_Card_room_server | nRF52 | SX1262 | 314112 | 314064 | -48 | +0 |
| LilyGo_T-Echo_companion_radio_ble | nRF52 | SX1262 | 376640 | 376592 | -48 | +0 |
| LilyGo_T-Echo_companion_radio_usb | nRF52 | SX1262 | 369248 | 369200 | -48 | +0 |
| LilyGo_T-Echo_kiss_modem | nRF52 | SX1262 | 291496 | 291432 | -64 | +0 |
| LilyGo_T-Echo_repeater | nRF52 | SX1262 | 350096 | 350032 | -64 | +0 |
| LilyGo_T-Echo_room_server | nRF52 | SX1262 | 343288 | 343240 | -48 | +0 |
| LilyGo_T3S3_sx1262_companion_radio_ble | ESP32 | SX1262 | 1217077 | 1217069 | -8 | +0 |
| LilyGo_T3S3_sx1262_companion_radio_usb | ESP32 | SX1262 | 591581 | 591577 | -4 | +0 |
| LilyGo_T3S3_sx1262_kiss_modem | ESP32 | SX1262 | 507869 | 507869 | +0 | +0 |
| LilyGo_T3S3_sx1262_repeater | ESP32 | SX1262 | 1108533 | 1108533 | +0 | +0 |
| LilyGo_T3S3_sx1262_repeater_bridge_espnow | ESP32 | SX1262 | 1118825 | 1118825 | +0 | +0 |
| LilyGo_T3S3_sx1262_room_server | ESP32 | SX1262 | 1103801 | 1103801 | +0 | +0 |
| LilyGo_T3S3_sx1262_terminal_chat | ESP32 | SX1262 | 535785 | 535733 | -52 | +0 |
| LilyGo_T3S3_sx1276_companion_radio_ble | ESP32 | SX1276 | 1214793 | 1214869 | +76 | +0 |
| LilyGo_T3S3_sx1276_companion_radio_usb | ESP32 | SX1276 | 589469 | 589545 | +76 | +0 |
| LilyGo_T3S3_sx1276_kiss_modem | ESP32 | SX1276 | 505769 | 505769 | +0 | +0 |
| LilyGo_T3S3_sx1276_repeater | ESP32 | SX1276 | 1105521 | 1105605 | +84 | +0 |
| LilyGo_T3S3_sx1276_repeater_bridge_espnow | ESP32 | SX1276 | 1115821 | 1115905 | +84 | +0 |
| LilyGo_T3S3_sx1276_room_server | ESP32 | SX1276 | 1100725 | 1100813 | +88 | +0 |
| LilyGo_T3S3_sx1276_terminal_chat | ESP32 | SX1276 | 533621 | 533621 | +0 | +0 |
| LilyGo_TBeam_1W_companion_radio_ble | ESP32 | SX1262 | 1267529 | 1267537 | +8 | +0 |
| LilyGo_TBeam_1W_companion_radio_usb | ESP32 | SX1262 | 642565 | 642577 | +12 | +0 |
| LilyGo_TBeam_1W_companion_radio_wifi | ESP32 | SX1262 | 1088761 | 1088761 | +0 | +0 |
| LilyGo_TBeam_1W_repeater | ESP32 | SX1262 | 1154973 | 1154973 | +0 | +0 |
| LilyGo_TBeam_1W_repeater_bridge_espnow | ESP32 | SX1262 | 1165461 | 1165461 | +0 | +0 |
| LilyGo_TBeam_1W_room_server | ESP32 | SX1262 | 1150141 | 1150141 | +0 | +0 |
| LilyGo_TDeck_companion_radio_ble | ESP32 | SX1262 | 1253177 | 1253181 | +4 | +0 |
| LilyGo_TDeck_companion_radio_usb | ESP32 | SX1262 | 649749 | 649753 | +4 | +0 |
| LilyGo_TDeck_kiss_modem | ESP32 | SX1262 | 563305 | 563305 | +0 | +0 |
| LilyGo_TDeck_repeater | ESP32 | SX1262 | 1144713 | 1144713 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_companion_radio_ble | ESP32 | SX1262 | 1178633 | 1178613 | -20 | +0 |
| LilyGo_TETH_Elite_sx1262_companion_radio_usb | ESP32 | SX1262 | 553953 | 553933 | -20 | +0 |
| LilyGo_TETH_Elite_sx1262_kiss_modem | ESP32 | SX1262 | 489269 | 489269 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_repeater | ESP32 | SX1262 | 1076289 | 1076289 | +0 | +0 |
| LilyGo_TETH_Elite_sx1262_room_server | ESP32 | SX1262 | 1071653 | 1071653 | +0 | +0 |
| LilyGo_TLora_V2_1_1_6_companion_radio_ble | ESP32 | SX1276 | 1917873 | 1918181 | +308 | +0 |
| LilyGo_TLora_V2_1_1_6_companion_radio_usb | ESP32 | SX1276 | 993025 | 993389 | +364 | +0 |
| LilyGo_TLora_V2_1_1_6_companion_radio_wifi | ESP32 | SX1276 | 1520857 | 1521209 | +352 | +0 |
| LilyGo_TLora_V2_1_1_6_kiss_modem | ESP32 | SX1276 | 862433 | 862805 | +372 | +0 |
| LilyGo_TLora_V2_1_1_6_repeater | ESP32 | SX1276 | 1663989 | 1664357 | +368 | +0 |
| LilyGo_TLora_V2_1_1_6_repeater_bridge_espnow | ESP32 | SX1276 | 1677925 | 1678253 | +328 | +0 |
| LilyGo_TLora_V2_1_1_6_repeater_bridge_rs232 | ESP32 | SX1276 | 1669385 | 1669705 | +320 | +0 |
| LilyGo_TLora_V2_1_1_6_room_server | ESP32 | SX1276 | 1642141 | 1642473 | +332 | +0 |
| LilyGo_TLora_V2_1_1_6_terminal_chat | ESP32 | SX1276 | 907025 | 907409 | +384 | +0 |
| LilyGo_T_Impulse_Plus_companion_radio_ble | nRF52 | SX1262 | 406948 | 406900 | -48 | +0 |
| LilyGo_T_Impulse_Plus_kiss_modem | nRF52 | SX1262 | 311364 | 311300 | -64 | +0 |
| LilyGo_T_Impulse_Plus_repeater | nRF52 | SX1262 | 366508 | 366444 | -64 | +0 |
| LilyGo_Tlora_C6_companion_radio_ble_ | ESP32 | SX1262 | 1446667 | 1446667 | +0 | +0 |
| LilyGo_Tlora_C6_kiss_modem | ESP32 | SX1262 | 472809 | 472809 | +0 | +0 |
| LilyGo_Tlora_C6_repeater_ | ESP32 | SX1262 | 542447 | 542447 | +0 | +0 |
| LilyGo_Tlora_C6_room_server_ | ESP32 | SX1262 | 536981 | 536979 | -2 | +0 |
| M5Stack_Unit_C6L_companion_radio_ble | ESP32 | SX1262 | 1454227 | 1454239 | +12 | +0 |
| M5Stack_Unit_C6L_companion_radio_usb | ESP32 | SX1262 | 546345 | 546345 | +0 | +0 |
| M5Stack_Unit_C6L_kiss_modem | ESP32 | SX1262 | 480175 | 480175 | +0 | +0 |
| M5Stack_Unit_C6L_repeater | ESP32 | SX1262 | 552821 | 552821 | +0 | +0 |
| M5Stack_Unit_C6L_room_server | ESP32 | SX1262 | 547355 | 547355 | +0 | +0 |
| MeshTracker_X1_companion_radio_ble | nRF52 | LR2021 | 325484 | 325484 | +0 | +0 |
| MeshTracker_X1_companion_radio_usb | nRF52 | LR2021 | 319852 | 319836 | -16 | +0 |
| MeshTracker_X1_kiss_modem | nRF52 | LR2021 | 241380 | 241380 | +0 | +0 |
| MeshTracker_X1_repeater | nRF52 | LR2021 | 301152 | 301152 | +0 | +0 |
| MeshTracker_X1_room_server | nRF52 | LR2021 | 293652 | 293636 | -16 | +0 |
| Mesh_pocket_companion_radio_ble | nRF52 | SX1262 | 345996 | 345948 | -48 | +0 |
| Mesh_pocket_companion_radio_usb | nRF52 | SX1262 | 339196 | 339132 | -64 | +0 |
| Mesh_pocket_kiss_modem | nRF52 | SX1262 | 268408 | 268360 | -48 | +0 |
| Mesh_pocket_repeater | nRF52 | SX1262 | 326840 | 326792 | -48 | +0 |
| Mesh_pocket_room_server | nRF52 | SX1262 | 320096 | 320048 | -48 | +0 |
| Meshadventurer_sx1262_companion_radio_ble | ESP32 | SX1262 | 1442253 | 1442257 | +4 | +0 |
| Meshadventurer_sx1262_companion_radio_usb | ESP32 | SX1262 | 603441 | 603445 | +4 | +0 |
| Meshadventurer_sx1262_kiss_modem | ESP32 | SX1262 | 529933 | 529933 | +0 | +0 |
| Meshadventurer_sx1262_repeater | ESP32 | SX1262 | 1162097 | 1162097 | +0 | +0 |
| Meshadventurer_sx1262_repeater_bridge_espnow | ESP32 | SX1262 | 1172881 | 1172881 | +0 | +0 |
| Meshadventurer_sx1262_room_server | ESP32 | SX1262 | 1157353 | 1157353 | +0 | +0 |
| Meshadventurer_sx1262_terminal_chat | ESP32 | SX1262 | 558457 | 558413 | -44 | +0 |
| Meshadventurer_sx1268_companion_radio_ble | ESP32 | SX1268 | 1442029 | 1442033 | +4 | +0 |
| Meshadventurer_sx1268_companion_radio_usb | ESP32 | SX1268 | 603205 | 603213 | +8 | +0 |
| Meshadventurer_sx1268_kiss_modem | ESP32 | SX1268 | 529701 | 529701 | +0 | +0 |
| Meshadventurer_sx1268_repeater | ESP32 | SX1268 | 1161869 | 1161869 | +0 | +0 |
| Meshadventurer_sx1268_repeater_bridge_espnow | ESP32 | SX1268 | 1172661 | 1172661 | +0 | +0 |
| Meshadventurer_sx1268_room_server | ESP32 | SX1268 | 1157133 | 1157133 | +0 | +0 |
| Meshadventurer_sx1268_terminal_chat | ESP32 | SX1268 | 558233 | 558185 | -48 | +0 |
| Meshimi_companion_radio_ble_ | ESP32 | SX1262 | 1446739 | 1446739 | +0 | +0 |
| Meshimi_repeater_ | ESP32 | SX1262 | 542535 | 542535 | +0 | +0 |
| Meshtiny_companion_radio_ble | nRF52 | SX1262 | 338512 | 338464 | -48 | +0 |
| Meshtiny_companion_radio_usb | nRF52 | SX1262 | 331112 | 331064 | -48 | +0 |
| Meshtiny_kiss_modem | nRF52 | SX1262 | 243268 | 243220 | -48 | +0 |
| Minewsemi_me25ls01_companion_radio_ble | nRF52 | LR1110 | 319852 | 319852 | +0 | +0 |
| Minewsemi_me25ls01_companion_radio_usb | nRF52 | LR1110 | 314156 | 314140 | -16 | +0 |
| Minewsemi_me25ls01_kiss_modem | nRF52 | LR1110 | 244700 | 244700 | +0 | +0 |
| Minewsemi_me25ls01_repeater | nRF52 | LR1110 | 306976 | 306976 | +0 | +0 |
| Minewsemi_me25ls01_room_server | nRF52 | LR1110 | 298820 | 298820 | +0 | +0 |
| Minewsemi_me25ls01_terminal_chat | nRF52 | LR1110 | 271188 | 271188 | +0 | +0 |
| Nano_G2_Ultra_companion_radio_ble | nRF52 | SX1262 | 346684 | 346636 | -48 | +0 |
| Nano_G2_Ultra_companion_radio_usb | nRF52 | SX1262 | 339276 | 339212 | -64 | +0 |
| Nano_G2_Ultra_kiss_modem | nRF52 | SX1262 | 249476 | 249428 | -48 | +0 |
| Nano_G2_Ultra_repeater | nRF52 | SX1262 | 319500 | 319452 | -48 | +0 |
| Nano_G2_Ultra_room_server | nRF52 | SX1262 | 312580 | 312516 | -64 | +0 |
| PicoW_companion_radio | RP2040 | SX1262 | 667496 | 667496 | +0 | +0 |
| PicoW_companion_radio_ble | RP2040 | SX1262 | 635332 | 635332 | +0 | +0 |
| PicoW_companion_radio_usb | RP2040 | SX1262 | 497012 | 497012 | +0 | +0 |
| PicoW_companion_radio_wifi | RP2040 | SX1262 | 530428 | 530428 | +0 | +0 |
| PicoW_kiss_modem | RP2040 | SX1262 | 439572 | 439572 | +0 | +0 |
| PicoW_repeater | RP2040 | SX1262 | 492668 | 492668 | +0 | +0 |
| PicoW_room_server | RP2040 | SX1262 | 488612 | 488612 | +0 | +0 |
| PicoW_terminal_chat | RP2040 | SX1262 | 469148 | 469148 | +0 | +0 |
| ProMicro_companion_radio_ble | nRF52 | SX1262 | 368804 | 368756 | -48 | +0 |
| ProMicro_companion_radio_usb | nRF52 | SX1262 | 359796 | 359732 | -64 | +0 |
| ProMicro_kiss_modem | nRF52 | SX1262 | 273400 | 273336 | -64 | +0 |
| ProMicro_repeater | nRF52 | SX1262 | 346240 | 346176 | -64 | +0 |
| ProMicro_repeater_bridge_rs232_serial1 | nRF52 | SX1262 | 342544 | 342496 | -48 | +0 |
| ProMicro_room_server | nRF52 | SX1262 | 339288 | 339224 | -64 | +0 |
| ProMicro_sensor | nRF52 | SX1262 | 342956 | 342892 | -64 | +0 |
| ProMicro_terminal_chat | nRF52 | SX1262 | 298384 | 298320 | -64 | +0 |
| R1Neo_companion_radio_ble | nRF52 | SX1262 | 386296 | 386248 | -48 | +0 |
| R1Neo_companion_radio_usb | nRF52 | SX1262 | 378888 | 378840 | -48 | +0 |
| R1Neo_kiss_modem | nRF52 | SX1262 | 312020 | 311956 | -64 | +0 |
| R1Neo_repeater | nRF52 | SX1262 | 368916 | 368852 | -64 | +0 |
| R1Neo_room_server | nRF52 | SX1262 | 361420 | 361356 | -64 | +0 |
| R1Neo_sensor | nRF52 | SX1262 | 365104 | 365040 | -64 | +0 |
| R1Neo_terminal_chat | nRF52 | SX1262 | 334572 | 334524 | -48 | +0 |
| RAK_11310_companion_radio_usb | RP2040 | SX1262 | 240532 | 240532 | +0 | +0 |
| RAK_11310_kiss_modem | RP2040 | SX1262 | 183084 | 183084 | +0 | +0 |
| RAK_11310_repeater | RP2040 | SX1262 | 236172 | 236172 | +0 | +0 |
| RAK_11310_repeater_bridge_rs232 | RP2040 | SX1262 | 240664 | 240664 | +0 | +0 |
| RAK_11310_room_server | RP2040 | SX1262 | 232124 | 232124 | +0 | +0 |
| RAK_11310_terminal_chat | RP2040 | SX1262 | 212668 | 212668 | +0 | +0 |
| RAK_3112_companion_radio_ble | ESP32 | SX1262 | 1654533 | 1654529 | -4 | +0 |
| RAK_3112_companion_radio_usb | ESP32 | SX1262 | 643549 | 643553 | +4 | +0 |
| RAK_3112_companion_radio_wifi | ESP32 | SX1262 | 1092901 | 1092897 | -4 | +0 |
| RAK_3112_kiss_modem | ESP32 | SX1262 | 586073 | 586073 | +0 | +0 |
| RAK_3112_repeater | ESP32 | SX1262 | 1165829 | 1165829 | +0 | +0 |
| RAK_3112_repeater_bridge_espnow | ESP32 | SX1262 | 1176333 | 1176333 | +0 | +0 |
| RAK_3112_repeater_bridge_rs232 | ESP32 | SX1262 | 1168329 | 1168329 | +0 | +0 |
| RAK_3112_room_server | ESP32 | SX1262 | 1161341 | 1161341 | +0 | +0 |
| RAK_3112_sensor | ESP32 | SX1262 | 1172777 | 1172777 | +0 | +0 |
| RAK_3112_terminal_chat | ESP32 | SX1262 | 607193 | 607145 | -48 | +0 |
| RAK_3401_companion_radio_ble | nRF52 | SX1262 | 433916 | 433868 | -48 | +0 |
| RAK_3401_companion_radio_usb | nRF52 | SX1262 | 424908 | 424860 | -48 | +0 |
| RAK_3401_kiss_modem | nRF52 | SX1262 | 340928 | 340880 | -48 | +0 |
| RAK_3401_repeater | nRF52 | SX1262 | 411040 | 410992 | -48 | +0 |
| RAK_3401_room_server | nRF52 | SX1262 | 404192 | 404144 | -48 | +0 |
| RAK_3401_sensor | nRF52 | SX1262 | 407888 | 407824 | -64 | +0 |
| RAK_3401_terminal_chat | nRF52 | SX1262 | 363424 | 363376 | -48 | +0 |
| RAK_3x72_companion_radio_usb | STM32 | STM32WLx | 199816 | 199824 | +8 | +0 |
| RAK_3x72_kiss_modem | STM32 | STM32WLx | 143164 | 143164 | +0 | +0 |
| RAK_3x72_repeater | STM32 | STM32WLx | 196776 | 196776 | +0 | +0 |
| RAK_3x72_sensor | STM32 | STM32WLx | 196376 | 196376 | +0 | +0 |
| RAK_4631_companion_radio_ble | nRF52 | SX1262 | 465172 | 465124 | -48 | +0 |
| RAK_4631_companion_radio_ethernet | nRF52 | SX1262 | 470712 | 470664 | -48 | +0 |
| RAK_4631_companion_radio_usb | nRF52 | SX1262 | 456148 | 456100 | -48 | +0 |
| RAK_4631_kiss_modem | nRF52 | SX1262 | 371976 | 371928 | -48 | +0 |
| RAK_4631_repeater | nRF52 | SX1262 | 441944 | 441880 | -64 | +0 |
| RAK_4631_repeater_bridge_rs232_serial1 | nRF52 | SX1262 | 408052 | 407988 | -64 | +0 |
| RAK_4631_repeater_bridge_rs232_serial2 | nRF52 | SX1262 | 408052 | 407988 | -64 | +0 |
| RAK_4631_repeater_ethernet | nRF52 | SX1262 | 459588 | 459540 | -48 | +0 |
| RAK_4631_room_server | nRF52 | SX1262 | 435096 | 435032 | -64 | +0 |
| RAK_4631_room_server_ethernet | nRF52 | SX1262 | 452732 | 452684 | -48 | +0 |
| RAK_4631_sensor | nRF52 | SX1262 | 438776 | 438712 | -64 | +0 |
| RAK_4631_terminal_chat | nRF52 | SX1262 | 394376 | 394328 | -48 | +0 |
| RAK_WisMesh_Tag_companion_radio_ble | nRF52 | SX1262 | 386600 | 386552 | -48 | +0 |
| RAK_WisMesh_Tag_companion_radio_usb | nRF52 | SX1262 | 379176 | 379128 | -48 | +0 |
| RAK_WisMesh_Tag_kiss_modem | nRF52 | SX1262 | 312180 | 312116 | -64 | +0 |
| RAK_WisMesh_Tag_repeater | nRF52 | SX1262 | 369076 | 369012 | -64 | +0 |
| RAK_WisMesh_Tag_room_server | nRF52 | SX1262 | 361548 | 361500 | -48 | +0 |
| RAK_WisMesh_Tag_sensor | nRF52 | SX1262 | 365232 | 365184 | -48 | +0 |
| SenseCap_Solar_companion_radio_ble | nRF52 | SX1262 | 375040 | 374976 | -64 | +0 |
| SenseCap_Solar_companion_radio_usb | nRF52 | SX1262 | 369904 | 369840 | -64 | +0 |
| SenseCap_Solar_kiss_modem | nRF52 | SX1262 | 311028 | 310980 | -48 | +0 |
| SenseCap_Solar_repeater | nRF52 | SX1262 | 366108 | 366044 | -64 | +0 |
| SenseCap_Solar_room_server | nRF52 | SX1262 | 359372 | 359324 | -48 | +0 |
| Station_G2_companion_radio_usb | ESP32 | SX1262 | 715897 | 715897 | +0 | +0 |
| Station_G2_companion_radio_wifi | ESP32 | SX1262 | 1141001 | 1141005 | +4 | +0 |
| Station_G2_kiss_modem | ESP32 | SX1262 | 598649 | 598649 | +0 | +0 |
| Station_G2_logging_repeater | ESP32 | SX1262 | 1207909 | 1207909 | +0 | +0 |
| Station_G2_logging_repeater_bridge_espnow | ESP32 | SX1262 | 1218365 | 1218365 | +0 | +0 |
| Station_G2_repeater | ESP32 | SX1262 | 1207885 | 1207885 | +0 | +0 |
| Station_G2_repeater_bridge_espnow | ESP32 | SX1262 | 1218341 | 1218341 | +0 | +0 |
| Station_G2_room_server | ESP32 | SX1262 | 1203069 | 1203069 | +0 | +0 |
| Station_G3_ESP32_companion_radio_usb | ESP32 | SX1262 | 717525 | 717525 | +0 | +0 |
| Station_G3_ESP32_companion_radio_wifi | ESP32 | SX1262 | 1142589 | 1142593 | +4 | +0 |
| Station_G3_ESP32_logging_repeater | ESP32 | SX1262 | 1209509 | 1209509 | +0 | +0 |
| Station_G3_ESP32_repeater | ESP32 | SX1262 | 1209469 | 1209469 | +0 | +0 |
| Station_G3_ESP32_room_server | ESP32 | SX1262 | 1204681 | 1204681 | +0 | +0 |
| T_Beam_S3_Supreme_SX1262_companion_radio_ble | ESP32 | SX1262 | 1264157 | 1264161 | +4 | +0 |
| T_Beam_S3_Supreme_SX1262_companion_radio_wifi | ESP32 | SX1262 | 1085749 | 1085745 | -4 | +0 |
| T_Beam_S3_Supreme_SX1262_kiss_modem | ESP32 | SX1262 | 570321 | 570321 | +0 | +0 |
| T_Beam_S3_Supreme_SX1262_repeater | ESP32 | SX1262 | 1155517 | 1155517 | +0 | +0 |
| T_Beam_S3_Supreme_SX1262_repeater_bridge_espnow | ESP32 | SX1262 | 1166001 | 1166001 | +0 | +0 |
| T_Beam_S3_Supreme_SX1262_room_server | ESP32 | SX1262 | 1150717 | 1150717 | +0 | +0 |
| Tbeam_SX1262_companion_radio_ble | ESP32 | SX1262 | 1485265 | 1485273 | +8 | +0 |
| Tbeam_SX1262_kiss_modem | ESP32 | SX1262 | 567437 | 567437 | +0 | +0 |
| Tbeam_SX1262_repeater | ESP32 | SX1262 | 1205213 | 1205213 | +0 | +0 |
| Tbeam_SX1262_repeater_bridge_espnow | ESP32 | SX1262 | 1216165 | 1216165 | +0 | +0 |
| Tbeam_SX1262_room_server | ESP32 | SX1262 | 1200065 | 1200065 | +0 | +0 |
| Tbeam_SX1276_companion_radio_ble | ESP32 | SX1276 | 1481229 | 1481313 | +84 | +0 |
| Tbeam_SX1276_kiss_modem | ESP32 | SX1276 | 563485 | 563485 | +0 | +0 |
| Tbeam_SX1276_repeater | ESP32 | SX1276 | 1201181 | 1201325 | +144 | +0 |
| Tbeam_SX1276_repeater_bridge_espnow | ESP32 | SX1276 | 1212213 | 1212285 | +72 | +0 |
| Tbeam_SX1276_room_server | ESP32 | SX1276 | 1196049 | 1196157 | +108 | +0 |
| Tenstar_C3_sx1262_kiss_modem | ESP32 | SX1262 | 497480 | 497480 | +0 | +0 |
| Tenstar_C3_sx1262_repeater | ESP32 | SX1262 | 1152392 | 1152392 | +0 | +0 |
| Tenstar_C3_sx1268_kiss_modem | ESP32 | SX1268 | 497412 | 497412 | +0 | +0 |
| Tenstar_C3_sx1268_repeater | ESP32 | SX1268 | 1152294 | 1152294 | +0 | +0 |
| ThinkNode_M1_companion_radio_ble | nRF52 | SX1262 | 365532 | 365484 | -48 | +0 |
| ThinkNode_M1_companion_radio_usb | nRF52 | SX1262 | 354748 | 354684 | -64 | +0 |
| ThinkNode_M1_kiss_modem | nRF52 | SX1262 | 249444 | 249396 | -48 | +0 |
| ThinkNode_M1_repeater | nRF52 | SX1262 | 306180 | 306116 | -64 | +0 |
| ThinkNode_M1_room_server | nRF52 | SX1262 | 299636 | 299572 | -64 | +0 |
| ThinkNode_M2_Repeater | ESP32 | SX1262 | 1110181 | 1110181 | +0 | +0 |
| ThinkNode_M2_Repeater_bridge_espnow | ESP32 | SX1262 | 1120509 | 1120509 | +0 | +0 |
| ThinkNode_M2_companion_radio_ble | ESP32 | SX1262 | 1220049 | 1220045 | -4 | +0 |
| ThinkNode_M2_companion_radio_serial | ESP32 | SX1262 | 617201 | 617197 | -4 | +0 |
| ThinkNode_M2_companion_radio_usb | ESP32 | SX1262 | 617125 | 617121 | -4 | +0 |
| ThinkNode_M2_companion_radio_wifi | ESP32 | SX1262 | 1045021 | 1045021 | +0 | +0 |
| ThinkNode_M2_kiss_modem | ESP32 | SX1262 | 520617 | 520617 | +0 | +0 |
| ThinkNode_M2_room_server | ESP32 | SX1262 | 1105477 | 1105477 | +0 | +0 |
| ThinkNode_M2_terminal_chat | ESP32 | SX1262 | 548569 | 548517 | -52 | +0 |
| ThinkNode_M3_companion_radio_ble | nRF52 | LR1110 | 320304 | 320288 | -16 | +0 |
| ThinkNode_M3_companion_radio_usb | nRF52 | LR1110 | 314584 | 314584 | +0 | +0 |
| ThinkNode_M3_kiss_modem | nRF52 | LR1110 | 244580 | 244580 | +0 | +0 |
| ThinkNode_M3_repeater | nRF52 | LR1110 | 302260 | 302260 | +0 | +0 |
| ThinkNode_M3_room_server | nRF52 | LR1110 | 295732 | 295732 | +0 | +0 |
| ThinkNode_M4_companion_radio_ble | nRF52 | LR1110 | 320688 | 320672 | -16 | +0 |
| ThinkNode_M4_companion_radio_usb | nRF52 | LR1110 | 314968 | 314968 | +0 | +0 |
| ThinkNode_M4_kiss_modem | nRF52 | LR1110 | 244564 | 244564 | +0 | +0 |
| ThinkNode_M4_repeater | nRF52 | LR1110 | 302268 | 302252 | -16 | +0 |
| ThinkNode_M4_room_server | nRF52 | LR1110 | 295716 | 295716 | +0 | +0 |
| ThinkNode_M5_Repeater | ESP32 | SX1262 | 1127465 | 1127465 | +0 | +0 |
| ThinkNode_M5_Repeater_bridge_espnow | ESP32 | SX1262 | 1137969 | 1137969 | +0 | +0 |
| ThinkNode_M5_companion_radio_ble | ESP32 | SX1262 | 1237889 | 1237901 | +12 | +0 |
| ThinkNode_M5_companion_radio_usb | ESP32 | SX1262 | 634917 | 634925 | +8 | +0 |
| ThinkNode_M5_companion_radio_wifi | ESP32 | SX1262 | 1062609 | 1062609 | +0 | +0 |
| ThinkNode_M5_kiss_modem | ESP32 | SX1262 | 538413 | 538413 | +0 | +0 |
| ThinkNode_M5_room_server | ESP32 | SX1262 | 1122813 | 1122813 | +0 | +0 |
| ThinkNode_M5_terminal_chat | ESP32 | SX1262 | 566685 | 566629 | -56 | +0 |
| ThinkNode_M6_companion_radio_ble | nRF52 | SX1262 | 377424 | 377360 | -64 | +0 |
| ThinkNode_M6_companion_radio_usb | nRF52 | SX1262 | 369392 | 369344 | -48 | +0 |
| ThinkNode_M6_kiss_modem | nRF52 | SX1262 | 311444 | 311380 | -64 | +0 |
| ThinkNode_M6_repeater | nRF52 | SX1262 | 366212 | 366164 | -48 | +0 |
| ThinkNode_M6_room_server | nRF52 | SX1262 | 359676 | 359628 | -48 | +0 |
| ThinkNode_M7_companion_radio_ble | ESP32 | LR1110 | 1629677 | 1629681 | +4 | +0 |
| ThinkNode_M7_companion_radio_ethernet | ESP32 | LR1110 | 1062465 | 1062453 | -12 | +0 |
| ThinkNode_M7_companion_radio_usb | ESP32 | LR1110 | 589309 | 589297 | -12 | +0 |
| ThinkNode_M7_companion_radio_wifi | ESP32 | LR1110 | 1028821 | 1028825 | +4 | +0 |
| ThinkNode_M7_kiss_modem | ESP32 | LR1110 | 502001 | 502001 | +0 | +0 |
| ThinkNode_M7_repeater | ESP32 | LR1110 | 1089241 | 1089241 | +0 | +0 |
| ThinkNode_M7_room_server | ESP32 | LR1110 | 1084621 | 1084621 | +0 | +0 |
| ThinkNode_M8_companion_radio_ble | nRF52 | SX1262 | 371116 | 371052 | -64 | +0 |
| ThinkNode_M8_companion_radio_usb | nRF52 | SX1262 | 346856 | 346792 | -64 | +0 |
| ThinkNode_M9_companion_radio_ble_ | ESP32 | LR1110 | 1292989 | 1292993 | +4 | +0 |
| ThinkNode_M9_companion_radio_usb_ | ESP32 | LR1110 | 689357 | 689361 | +4 | +0 |
| ThinkNode_M9_companion_radio_wifi_ | ESP32 | LR1110 | 1117505 | 1117509 | +4 | +0 |
| ThinkNode_M9_kiss_modem | ESP32 | LR1110 | 601349 | 601349 | +0 | +0 |
| ThinkNode_M9_repeater_ | ESP32 | LR1110 | 1182977 | 1182977 | +0 | +0 |
| ThinkNode_M9_room_server_ | ESP32 | LR1110 | 1178285 | 1178285 | +0 | +0 |
| Tiny_Relay_companion_radio_usb | STM32 | STM32WLx | 199824 | 199832 | +8 | +0 |
| Tiny_Relay_kiss_modem | STM32 | STM32WLx | 143164 | 143164 | +0 | +0 |
| Tiny_Relay_repeater | STM32 | STM32WLx | 196784 | 196784 | +0 | +0 |
| Tiny_Relay_sensor | STM32 | STM32WLx | 196376 | 196376 | +0 | +0 |
| WHY2025_badge_companion_radio_ble_ | ESP32 | SX1262 | 1446639 | 1446639 | +0 | +0 |
| WHY2025_badge_repeater_ | ESP32 | SX1262 | 542435 | 542435 | +0 | +0 |
| Waveshare_S3_LR1121_companion_radio_ble | ESP32 | LR1121 | 1185873 | 1185853 | -20 | +0 |
| Waveshare_S3_LR1121_companion_radio_usb | ESP32 | LR1121 | 584221 | 584201 | -20 | +0 |
| Waveshare_S3_LR1121_kiss_modem | ESP32 | LR1121 | 497809 | 497809 | +0 | +0 |
| Waveshare_S3_LR1121_repeater | ESP32 | LR1121 | 1084869 | 1084869 | +0 | +0 |
| Waveshare_S3_LR1121_room_server | ESP32 | LR1121 | 1080313 | 1080313 | +0 | +0 |
| WioTrackerL1-1W_companion_radio_ble | nRF52 | SX1262 | 408732 | 408684 | -48 | +0 |
| WioTrackerL1-1W_companion_radio_usb | nRF52 | SX1262 | 399052 | 398988 | -64 | +0 |
| WioTrackerL1-1W_kiss_modem | nRF52 | SX1262 | 312084 | 312020 | -64 | +0 |
| WioTrackerL1-1W_repeater | nRF52 | SX1262 | 378716 | 378668 | -48 | +0 |
| WioTrackerL1-1W_room_server | nRF52 | SX1262 | 371812 | 371764 | -48 | +0 |
| WioTrackerL1Eink_companion_radio_ble | nRF52 | SX1262 | 425124 | 425076 | -48 | +0 |
| WioTrackerL1Eink_kiss_modem | nRF52 | SX1262 | 311236 | 311172 | -64 | +0 |
| WioTrackerL1_companion_radio_ble | nRF52 | SX1262 | 407676 | 407612 | -64 | +0 |
| WioTrackerL1_companion_radio_usb | nRF52 | SX1262 | 397980 | 397932 | -48 | +0 |
| WioTrackerL1_kiss_modem | nRF52 | SX1262 | 311140 | 311092 | -48 | +0 |
| WioTrackerL1_repeater | nRF52 | SX1262 | 377724 | 377660 | -64 | +0 |
| WioTrackerL1_room_server | nRF52 | SX1262 | 370804 | 370740 | -64 | +0 |
| Xiao_C3_companion_radio_ble | ESP32 | SX1262 | 1256962 | 1256962 | +0 | +0 |
| Xiao_C3_companion_radio_usb | ESP32 | SX1262 | 544902 | 544902 | +0 | +0 |
| Xiao_C3_companion_radio_wifi | ESP32 | SX1262 | 1012330 | 1012328 | -2 | +0 |
| Xiao_C3_kiss_modem | ESP32 | SX1262 | 473692 | 473692 | +0 | +0 |
| Xiao_C3_repeater | ESP32 | SX1262 | 1158046 | 1158046 | +0 | +0 |
| Xiao_C3_room_server | ESP32 | SX1262 | 1152902 | 1152902 | +0 | +0 |
| Xiao_C6_companion_radio_ble_ | ESP32 | SX1262 | 1446683 | 1446683 | +0 | +0 |
| Xiao_C6_kiss_modem | ESP32 | SX1262 | 472835 | 472835 | +0 | +0 |
| Xiao_C6_repeater_ | ESP32 | SX1262 | 542479 | 542479 | +0 | +0 |
| Xiao_S3_WIO_companion_radio_ble | ESP32 | SX1262 | 1643821 | 1643833 | +12 | +0 |
| Xiao_S3_WIO_companion_radio_serial | ESP32 | SX1262 | 672893 | 672893 | +0 | +0 |
| Xiao_S3_WIO_companion_radio_usb | ESP32 | SX1262 | 653989 | 653989 | +0 | +0 |
| Xiao_S3_WIO_companion_radio_wifi | ESP32 | SX1262 | 1055781 | 1055785 | +4 | +0 |
| Xiao_S3_WIO_kiss_modem | ESP32 | SX1262 | 549837 | 549837 | +0 | +0 |
| Xiao_S3_WIO_repeater | ESP32 | SX1262 | 1127969 | 1127969 | +0 | +0 |
| Xiao_S3_WIO_repeater_bridge_espnow | ESP32 | SX1262 | 1138525 | 1138525 | +0 | +0 |
| Xiao_S3_WIO_room_server | ESP32 | SX1262 | 1123353 | 1123353 | +0 | +0 |
| Xiao_S3_WIO_sensor | ESP32 | SX1262 | 1134745 | 1134745 | +0 | +0 |
| Xiao_S3_WIO_terminal_chat | ESP32 | SX1262 | 571045 | 570989 | -56 | +0 |
| Xiao_S3_companion_radio_ble | ESP32 | SX1262 | 1646569 | 1646565 | -4 | +0 |
| Xiao_S3_companion_radio_usb | ESP32 | SX1262 | 656689 | 656693 | +4 | +0 |
| Xiao_S3_kiss_modem | ESP32 | SX1262 | 559569 | 559569 | +0 | +0 |
| Xiao_S3_repeater | ESP32 | SX1262 | 1137605 | 1137605 | +0 | +0 |
| Xiao_S3_repeater_bridge_espnow | ESP32 | SX1262 | 1148121 | 1148121 | +0 | +0 |
| Xiao_S3_room_server | ESP32 | SX1262 | 1132949 | 1132949 | +0 | +0 |
| Xiao_S3_sensor | ESP32 | SX1262 | 1144397 | 1144397 | +0 | +0 |
| Xiao_nrf52_companion_radio_ble | nRF52 | SX1262 | 380568 | 380504 | -64 | +0 |
| Xiao_nrf52_companion_radio_usb | nRF52 | SX1262 | 374872 | 374808 | -64 | +0 |
| Xiao_nrf52_kiss_modem | nRF52 | SX1262 | 305840 | 305792 | -48 | +0 |
| Xiao_nrf52_repeater | nRF52 | SX1262 | 361800 | 361736 | -64 | +0 |
| Xiao_nrf52_room_server | nRF52 | SX1262 | 354264 | 354200 | -64 | +0 |
| Xiao_rp2040_companion_radio_usb | RP2040 | SX1262 | 240148 | 240148 | +0 | +0 |
| Xiao_rp2040_kiss_modem | RP2040 | SX1262 | 182708 | 182708 | +0 | +0 |
| Xiao_rp2040_repeater | RP2040 | SX1262 | 235796 | 235796 | +0 | +0 |
| Xiao_rp2040_room_server | RP2040 | SX1262 | 231740 | 231740 | +0 | +0 |
| Xiao_rp2040_terminal_chat | RP2040 | SX1262 | 212284 | 212284 | +0 | +0 |
| heltec_rc32_companion_radio_ble | ESP32 | SX1262 | 1281841 | 1281849 | +8 | +0 |
| heltec_rc32_companion_radio_usb | ESP32 | SX1262 | 676905 | 676917 | +12 | +0 |
| heltec_rc32_companion_radio_wifi | ESP32 | SX1262 | 1104789 | 1104789 | +0 | +0 |
| heltec_rc32_kiss_modem | ESP32 | SX1262 | 582197 | 582197 | +0 | +0 |
| heltec_rc32_repeater | ESP32 | SX1262 | 1169069 | 1169069 | +0 | +0 |
| heltec_rc32_repeater_bridge_espnow | ESP32 | SX1262 | 1179565 | 1179565 | +0 | +0 |
| heltec_rc32_room_server | ESP32 | SX1262 | 1164401 | 1164401 | +0 | +0 |
| heltec_rc32_sensor | ESP32 | SX1262 | 1175813 | 1175813 | +0 | +0 |
| heltec_rc32_terminal_chat | ESP32 | SX1262 | 603285 | 603233 | -52 | +0 |
| heltec_rc32_without_display_companion_radio_ble | ESP32 | SX1262 | 1276937 | 1276945 | +8 | +0 |
| heltec_rc32_without_display_companion_radio_usb | ESP32 | SX1262 | 671725 | 671737 | +12 | +0 |
| heltec_rc32_without_display_companion_radio_wifi | ESP32 | SX1262 | 1099669 | 1099669 | +0 | +0 |
| heltec_rc32_without_display_repeater | ESP32 | SX1262 | 1162169 | 1162169 | +0 | +0 |
| heltec_rc32_without_display_repeater_bridge_espnow | ESP32 | SX1262 | 1172545 | 1172545 | +0 | +0 |
| heltec_rc32_without_display_room_server | ESP32 | SX1262 | 1157585 | 1157585 | +0 | +0 |
| heltec_rc32_without_display_sensor | ESP32 | SX1262 | 1169053 | 1169053 | +0 | +0 |
| heltec_tracker_v2_companion_radio_ble | ESP32 | SX1262 | 1339277 | 1339285 | +8 | +0 |
| heltec_tracker_v2_companion_radio_usb | ESP32 | SX1262 | 712585 | 712597 | +12 | +0 |
| heltec_tracker_v2_companion_radio_wifi | ESP32 | SX1262 | 1159269 | 1159269 | +0 | +0 |
| heltec_tracker_v2_repeater | ESP32 | SX1262 | 1226257 | 1226257 | +0 | +0 |
| heltec_tracker_v2_repeater_bridge_espnow | ESP32 | SX1262 | 1236713 | 1236713 | +0 | +0 |
| heltec_tracker_v2_room_server | ESP32 | SX1262 | 1221505 | 1221505 | +0 | +0 |
| heltec_tracker_v2_sensor | ESP32 | SX1262 | 1232881 | 1232881 | +0 | +0 |
| heltec_tracker_v2_terminal_chat | ESP32 | SX1262 | 628193 | 628133 | -60 | +0 |
| heltec_v4_companion_radio_ble | ESP32 | SX1262 | 1669997 | 1669997 | +0 | +0 |
| heltec_v4_companion_radio_usb | ESP32 | SX1262 | 656597 | 656609 | +12 | +0 |
| heltec_v4_companion_radio_wifi | ESP32 | SX1262 | 1106165 | 1106165 | +0 | +0 |
| heltec_v4_expansionkit_repeater | ESP32 | SX1262 | 1173189 | 1173189 | +0 | +0 |
| heltec_v4_kiss_modem | ESP32 | SX1262 | 579645 | 579645 | +0 | +0 |
| heltec_v4_r8_companion_radio_ble | ESP32 | SX1262 | 1673481 | 1673481 | +0 | +0 |
| heltec_v4_r8_companion_radio_usb | ESP32 | SX1262 | 660077 | 660089 | +12 | +0 |
| heltec_v4_r8_companion_radio_wifi | ESP32 | SX1262 | 1109629 | 1109629 | +0 | +0 |
| heltec_v4_r8_kiss_modem | ESP32 | SX1262 | 583089 | 583089 | +0 | +0 |
| heltec_v4_r8_repeater | ESP32 | SX1262 | 1176629 | 1176629 | +0 | +0 |
| heltec_v4_r8_room_server | ESP32 | SX1262 | 1171913 | 1171913 | +0 | +0 |
| heltec_v4_r8_sensor | ESP32 | SX1262 | 1183245 | 1183245 | +0 | +0 |
| heltec_v4_r8_terminal_chat | ESP32 | SX1262 | 604189 | 604137 | -52 | +0 |
| heltec_v4_r8_tft_companion_radio_ble | ESP32 | SX1262 | 1290425 | 1290433 | +8 | +0 |
| heltec_v4_r8_tft_companion_radio_usb | ESP32 | SX1262 | 663853 | 663865 | +12 | +0 |
| heltec_v4_r8_tft_companion_radio_wifi | ESP32 | SX1262 | 1113457 | 1113457 | +0 | +0 |
| heltec_v4_r8_tft_kiss_modem | ESP32 | SX1262 | 583481 | 583481 | +0 | +0 |
| heltec_v4_r8_tft_repeater | ESP32 | SX1262 | 1178717 | 1178717 | +0 | +0 |
| heltec_v4_r8_tft_room_server | ESP32 | SX1262 | 1173901 | 1173901 | +0 | +0 |
| heltec_v4_r8_tft_sensor | ESP32 | SX1262 | 1185277 | 1185277 | +0 | +0 |
| heltec_v4_r8_tft_terminal_chat | ESP32 | SX1262 | 604577 | 604525 | -52 | +0 |
| heltec_v4_repeater | ESP32 | SX1262 | 1173137 | 1173137 | +0 | +0 |
| heltec_v4_repeater_bridge_espnow | ESP32 | SX1262 | 1183673 | 1183673 | +0 | +0 |
| heltec_v4_room_server | ESP32 | SX1262 | 1168421 | 1168421 | +0 | +0 |
| heltec_v4_sensor | ESP32 | SX1262 | 1179785 | 1179785 | +0 | +0 |
| heltec_v4_terminal_chat | ESP32 | SX1262 | 600737 | 600681 | -56 | +0 |
| heltec_v4_tft_companion_radio_ble | ESP32 | SX1262 | 1284809 | 1284817 | +8 | +0 |
| heltec_v4_tft_companion_radio_usb | ESP32 | SX1262 | 658273 | 658285 | +12 | +0 |
| heltec_v4_tft_companion_radio_wifi | ESP32 | SX1262 | 1107773 | 1107773 | +0 | +0 |
| heltec_v4_tft_repeater | ESP32 | SX1262 | 1174849 | 1174849 | +0 | +0 |
| heltec_v4_tft_repeater_bridge_espnow | ESP32 | SX1262 | 1185325 | 1185325 | +0 | +0 |
| heltec_v4_tft_room_server | ESP32 | SX1262 | 1170009 | 1170009 | +0 | +0 |
| heltec_v4_tft_sensor | ESP32 | SX1262 | 1181413 | 1181413 | +0 | +0 |
| heltec_v4_tft_terminal_chat | ESP32 | SX1262 | 600737 | 600681 | -56 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_companion_radio_ble | nRF52 | SX1262 | 398560 | 398496 | -64 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_companion_radio_usb | nRF52 | SX1262 | 391168 | 391120 | -48 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_rotated_companion_radio_ble | nRF52 | SX1262 | 398608 | 398560 | -48 | +0 |
| ikoka_handheld_nrf_e22_30dbm_096_rotated_companion_radio_usb | nRF52 | SX1262 | 391232 | 391168 | -64 | +0 |
| ikoka_handheld_nrf_e22_30dbm_repeater | nRF52 | SX1262 | 358296 | 358248 | -48 | +0 |
| ikoka_handheld_nrf_e22_30dbm_room_server | nRF52 | SX1262 | 351752 | 351688 | -64 | +0 |
| ikoka_handheld_nrf_kiss_modem | nRF52 | SX1262 | 304272 | 304224 | -48 | +0 |
| ikoka_nano_nrf_22dbm_companion_radio_ble | nRF52 | SX1262 | 387712 | 387648 | -64 | +0 |
| ikoka_nano_nrf_22dbm_companion_radio_usb | nRF52 | SX1262 | 380768 | 380704 | -64 | +0 |
| ikoka_nano_nrf_22dbm_kiss_modem | nRF52 | SX1262 | 305264 | 305216 | -48 | +0 |
| ikoka_nano_nrf_22dbm_repeater | nRF52 | SX1262 | 361176 | 361128 | -48 | +0 |
| ikoka_nano_nrf_22dbm_room_server | nRF52 | SX1262 | 353720 | 353656 | -64 | +0 |
| ikoka_nano_nrf_30dbm_companion_radio_ble | nRF52 | SX1262 | 387712 | 387648 | -64 | +0 |
| ikoka_nano_nrf_30dbm_companion_radio_usb | nRF52 | SX1262 | 380768 | 380704 | -64 | +0 |
| ikoka_nano_nrf_30dbm_kiss_modem | nRF52 | SX1262 | 305264 | 305216 | -48 | +0 |
| ikoka_nano_nrf_30dbm_repeater | nRF52 | SX1262 | 361176 | 361128 | -48 | +0 |
| ikoka_nano_nrf_30dbm_room_server | nRF52 | SX1262 | 353720 | 353656 | -64 | +0 |
| ikoka_nano_nrf_33dbm_companion_radio_ble | nRF52 | SX1262 | 387712 | 387648 | -64 | +0 |
| ikoka_nano_nrf_33dbm_companion_radio_usb | nRF52 | SX1262 | 380768 | 380704 | -64 | +0 |
| ikoka_nano_nrf_33dbm_kiss_modem | nRF52 | SX1262 | 305264 | 305216 | -48 | +0 |
| ikoka_nano_nrf_33dbm_repeater | nRF52 | SX1262 | 361176 | 361128 | -48 | +0 |
| ikoka_nano_nrf_33dbm_room_server | nRF52 | SX1262 | 353720 | 353656 | -64 | +0 |
| ikoka_stick_nrf_22dbm_companion_radio_ble | nRF52 | SX1262 | 398624 | 398560 | -64 | +0 |
| ikoka_stick_nrf_22dbm_companion_radio_usb | nRF52 | SX1262 | 391824 | 391776 | -48 | +0 |
| ikoka_stick_nrf_22dbm_kiss_modem | nRF52 | SX1262 | 318112 | 318048 | -64 | +0 |
| ikoka_stick_nrf_22dbm_repeater | nRF52 | SX1262 | 373768 | 373720 | -48 | +0 |
| ikoka_stick_nrf_22dbm_room_server | nRF52 | SX1262 | 366816 | 366768 | -48 | +0 |
| ikoka_stick_nrf_30dbm_companion_radio_ble | nRF52 | SX1262 | 398624 | 398560 | -64 | +0 |
| ikoka_stick_nrf_30dbm_companion_radio_usb | nRF52 | SX1262 | 391824 | 391776 | -48 | +0 |
| ikoka_stick_nrf_30dbm_kiss_modem | nRF52 | SX1262 | 318112 | 318048 | -64 | +0 |
| ikoka_stick_nrf_30dbm_repeater | nRF52 | SX1262 | 373768 | 373720 | -48 | +0 |
| ikoka_stick_nrf_30dbm_room_server | nRF52 | SX1262 | 366816 | 366768 | -48 | +0 |
| ikoka_stick_nrf_33dbm_companion_radio_ble | nRF52 | SX1262 | 398624 | 398560 | -64 | +0 |
| ikoka_stick_nrf_33dbm_companion_radio_usb | nRF52 | SX1262 | 391824 | 391776 | -48 | +0 |
| ikoka_stick_nrf_33dbm_kiss_modem | nRF52 | SX1262 | 318112 | 318048 | -64 | +0 |
| ikoka_stick_nrf_33dbm_repeater | nRF52 | SX1262 | 373768 | 373720 | -48 | +0 |
| ikoka_stick_nrf_33dbm_room_server | nRF52 | SX1262 | 366816 | 366768 | -48 | +0 |
| meshnology_w12_companion_radio_ble | ESP32 | LR2021 | 1291233 | 1291233 | +0 | +0 |
| meshnology_w12_companion_radio_usb | ESP32 | LR2021 | 666013 | 666013 | +0 | +0 |
| meshnology_w12_companion_radio_wifi | ESP32 | LR2021 | 1113313 | 1113313 | +0 | +0 |
| meshnology_w12_kiss_modem | ESP32 | LR2021 | 589353 | 589425 | +72 | +0 |
| meshnology_w12_repeater | ESP32 | LR2021 | 1183449 | 1183449 | +0 | +0 |
| meshnology_w12_repeater_bridge_espnow | ESP32 | LR2021 | 1193929 | 1193929 | +0 | +0 |
| meshnology_w12_room_server | ESP32 | LR2021 | 1178341 | 1178341 | +0 | +0 |
| meshnology_w12_sensor | ESP32 | LR2021 | 1189749 | 1189749 | +0 | +0 |
| meshnology_w12_terminal_chat | ESP32 | LR2021 | 610585 | 610521 | -64 | +0 |
| muzi_base_duo_companion_radio_ble | nRF52 | LR1121 | 327124 | 327124 | +0 | +0 |
| muzi_base_duo_companion_radio_usb | nRF52 | LR1121 | 322084 | 322084 | +0 | +0 |
| muzi_base_duo_repeater | nRF52 | LR1121 | 297316 | 297316 | +0 | +0 |
| muzi_base_duo_room_server | nRF52 | LR1121 | 290820 | 290804 | -16 | +0 |
| muzi_base_duo_superIO_companion_radio_ble | nRF52 | LR1121 | 346168 | 346168 | +0 | +0 |
| muzi_base_uno_companion_radio_ble | nRF52 | SX1262 | 331148 | 331100 | -48 | +0 |
| muzi_base_uno_companion_radio_usb | nRF52 | SX1262 | 326124 | 326060 | -64 | +0 |
| muzi_base_uno_repeater | nRF52 | SX1262 | 301236 | 301172 | -64 | +0 |
| muzi_base_uno_room_server | nRF52 | SX1262 | 294724 | 294660 | -64 | +0 |
| muzi_base_uno_superIO_companion_radio_ble | nRF52 | SX1262 | 350208 | 350160 | -48 | +0 |
| nibble_screen_connect_companion_radio_ble_ | ESP32 | SX1262 | 1211705 | 1211689 | -16 | +0 |
| nibble_screen_connect_companion_radio_usb_ | ESP32 | SX1262 | 586329 | 586313 | -16 | +0 |
| nibble_screen_connect_companion_radio_wifi_ | ESP32 | SX1262 | 1035369 | 1035365 | -4 | +0 |
| nibble_screen_connect_kiss_modem_ | ESP32 | SX1262 | 495625 | 495625 | +0 | +0 |
| nibble_screen_connect_repeater_ | ESP32 | SX1262 | 1103205 | 1103205 | +0 | +0 |
| nibble_screen_connect_repeater_bridge_espnow_ | ESP32 | SX1262 | 1113529 | 1113529 | +0 | +0 |
| nibble_screen_connect_room_server_ | ESP32 | SX1262 | 1098497 | 1098497 | +0 | +0 |
| nibble_screen_connect_terminal_chat_ | ESP32 | SX1262 | 523457 | 523405 | -52 | +0 |
| nibble_zero_connect_companion_radio_ble_ | ESP32 | SX1262 | 1211937 | 1211921 | -16 | +0 |
| nibble_zero_connect_companion_radio_usb_ | ESP32 | SX1262 | 586537 | 586521 | -16 | +0 |
| nibble_zero_connect_companion_radio_wifi_ | ESP32 | SX1262 | 1035601 | 1035597 | -4 | +0 |
| nibble_zero_connect_repeater_ | ESP32 | SX1262 | 1103309 | 1103309 | +0 | +0 |
| nibble_zero_connect_repeater_bridge_espnow_ | ESP32 | SX1262 | 1113613 | 1113613 | +0 | +0 |
| nibble_zero_connect_room_server_ | ESP32 | SX1262 | 1098589 | 1098589 | +0 | +0 |
| nibble_zero_connect_terminal_chat_ | ESP32 | SX1262 | 523537 | 523485 | -52 | +0 |
| t1000e_companion_radio_ble | nRF52 | LR1110 | 311644 | 311644 | +0 | +0 |
| t1000e_companion_radio_usb | nRF52 | LR1110 | 306020 | 306020 | +0 | +0 |
| t1000e_kiss_modem | nRF52 | LR1110 | 235436 | 235436 | +0 | +0 |
| t1000e_repeater | nRF52 | LR1110 | 294712 | 294712 | +0 | +0 |
| t1000e_room_server | nRF52 | LR1110 | 287564 | 287548 | -16 | +0 |
| waveshare_rp2040_lora_companion_radio_usb | RP2040 | SX1262 | 240940 | 240948 | +8 | +0 |
| waveshare_rp2040_lora_kiss_modem | RP2040 | SX1262 | 183500 | 183500 | +0 | +0 |
| waveshare_rp2040_lora_repeater | RP2040 | SX1262 | 236596 | 236596 | +0 | +0 |
| waveshare_rp2040_lora_repeater_bridge_rs232 | RP2040 | SX1262 | 241080 | 241080 | +0 | +0 |
| waveshare_rp2040_lora_room_server | RP2040 | SX1262 | 232548 | 232548 | +0 | +0 |
| waveshare_rp2040_lora_terminal_chat | RP2040 | SX1262 | 213084 | 213084 | +0 | +0 |
| wio-e5-mini_companion_radio_usb | STM32 | STM32WLx | 211296 | 211296 | +0 | +0 |
| wio-e5-mini_kiss_modem | STM32 | STM32WLx | 151208 | 151208 | +0 | +0 |
| wio-e5-mini_repeater | STM32 | STM32WLx | 204136 | 204136 | +0 | +0 |
| wio-e5-mini_sensor | STM32 | STM32WLx | 203664 | 203664 | +0 | +0 |
| wio-e5-repeater_bridge_rs232 | STM32 | STM32WLx | 199064 | 199064 | +0 | +0 |
| wio-e5_companion_radio_usb | STM32 | STM32WLx | 199968 | 199968 | +0 | +0 |
| wio-e5_kiss_modem | STM32 | STM32WLx | 143248 | 143248 | +0 | +0 |
| wio-e5_repeater | STM32 | STM32WLx | 196848 | 196848 | +0 | +0 |
| wio_wm1110_companion_radio_ble | nRF52 | LR1110 | 358792 | 358792 | +0 | +0 |
| wio_wm1110_kiss_modem | nRF52 | LR1110 | 289992 | 289992 | +0 | +0 |
| wio_wm1110_repeater | nRF52 | LR1110 | 345388 | 345388 | +0 | +0 |
| wio_wm1110_room_server | nRF52 | LR1110 | 338224 | 338224 | +0 | +0 |

</details>

### What was not verified on hardware

No board test for 9c. Every wrapper makes the same chip calls in the same order
as before (host suite, 80 cases, 16 mutations), and the firmware differences
are explained above down to the symbol. LLCC68 has no firmware environment and
is covered by the host suite only.

## Step 9d: one template for LR1110 and LR1121

Status: designed, not implemented yet. Behaviour change: none planned.

### Measured inventory

`tools/refactoring/scripts/sx126x_inventory.py` now takes a family as second
argument; `lr11x0` compares `CustomLR1110.h` and `CustomLR1121.h` the same way
as for 9b (comments and white space removed, chip name replaced by a
placeholder). On `integration` 07fa9c5c:

| Member | Lines (normalized) | Groups of identical copies |
|--------|--------------------|----------------------------|
| includes | 4 | LR1110, LR1121 |
| `_rxActivity`, `_rx_boosted`, constructor | 1 each | LR1110, LR1121 |
| `begin()` | 8 | LR1110, LR1121 |
| `getPacketLength()` | 8 | LR1110, LR1121 |
| `getFreqMHz()` | 1 | LR1110, LR1121 |
| `setRxBoostedGainMode()`, `getRxBoostedGainMode()` | 4, 1 | LR1110, LR1121 |
| `startReceive()` | 3 | LR1110, LR1121 |
| `isReceiving()` | 14 | LR1110, LR1121 |
| `setPreambleMillis()`, `setMaxPayloadMillis()` | 1 each | LR1110, LR1121 |
| `getSpreadingFactor()` | 1 | LR1110, LR1121 |
| `_txPower`, `_txPowerSet`, `using LR1121::setOutputPower`, `using LR1121::setFrequency` | 1 each | LR1121 only |
| `setOutputPower(int8_t)` | 15 | LR1121 only |
| `setFrequency(float)` | 5 | LR1121 only |

So every member of `CustomLR1110` has an identical copy in `CustomLR1121`, and
`CustomLR1121` adds the band dependent power handling: `setOutputPower()` clips
to the range of the power amplifier the band selects (and honours
`LR1121_FORCE_HP_PA`, set by `waveshare_esp32s3_lr1121`), remembers the power,
and `setFrequency()` applies it again after a band change.

One difference the placeholder hides: the debug line in `getPacketLength()`
names the chip (`"LR1110: got header err, calling standby()"` against
`"LR1121: ..."`). `MESH_DEBUG_PRINTLN` pastes its format into a `printf`
(`src/MeshCore.h:28`), so printing `Traits::name()` through `%s` gives the same
text, as in 9b.

In the tree, `RADIO_CLASS` is `CustomLR1110` in 38 environments (25 nRF52, 13
ESP32) and `CustomLR1121` in 10 (5 nRF52, 5 ESP32); every one of them uses the
matching wrapper. No class in `src`, `variants` or `examples` derives from either
chip class; they are named only as `RADIO_CLASS` and in `target.h`, and built by
copy initialization (`RADIO_CLASS radio = new Module(...)`), which needs the
constructor to stay non `explicit`.

### Template form

A new header `src/helpers/radiolib/CustomLR11x0.h`, shaped like
`CustomSX126x.h`:

```cpp
template <class Base, class Traits>
class CustomLR11x0 : public Base {
  RxActivityTracker _rxActivity;
  bool _rx_boosted = false;
public:
  explicit CustomLR11x0(Module* mod) : Base(mod) { }
  int16_t begin(/* the same parameters and defaults */);  // Base::begin, then setRegulatorDCDC() on success
  size_t getPacketLength(bool update) override;            // standby() after a header error, debug line with Traits::name()
  float getFreqMHz() const;
  int16_t setRxBoostedGainMode(bool en);
  bool getRxBoostedGainMode() const;
  int16_t startReceive() override;
  bool isReceiving();
  void setPreambleMillis(uint32_t);
  void setMaxPayloadMillis(uint32_t);
  uint8_t getSpreadingFactor() const;
};
```

The two chip headers keep their class as a thin subclass:

```cpp
struct CustomLR1110Traits {
  static const char* name() { return "LR1110"; }
};
class CustomLR1110 : public CustomLR11x0<LR1110, CustomLR1110Traits> {
public:
  CustomLR1110(Module* mod) : CustomLR11x0(mod) { }
};

struct CustomLR1121Traits {
  static const char* name() { return "LR1121"; }
};
class CustomLR1121 : public CustomLR11x0<LR1121, CustomLR1121Traits> {
  int8_t _txPower = 0;
  bool _txPowerSet = false;
public:
  CustomLR1121(Module* mod) : CustomLR11x0(mod) { }
  using LR1121::setOutputPower;
  using LR1121::setFrequency;
  int16_t setOutputPower(int8_t power) override;   // unchanged text
  int16_t setFrequency(float freq) override;       // unchanged text
};
```

The bodies move unchanged, with `this->` (or `Base::`) in front of the members of
the dependent base (`freqMHz`, `spreadingFactor`, `getIrqStatus()`,
`clearIrqState()`, `standby()`, `setRegulatorDCDC()`), C++11 as in 9b. Name
lookup stays as it was: the template's `begin()`, `getPacketLength(bool)` and
`setRxBoostedGainMode()` hide the RadioLib overloads exactly as the class
members did; `CustomLR1121` declares `setOutputPower` and `setFrequency` itself
with the same `using` lines, the template declares neither. `standby()` inside
`getPacketLength()` stays a virtual call, so an override in a subclass (#3638)
is reached as before.

Traits only where needed: the chip name is the only difference in the shared
part, so the traits struct holds nothing else.

### Exceptions, kept as they are

- **The power handling stays LR1121 only.** `CustomLR1110` uses RadioLib's
  `setOutputPower()` and `setFrequency()` as now. Giving LR1110 the clipping and
  the re-apply after a frequency change would change what LR1110 boards transmit
  after `set radio`, a behaviour change, and LR1110 has no band dependent PA
  choice in MeshCore today.
- **`LR1121_FORCE_HP_PA` keeps its name** and stays in the LR1121 subclass.
- `CustomLR11x0Wrapper.h`, `CustomLR1110Wrapper.h`, `CustomLR1121Wrapper.h`,
  `LR11x0Reset.h`, `RxActivityTracker.h` and the variants are untouched.
- `CustomLR2021` stays its own class (different RadioLib family `LR2021`, other
  IRQ names, no `begin()` or `getPacketLength()` override).

### Alternatives considered for 9d

- **The power handling in the template behind a trait** (`bandPower = false` for
  LR1110). Without `if constexpr` both branches must compile for both chips; they
  do, but the trait would hold two overrides and two members for one chip. The
  subclass is the place for chip only code, as `powerOff()` in 9c.
- **`using CustomLR1110 = CustomLR11x0<LR1110, ...>;`** Breaks a downstream
  forward declaration and leaves #3638's and #3261's LR1110 only additions no
  place. Rejected as in 9b and 9c.
- **No traits, the debug line prints "LR11x0".** Changes the debug output, a
  (small) behaviour change. Rejected.
- **A template parameter `const char*` for the name.** Needs a string with
  linkage per chip in C++11; a traits struct is shorter and matches 9b.
- **Merging into `CustomSX126x.h` style naming `CustomLR11x0<Base>` without
  traits, overriding `getPacketLength()` per chip for the name.** Leaves the eight
  lines of `getPacketLength()` twice, the copy the step is meant to remove.

### Upstream collisions (checked 22 October 2026)

All open pull requests on `meshcore-dev/MeshCore` (493) were listed with their
files; these touch `CustomLR1110.h` or `CustomLR1121.h`:

| PR | Touches | Collision with 9d |
|----|---------|-------------------|
| #3638 dual radio profiles | `CustomLR1110.h` +236 -2: profile switching members, overrides of `standby()`, `standby(uint8_t)`, `sleep()`, `reset()`, `stageMode()`, `setPreambleLength()`, `setFrequency()`, `invertIQ()`, `setModem()`, `getTimeOnAir()`, and a new `startReceive()` body; `begin()`, `getPacketLength()`, `isReceiving()` unchanged (the PR still has the pre 9a members) | Heavy textual, light semantic. Its members are LR1110 only and land in the thin `CustomLR1110` subclass; its `startReceive()` there overrides the template's, its `standby()` is reached from the template's `getPacketLength()` through the vtable as today. A hunk move, as in 9b. |
| #3261 stale SPI reply | `CustomLR1110.h` +41 -1: a status aware read in `getPacketLength()` and a helper | Semantic. After 9d `getPacketLength()` is shared: on rebase the PR either overrides it in `CustomLR1110` (LR1110 only, as it is written) or changes the template, which also gives LR1121 the fix. That choice is the PR's, and the template makes it one place instead of two. |
| #3496 KISS gain control | `setRxBoostedGainMode()` in both, the same 3 line change (store the flag only on success) | Textual; one edit in the template instead of two. |
| #2682 ThinkNode M4 | adds `begin()` to `CustomLR1110.h` (already in the tree) and creates `CustomLR1121.h` as a new file | Already conflicts with the tree; 9d changes nothing for it. |
| #2999, #2696, #2198 LR1121 boards | create `CustomLR1121.h` as a new file | As #2682. |

### Verification plan

- Characterization first, committed before the move and passing unchanged on
  both sides, in the `native_radio_chips` environment (no new environment):
  - `test/test_radio_chips`: a suite over both chips for `begin()` (the
    arguments reach RadioLib's `begin()`, `setRegulatorDCDC()` follows only on
    success, its result is returned), `getPacketLength()` (`standby()` only for
    length 0 with the header error bit, the length returned unchanged),
    `getFreqMHz()`, `getSpreadingFactor()` (the member), boosted gain (flag
    stored before the chip call, returned by the getter); and for the power
    handling: LR1121 clips through `checkOutputPower()` with `force_hp` false,
    remembers the requested (not the clipped) power only on success, applies it
    again after a successful `setFrequency()` and not before one was set; LR1110
    passes `setOutputPower()` and `setFrequency()` straight to RadioLib.
  - `test/test_radio_chips_init` (built with `MESH_DEBUG`): the header error
    debug line per chip, and with `LR1121_FORCE_HP_PA` the `force_hp` true path.
  - The LR11x0 stubs gain a logged `begin()` with RadioLib's defaults,
    `setRegulatorDCDC()`, a settable `getPacketLength(bool)`, `checkOutputPower()`
    with a clip value and `setOutputPower(int8_t, bool)` / `setFrequency()` with
    queued results.
- Mutation tests on the code commit (wrong trait name, DCDC also after a failed
  `begin()`, no `standby()` after a header error, power stored when the chip
  fails, LR1121 power handling moved to LR1110, `force_hp` ignored, and so on).
- Build matrix: all 48 environments with `CustomLR1110` or `CustomLR1121`, plus
  controls per platform that do not include the two headers
  (`RAK_4631_repeater`, `Heltec_v3_repeater`, `waveshare_rp2040_lora_repeater`,
  `wio-e5-mini_repeater`, `Heltec_v2_repeater`, `MeshTracker_X1_repeater`,
  `meshnology_w12_repeater`), on `integration` 07fa9c5c and on the code commit in
  the same worktree, with `SOURCE_DATE_EPOCH=1792540800`; flash and RAM per
  environment.
- Image comparison of a sample keeping the ELF files, `nm -S -C` symbol by
  symbol with the template name folded back, and disassembly where a size
  changes.
- All native test environments.
