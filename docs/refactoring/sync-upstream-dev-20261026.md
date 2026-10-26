# Sync upstream/dev into integration (26.10.2026)

Merge of `upstream/dev` 46216b2d (23 commits over the old base 428a95c6) into
`integration` d7c52276, branch `sync/upstream-dev-20261026`, merge commit
e3b9235c.

## What upstream brought

- #3651 auto-detect display (SH1106/SSD1306, `AutoDetectDisplay`, RAK4631),
  vtable alignment fix in `DisplayDriver`
- #3659, #3680 ThinkNode M1/M5 e-ink rework, backlight button on T-Echo and
  ThinkNode M1/M5/M8, WiFi for the M5 USB companion
- #3614, #3627 WiFi on Heltec V4 TFT BLE and T-Beam Supreme BLE companion
- #3667 KISS modem: `setTxBufferSize()` only for HWCDC
- #3617 quarter-hour time zone offsets
- #3643, #3646 our own fixes (negative group text length, "nan" for the
  airtime factor), already in integration in their refactored form

## Conflicts and their resolution

Nine files, each resolved on its own.

| File | Conflict | Resolution | Why |
|---|---|---|---|
| `examples/companion_radio/MyMesh.cpp` | upstream #3643 adds `len >= 7` to the old if chain of `handleCmdFrame()`; integration replaced that chain with `CMD_TABLE` (10b/10c) | integration side for the hunk; upstream's tz.offset change (#3617) outside the hunk merged as is | `CMD_TABLE` already has the row `{ CMD_SEND_CHANNEL_TXT_MSG, 7, ... }`, and `BaseChatMesh::sendGroupMessage()` got the identical `text_len < 0` guard from both sides (merged cleanly, present once) |
| `src/helpers/CommonRadioPrefs.cpp` (4 hunks) | upstream took #3646 as `looksLikeANumber()` in `set af` and `set dutycycle`; integration has the settings table (step 3) with `spellsNanOrInf()`/`inRange()` | integration side, file identical to integration | Measured on the host, 27 inputs (nan/inf spellings, signs, white space, garbage, empty, `1e40`, `0x3`, `5abc`), with and without `-ffast-math`: both versions reject exactly the same inputs for `af` and `dutycycle` (0 differences). Integration also checks `rxdelay`, `txdelay` and `direct.txdelay`, which upstream leaves unchecked. |
| `variants/lilygo_techo/target.{cpp,h}`, `variants/thinknode_m1/target.{cpp,h}`, `variants/thinknode_m5/target.{cpp,h}`, `variants/thinknode_m8/target.h` | upstream adds `backlight_btn` next to `display` and `user_btn` (for T-Echo and M1 the upstream side of the hunk still holds `radio_init()`/`radio_new_identity()`, which integration moved to the shared weak definitions) | keep `DISPLAY_TARGET_GLOBALS` (display, user_btn from `VariantTarget.cpp`) and `RADIOLIB_TARGET_GLOBALS`; `backlight_btn` becomes a variant-local global: `extern` in `target.h`, defined in `target.cpp` under `#ifdef DISPLAY_CLASS` | `backlight_btn` only exists on these four boards, so it does not belong in the shared opt-in globals. Upstream's own M8 `target.cpp` (merged cleanly) defines it the same way. |

Merged without conflict and checked by reading: `variants/rak4631/target.h`
(`AutoDetectDisplay.h` instead of `SSD1306Display.h`), `variants/thinknode_m8/target.cpp`,
`examples/kiss_modem/main.cpp`, all `platformio.ini` changes.

## Checks

All on the merge commit e3b9235c, comparison builds on origin/integration
d7c52276 in the same worktree.

- Native: all 27 `native*` environments of `platformio.ini`, 881 test cases,
  880 passed, 1 skipped, 0 failed.
- Firmware: 73 environments, namely every environment of the seven variants
  upstream touched (Heltec V4, T-Beam Supreme SX1262, T-Echo, RAK4631,
  ThinkNode M1, M5, M8), the six ESP32-S3 KISS modems that failed with
  `setTxBufferSize`, and controls per platform (Heltec V3 repeater and BLE
  companion, Heltec T114 BLE companion, PicoW repeater, wio-e5-mini repeater,
  LilyGo T-Lora C6 repeater, Station G2 BLE companion).
  - Merge: 68 of 73 build. origin/integration: 62 of 73.
  - Fixed by upstream #3667: `Heltec_E213_kiss_modem`, `Heltec_E290_kiss_modem`,
    `Heltec_T190_kiss_modem`, `LilyGo_TBeam_1W_kiss_modem`,
    `Station_G3_ESP32_kiss_modem`, `heltec_tracker_v2_kiss_modem` (all failed on
    origin/integration with `USBCDC` has no `setTxBufferSize`).
  - Failing on both sides with the same error, so not caused by the merge:
    - `ThinkNode_M8_repeater`, `ThinkNode_M8_room_server`, `ThinkNode_M8_kiss_modem`:
      undefined `EnvironmentSensorManager`, the M8 base `build_src_filter` (also in
      upstream/dev) does not add `helpers/sensors/*.cpp`
    - `ThinkNode_M5_companion_radio_serial`: `D6`/`D7` not declared
    - `Station_G2_companion_radio_ble`: 1320733 bytes on origin/integration,
      1320869 on the merge, limit 1310720
- Flash/RAM, merge minus origin/integration, for the 62 environments that build
  on both sides (read as: the larger steps line up with the upstream features below).
  - WiFi now enabled: `heltec_v4_tft_companion_radio_ble` +386916/+22848,
    `T_Beam_S3_Supreme_SX1262_companion_radio_ble` +386388/+22848,
    `ThinkNode_M5_companion_radio_usb` +425604/+26668
  - RAK4631 (auto-detect display): +3504 to +4432 flash, +120/+128 RAM
  - ThinkNode M1 companion: -688/-752 flash, +48 RAM (e-ink rework)
  - all others: -0 to +864 flash, +0 to +48 RAM

## Open risks

- No board run. Most at stake: the backlight button and e-ink hibernate on
  T-Echo and ThinkNode M1/M5/M8 (from upstream, wired into the refactored
  targets here), and the auto-detect display on RAK4631.
- The size deltas were not measured against upstream/dev itself, so the claim
  that the merge adds nothing of its own on top of upstream rests on reading the
  resolutions, not on a measurement.
- The three pre-existing failures (M8 non-companion envs, M5 serial companion,
  Station G2 size) stay open. They are not caused by this merge; only the M8
  one is shown to exist in upstream/dev as well (by reading its `platformio.ini`),
  the other two were not built on upstream/dev.
