# Sync upstream/dev into integration (04.11.2026)

Merge of `upstream/dev` 26079dea (41 commits over the old base 46216b2d) into
`integration` 03a1f43d, branch `sync/upstream-dev-20261104`.

## What upstream brought

- #3715 (`f688d99b`) the RadioLib receive error counter is initialised
- #3739 (`c79500e6`) packets whose LoRa header says "no CRC" are dropped
- b0bacf8a LR2021 goes to standby before `setParams()` reconfigures it
- #3615 TX power is read and written as `int8_t` in `CommonRadioPrefs`
- #3682 scope 0 rules: a transport flood with scope code 0 counts as unscoped
- #3639 (`060fd219`) empty anonymous contact slots are skipped in lookups
- #3688 duplicate packet statistics saturate instead of wrapping
- #3728 (`017db61c`) `sensor list` no longer overflows the reply buffer
- #3687 (`5d7100ae`) display preferences in the companion CLI (`set ui.*`)
- #3603 companion buzzer CLI (`get/set buzzer`), T1000-E specific code removed
- #3702, #3703 T-Echo e-ink driver, unused `buttonStateChanged` removed
- #3599 SenseCAP Solar P1 Pro status LED, f99ea79b ThinkNode M8 sensor helpers
- #3686 the ESP32 RTC backup (our note 0012), RadioLib 7.8.1

## Conflicts and their resolution

Eleven files, each resolved on its own.

| File | Conflict | Resolution | Why |
|---|---|---|---|
| `src/helpers/radiolib/Custom{SX1262,SX1268,LLCC68,STM32WLx,LR1110,LR1121,LR2021}Wrapper.h` | upstream added `lastRxHadCRC()` to every wrapper (7 copies) and rewrote LR2021 `setParams()`; integration moved the common calls into `CustomChipWrapper` and the family templates (9b to 9d) | integration side for all seven files. `lastRxHadCRC()` is defined once in `CustomChipWrapper<Chip>`; `CustomSX1276Wrapper` overrides it back to `true`; the LR2021 standby fix is applied inside `CustomLR2021Wrapper::setParams()` | one copy instead of seven. SX1276 keeps the default because upstream never gave it the override. RadioLib 7.8.1 does have `SX127x::getLoRaRxHeaderInfo()`, so checking SX1276 too is possible but is a behaviour change nobody asked for |
| `src/helpers/CommonRadioPrefs.h` | upstream changes the virtual `getTxPower()`/`setTxPower()` to `int8_t`; integration has non-virtual accessors on `RadioPrefsFields` | integration side with `int8_t` | the settings table already stores `tx` as `FIELD_I8`; only the accessor type changes |
| `src/helpers/CommonCLI.h` | upstream's `int8_t` edit sits inside the `NodePrefs` class that integration moved out of this header | integration side (the block stays removed) | same change as above, already applied through `CommonRadioPrefs.h` |
| `examples/companion_radio/NodePrefs.h` | upstream's `int8_t` edit sits inside the radio inner class that step 1 replaced by `CommonRadioPrefs`/`RadioPrefsFields` | integration side for that hunk. Upstream's `ui_*` fields, the `UI_Prefs` serializer and its entry in `structure()` merged cleanly and are kept | |
| `src/helpers/RegionMap.cpp` | upstream's one-liner for scope 0 against the same fix already in integration, with a comment | integration side | identical logic: `(wildcard.flags & mask) == 0 ? &wildcard : NULL` |

Merged without conflict and checked by reading: `ESP32Board.{h,cpp}` (identical to
integration, upstream took the same fix as note 0012), `BaseChatMesh.cpp`,
`ContactInfo.h`, `RoutingPolicy.h`, `CommonCLI.cpp` (`sensor list`), the
`set ui.*`/`set buzzer` commands in `examples/companion_radio/MyMesh.cpp` (they sit in
the text CLI `handleCommand()`, which `CMD_TABLE` does not cover), the display and
variant files.

