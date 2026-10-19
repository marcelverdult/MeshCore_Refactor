# Fix: ESPNOWRadio::setRxBoostedGainMode returns no value

Status: ready for review. Behaviour change: none observable today, see Reach below.

## Context

`src/helpers/esp32/ESPNOWRadio.h:41` declares a `bool` and never returns one:

```cpp
virtual bool setRxBoostedGainMode(bool) { }
```

Flowing off the end of a value returning function is undefined behaviour. The
comment above it says both functions "do nothing for ESP-NOW", which is the right
intent, but the neighbour on the next line gets it right (`getRxBoostedGainMode()
const { return false; }`) and so does the same no-op in the LoRa base class,
`RadioLibWrappers.h:78`.

The value is read. `src/helpers/CommonRadioPrefs.cpp:126` handles the CLI command
`set radio.rxgain on|off` and decides the reply from it:

```cpp
if (radio_driver.setRxBoostedGainMode(enabled)) {
  strcpy(reply, "OK");
} else {
  strcpy(reply, "Error: unsupported");
}
```

`mesh::Radio` (`src/Dispatcher.h:22`) does not declare `setRxBoostedGainMode`, so
this call binds statically to whichever type `radio_driver` has in the variant.
Two variants give it the ESP-NOW driver:

- `variants/generic_espnow/target.cpp:7`
- `variants/sensecap_indicator-espnow/target.cpp:7`

On those, `set radio.rxgain on` answers from whatever happens to be in the return
register. Three other callers ignore the result, so they are unaffected in
practice: `examples/companion_radio/MyMesh.cpp:1007`,
`examples/simple_repeater/MyMesh.cpp:983`,
`examples/simple_room_server/MyMesh.cpp:727`.

## How it got there

Commit 1f9bb674 (26 June 2026, "return bool when setting rx boost") changed the
return type across the radio drivers. In this file it changed exactly one line:

```
-  virtual void setRxBoostedGainMode(bool) { }
+  virtual bool setRxBoostedGainMode(bool) { }
```

The signature became a `bool` and the empty body stayed empty. The same commit
gave the other drivers a real return value, and `CommonCLI.h` the branch that
reads it. So this is not an oversight in a new file, it is one line of a sweep
that the compiler could not flag, because the framework builds project sources
with `-w` (measured under Verification).

## Decision

Return `false`, character for character the same as the no-op in
`RadioLibWrappers.h:78`:

```cpp
virtual bool setRxBoostedGainMode(bool) { return false; }
```

`false` is the truthful answer: ESP-NOW has no boosted gain mode to switch on, so
`set radio.rxgain on` should report `Error: unsupported` rather than claim success.

## Alternatives considered

- **Return `true`.** It would silence the compiler just as well and is what an
  author optimising for a quiet CLI might pick. Rejected: it would report success
  for something that did not happen. Note that no compile check can tell these two
  apart, see the gap in the mutation table below.
- **Make the function `void`.** Honest about doing nothing, but then the caller in
  `CommonRadioPrefs.cpp` no longer compiles (mutation 3 below shows exactly that
  error), so it would have to grow a driver specific branch. More code for no gain.
- **Declare the pair in `mesh::Radio` and give the base a `false` default.** This
  is the real cure for the underlying shape, since every driver currently invents
  the same two functions. Rejected here: it touches the central radio interface
  and every driver, which is a refactoring step and not a fix for undefined
  behaviour in one line.

## Reach: compiled widely, emitted nowhere, reachable in nothing that builds

The file is compiled in many environments, the changed function reaches no image,
and the one call site that reads its value belongs to two variants that do not
build. Each of those three steps is measured below, because the easy version of
this claim is wrong: unchanged binary sizes do not mean a file was not compiled.

1. **The two ESP-NOW-only variants do not compile.**
   `src/helpers/ESP32Board.h:73` returns `P_LORA_DIO_1` from `getIRQGpio()`
   unconditionally, and `src/helpers/ESP32Board.cpp:62` uses `P_LORA_NSS`. Neither
   variant defines any `P_LORA_*` pin, because neither has a LoRa radio. Measured
   on unmodified `upstream/dev` 428a95c6:

   ```
   Generic_ESPNOW_repeatr:                   1 failed, 0 succeeded
   SenseCapIndicator-ESPNow_comp_radio_usb:  1 failed, 0 succeeded
   both: src/helpers/ESP32Board.h:73:12: error: 'P_LORA_DIO_1' was not declared in this scope
   ```

2. **`ESPNOWRadio.cpp` does not compile against ESP-IDF 5.x.** On the C6 platform
   the ESP-NOW receive callback changed shape:

   ```
   src/helpers/esp32/ESPNOWRadio.cpp:40:28: error: invalid conversion from
     'void (*)(const uint8_t*, const uint8_t*, int)' to 'esp_now_recv_cb_t'
     {aka 'void (*)(const esp_now_recv_info*, const unsigned char*, int)'}
   src/helpers/esp32/ESPNOWRadio.cpp:68:3: error: 'esp_efuse_mac_get_default' was not declared in this scope
   ```

   That is why every C6 environment that pulls in `helpers/esp32/*.cpp` excludes
   this one file again: `variants/lilygo_tlora_c6/platformio.ini:82`,
   `variants/m5stack_unit_c6l/platformio.ini:86` and `:104`,
   `variants/xiao_c6/platformio.ini:66`, `:121` and `:183`.

`arduino_base.build_src_filter` matches `helpers/*.cpp` and does not recurse, so
an environment picks the file up only if it asks for `helpers/esp32/*.cpp`. 39
variant files do exactly that, so the file is compiled in a great many ESP32
environments, two of them inside this fix's own build matrix:

