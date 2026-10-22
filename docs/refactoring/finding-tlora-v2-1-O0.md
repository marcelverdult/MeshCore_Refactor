# Finding: LilyGo T-Lora V2.1 1.6 builds without optimization

Status: fix on branch `fix/tlora-v2-1-optimization` (based on `upstream/dev`
02c99229). Behaviour change: yes, different code generation. Not part of the
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

1. Verbose build (`pio run -e LilyGo_TLora_V2_1_1_6_repeater -v`): 287 compile
   lines, none contains an `-O` option. A sample:
   `xtensa-esp32-elf-g++ -o .../src/Mesh.cpp.o -c ... -ffunction-sections -fdata-sections -mlongcalls ... -ggdb ...`
2. `DW_AT_producer` of `src/Mesh.cpp.o` lists the options without `-O`.
3. After the fix, `compile_commands.json` shows `-Os` for all 287 units and
   `DW_AT_producer` contains `-Os`.

Precompiled ESP-IDF libraries in the image are not affected, they carry their
own optimization.

## Size, upstream/dev 02c99229, all 9 envs build (rc 0)

Static RAM and flash as printed by `pio run`, app partition `min_spiffs.csv`
(1966080 bytes).

| Env | RAM before | RAM after | Diff | Flash before | Flash after | Diff |
|---|---|---|---|---|---|---|
| companion_radio_ble | 106052 | 106004 | -48 | 1916361 | 1478897 | -437464 (-22.8 %) |
| companion_radio_usb | 68520 | 68472 | -48 | 991605 | 640789 | -350816 (-35.4 %) |
| companion_radio_wifi | 111900 | 111836 | -64 | 1519529 | 1133005 | -386524 (-25.4 %) |
| kiss_modem | 27640 | 27592 | -48 | 861109 | 580929 | -280180 (-32.5 %) |
| repeater | 62472 | 62420 | -52 | 1663713 | 1205269 | -458444 (-27.6 %) |
| repeater_bridge_espnow | 64152 | 64100 | -52 | 1677601 | 1215957 | -461644 (-27.5 %) |
| repeater_bridge_rs232 | 64056 | 64004 | -52 | 1669041 | 1207653 | -461388 (-27.6 %) |
| room_server | 66160 | 66108 | -52 | 1642001 | 1200645 | -441356 (-26.9 %) |
| terminal_chat | 63656 | 63608 | -48 | 904829 | 601721 | -303108 (-33.5 %) |

`companion_radio_ble` drops from 97.5 % to 75.2 % of the app partition.

## Stack

PR #62 reports crashes while signing, solved with
`ARDUINO_LOOP_STACK_SIZE=16384`. Stack frames from `-fstack-usage` on the
ed25519 library, same compile line (from the `main` 81dbae59 build) with and without `-Os`:

| Function | -O0 | -Os |
|---|---|---|
| `fe_mul` | 3632 | 192 |
| `fe_sq` | 2144 | 144 |
| `ed25519_sign` | 576 | 544 |
| `ge_scalarmult_base` | 528 | 512 |

Assumption, not verified: the crash in PR #62 came from `-O0`. The fix keeps
the 16 KB loop stack, so stack headroom only grows.

## Not verified

No hardware test. The fix changes the generated code of the whole firmware,
so a boot, advert, receive and send check on a T-Lora V2.1 1.6 is needed
before an upstream PR.