One detail of the LR2021 merge: upstream logged the status of each of the four modem
setters. `setModemParams()` calls them without checking, so those four debug lines are
gone (the `standby()` and `applySideDetectorConfig()` lines stay).

## Test changes

Upstream behaviour that changed what the host tests pinned:

- `test/mocks/RadioPrefsChecks.h`: `get tx` after `set tx -5` printed `> 251`
  (marked "odd" in the test); with the `int8_t` getter it prints `> -5`.
- `test/mocks/ForwardPolicyChecks.h`: the scoped packet helper sets a non-zero scope
  code, as upstream did in `test_routing_policy`. New test
  `AScopeOfZeroCountsAsUnscoped`.
- `test/test_companion_frames`: a zero public key no longer names one of the eight
  reserved anonymous slots (`isEmptySlot()`), so `GET_CONTACT_BY_KEY` and
  `REMOVE_CONTACT` with zeros answer `ERR_NOT_FOUND`. The test is renamed
  `AZeroKeyNamesNoContact` and `frame_sweep.golden` is regenerated: every changed
  line is a probe with a zero key, from the old answer to `0102`. This retires the
  quirk recorded in note 0010.
- `test/test_empty_contact_slots` (new from upstream) defined its own
  `StrHelper::strncpy`; our `native` environment already links the real one, so
  the copy is removed.
- New for the CRC check: `RecvFixture` in `test_radio_wrappers` (packet with CRC is
  delivered, packet without CRC is dropped and counted, a failed read is counted,
  `resetStats()` clears the counter), `LastRxHadCRC` in `test_radio_chip_wrappers`
  for all eight wrappers (SX1276 stays `true`), and the LR2021 `standby()` first in
  `SetParamsCallsTheChipInOrder`. The mocks got `read_result`,
  `getLoRaRxHeaderInfo()` and `RADIOLIB_ERR_LORA_HEADER_DAMAGED` (-24, from RadioLib
  7.8.1 `TypeDef.h`).

## Checks

All on the merge result in this worktree.

- Native: all 27 `native*` environments, 901 test cases, 0 failed, 1 skipped
  (run 3 of 3; runs 1 and 2 found the test adjustments above).
- Firmware: 101 environments, 99 build, 2 fail. The set: every non-KISS environment
  of the variants this sync or the last one touched (ThinkNode M1, M3, M4, M5, M8,
  MeshTracker X1, ProMicro, SenseCAP Solar, T1000-E, T-Echo, RAK4631, Heltec V4 TFT,
  T-Beam Supreme SX1262), the KISS modems that failed before (E213, E290, T190,
  TBeam 1W, Station G3, Heltec Tracker V2) and per-chip controls (SX1276, SX1268,
  LR1121, STM32WL, LR1110 (WM1110), Heltec V3, T114, Pico W, C6, Station G2).
  Not the same 73 as last time (that list was not kept).
  - Fixed by upstream f99ea79b: `ThinkNode_M8_repeater`, `_room_server`, `_kiss_modem`.
  - Still red, same as on the last sync:
    - `ThinkNode_M5_companion_radio_serial`: `D7` not declared
    - `Station_G2_companion_radio_ble`: 1323541 bytes, limit 1310720 (last sync
      1320869 on the merge)
  - No LR2021 firmware environment exists in the tree; LR2021 is covered by the host
    tests only.

## Open risks

- No board run.
- `sensor list` overflow (`017db61c`), `set buzzer` and `set ui.*` have no host test
  here: they sit in code the refactor did not move and the host harness defines
  neither `PIN_BUZZER` nor `DISPLAY_CLASS`. They merged without conflict and compile
  in the matrix.
- The two red environments were not compared against `integration` in this run; they
  are the same two reds as at the last sync, by name, not by measurement.
- Flash and RAM were not compared with `integration` this time.
- SX1276 still accepts packets without a payload CRC (see the first conflict row).