```
.pio/build/Heltec_v3_companion_radio_ble/src/helpers/esp32/ESPNOWRadio.cpp.o
.pio/build/Heltec_v3_companion_radio_wifi/src/helpers/esp32/ESPNOWRadio.cpp.o
```

What those builds do not contain is the function. It is an inline member, and in
those variants `radio_driver` is a LoRa wrapper, so nothing calls the ESP-NOW one
and the linker never emits it. `xtensa-esp32-elf-nm` on the finished
`Heltec_v3_companion_radio_ble` image finds no `ESPNOWRadio` symbol at all, and
the only boosted gain symbols present belong to the SX1262 path:

```
ESPNOWRadio symbols in the image: 0
42009d04 W _ZN19CustomSX1262Wrapper20setRxBoostedGainModeEb
42015c28 T _ZN6SX126x20setRxBoostedGainModeEbb
```

That, and not an absent compile, is why both environments are byte identical with
and without this change (1666437 / 194836 and 1102677 / 172376 on each side).

The one call site that reads the value, `CommonRadioPrefs.cpp:126`, binds to
`ESPNOWRadio` only where `radio_driver` is one, which is the two ESP-NOW-only
variants, and those do not build. So the defect is undefined behaviour that sits
in many translation units, reaches no image, and is reachable in nothing that
builds today.

It is still worth fixing: the file is the ESP-NOW driver, whoever repairs the two
defects above should not inherit this one, and until then every ESP32 companion
build carries a translation unit the standard calls undefined.

## Compatibility

None affected. The two environments of the build matrix that compile the file are
byte identical with and without the change, because the inline member is never
called there and so never emitted; the other 16 do not compile it at all. Both
facts are measured under Reach, and the matrix figures are under Verification.

## Verification

### Compile check

Two obstacles had to be cleared to get a deterministic signal, and both are
interesting in their own right.

The framework compiles project sources with `-w`, which suppresses every warning.
Read from the generated `compile_commands.json` for `ESPNOWRadio.cpp`:

```
-w -Wno-error=unused-function -Wno-error=unused-variable ... -Wwrite-strings
```

A later `-Werror=return-type` does not win against it. Measured with the exact
target compiler on the reduced construct:

```
riscv32-esp-elf-g++ -w -Werror=return-type   -> rc=0, silent
riscv32-esp-elf-g++    -Werror=return-type   -> rc=1, error: no return statement
                                                in function returning non-void
```

So this defect was invisible to every build that ever ran, by configuration.

The diagnostic patch `tools/refactoring/diag-espnow-rxgain.patch` therefore does
three things to `variants/generic_espnow/platformio.ini`: it defines the two LoRa
pins that `ESP32Board` needs, drops `-w` through `build_unflags`, and promotes
`return-type` to an error. It is a test aid and part of no fix.

Before the fix, `Generic_ESPNOW_repeatr` with that patch:

```
src/helpers/esp32/ESPNOWRadio.h:41:45: error: no return statement in function returning non-void [-Werror=return-type]
==================== 1 failed, 0 succeeded in 00:00:20.442 ====================
```

After the fix, same patch, same environment:

```
========================= 1 succeeded in 00:00:40.118 =========================
```

with no `return-type` diagnostic left anywhere in the build, and no other error
surfacing from dropping `-w` in that environment.

### Mutation tests

Five deliberate errors, each built with the diagnostic patch.

| # | Mutation | Result |
|---|----------|--------|
| 1 | no return at all (the original defect) | caught: `error: no return statement in function returning non-void` |
| 2 | `return true;` | **not caught**, see below |
| 3 | return type changed to `void` | caught: `error: could not convert 'radio_driver.ESPNOWRadio::setRxBoostedGainMode(...)'` |
| 4 | function removed | caught: `error: 'class ESPNOWRadio' has no member named 'setRxBoostedGainMode'` |
| 5 | `if (false) return false;` | caught: `error: control reaches end of non-void function` |

Mutations 3 and 4 are worth more than their pass marks: they prove by measurement
that `CommonRadioPrefs.cpp:126` really does consume this value in this build,
rather than leaving that as a reading of the source.

Mutation 2 is a genuine blind spot. A compile check can prove that a value is
returned, not that it is the right one. Catching it would need a host test that
instantiates `ESPNOWRadio` and asserts `false`, which needs stand-ins for the
twelve member functions that live in `ESPNOWRadio.cpp`. That is not justified for
asserting one constant, so the argument for `false` rather than `true` rests on
the reasoning under Decision, not on a test.

### Host tests

`pio test -e native -e native_kiss_modem`: 54 of 54 test cases passed. The suites
`native_core` and `native_ofast` do not exist on this base.

### Builds

The 18 environment matrix on this commit against a `dev` worktree with the same
toolchain, including `RAK_3112_repeater_bridge_espnow` (which compiles
`helpers/bridges/ESPNowBridge.cpp`, not this driver). Flash and RAM identical
everywhere: in the 16 environments that never compile the file, and in the two
that do, because the changed member is not emitted there.

### Hardware

Not applicable, and not merely unavailable: there is no board here whose only
radio is ESP-NOW, and the two variants that would instantiate this driver do not
build at all. No image contains the changed function, which the symbol check
under Reach establishes, so there is nothing to observe at runtime.

## Follow-ups

Not part of this fix, listed because they were measured while verifying it:

- `ESP32Board` reaches for `P_LORA_*` unconditionally, which makes an ESP-NOW-only
  variant impossible to build.
- `ESPNOWRadio.cpp` needs updating for the ESP-IDF 5.x `esp_now_recv_cb_t`
  signature and a missing include for `esp_efuse_mac_get_default`.
- Project sources are compiled with `-w`. A defect like this one cannot be caught
  while that holds.
