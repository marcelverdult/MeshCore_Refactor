# Finding: LilyGo T-Lora V2.1 1.6 builds without optimization

Status: fix on branch `upstream-candidate/tlora-v2-1-optimization` (based on
`upstream/dev` 46216b2d). Behaviour change: yes, different code generation. Not part of the
behaviour neutral refactoring series. Upstream PR candidate, not submitted.

## What happens

`variants/lilygo_tlora_v2_1/platformio.ini`, section `[LilyGo_TLora_V2_1_1_6]`:

```ini
build_unflags = -Os
build_flags =
  ...
  -Os -ffunction-sections -fdata-sections  ; Optimize for size
```

PlatformIO applies `build_unflags` after merging all flags, so it removes every
`-Os`: the one from the Arduino ESP32 framework
(`framework-arduinoespressif32/tools/platformio-build-esp32.py`, `CCFLAGS`) and
the one the variant adds itself. No other `-O` is left, so GCC uses its default
`-O0`. All 9 `LilyGo_TLora_V2_1_1_6_*` envs inherit this.

The lines came with upstream PR #62 (commit e1022791, March 2025), whose
message says "Configure build environment for optimizing firmware size". The
effect is the opposite.

## Evidence

Measured on `upstream/dev` 46216b2d before and after the fix, PlatformIO 6.2.0.

1. `compile_commands.json` (`pio run -e <env> -t compiledb`) of all 9 envs:
   before, none of the 269 to 305 units per env has any `-O` option; after,
   every unit has `-Os`.
2. `DW_AT_producer` of `src/Mesh.cpp.o` (`LilyGo_TLora_V2_1_1_6_repeater`):
   before `GNU C++11 8.4.0 -mlongcalls -ggdb -std=gnu++11 ...`, after
   `GNU C++11 8.4.0 -mlongcalls -ggdb -Os -std=gnu++11 ...`.

`pio run -v` cannot be used for this with PlatformIO 6.2.0: it stops at the
`firmware.bin` step with a SCons `TypeError` while printing the command line
(before and after alike).

Precompiled ESP-IDF libraries in the image are not affected, they carry their
own optimization.

## Size, upstream/dev 46216b2d, all 9 envs build (rc 0)

Static RAM and flash as printed by `pio run`, app partition `min_spiffs.csv`
(1966080 bytes).

| Env | units | -O before | -Os after | RAM before | RAM after | Diff | Flash before | Flash after | Diff |
|---|---|---|---|---|---|---|---|---|---|
| companion_radio_ble | 304 | 0 | 304 | 106052 | 106004 | -48 | 1916869 | 1479185 | -437684 (-22.8 %) |
| companion_radio_usb | 301 | 0 | 301 | 68528 | 68480 | -48 | 992109 | 641081 | -351028 (-35.4 %) |
| companion_radio_wifi | 305 | 0 | 305 | 111908 | 111844 | -64 | 1520009 | 1133309 | -386700 (-25.4 %) |
| kiss_modem | 270 | 0 | 270 | 27640 | 27592 | -48 | 861157 | 580953 | -280204 (-32.5 %) |
| repeater | 287 | 0 | 287 | 62472 | 62420 | -52 | 1663953 | 1205433 | -458520 (-27.6 %) |
| repeater_bridge_espnow | 288 | 0 | 288 | 64152 | 64100 | -52 | 1677837 | 1216121 | -461716 (-27.5 %) |
| repeater_bridge_rs232 | 288 | 0 | 288 | 64056 | 64004 | -52 | 1669305 | 1207785 | -461520 (-27.6 %) |
| room_server | 287 | 0 | 287 | 66160 | 66108 | -52 | 1642233 | 1200813 | -441420 (-26.9 %) |
| terminal_chat | 269 | 0 | 269 | 63656 | 63608 | -48 | 904873 | 601761 | -303112 (-33.5 %) |

`companion_radio_ble` drops from 97.5 % to 75.2 % of the app partition.

## Stack

PR #62 reports crashes while signing, solved with
`ARDUINO_LOOP_STACK_SIZE=16384`. Stack frames from `-fstack-usage` on the
ed25519 library, same compile line (from `compile_commands.json` of the `_repeater` env on 46216b2d) with and without `-Os`:

| Function | -O0 | -Os |
|---|---|---|
| `fe_mul` | 3632 | 192 |
| `fe_sq` | 2144 | 144 |
| `ed25519_sign` | 576 | 544 |
| `ge_scalarmult_base` | 528 | 512 |

Assumption, not verified: the crash in PR #62 came from `-O0`. The fix keeps
the 16 KB loop stack, so stack headroom only grows.

## Not verified

No hardware test: no T-Lora V2.1 1.6 in the device pool (26.10.2026). The fix changes the generated code of the whole firmware,
so a boot, advert, receive and send check on a T-Lora V2.1 1.6 is needed
before an upstream PR.
