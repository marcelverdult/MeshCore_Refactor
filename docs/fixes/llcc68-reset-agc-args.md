# Fix: LLCC68 wrapper calls sx126xResetAGC with one argument

Status: ready for review. Behaviour change: none, no environment compiles the file.

## Context

`sx126xResetAGC()` in `src/helpers/radiolib/SX126xReset.h` takes two arguments,
the radio and the boosted gain mode to re-apply after calibration:

```cpp
inline void sx126xResetAGC(SX126x* radio, bool rx_boost_gain)
```

Three of the four SX126x family wrappers call it that way:

| Wrapper | Line | Call |
|---------|------|------|
| `CustomSX1262Wrapper.h` | 51 | `sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode());` |
| `CustomSX1268Wrapper.h` | 48 | `sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode());` |
| `CustomSTM32WLxWrapper.h` | 38 | `sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode());` |
| `CustomLLCC68Wrapper.h` | 38 | `sx126xResetAGC((SX126x *)_radio);` |

The fourth does not compile. It never had to: nothing in the tree includes
`CustomLLCC68Wrapper.h`. `LLCC68` appears only in `CustomLLCC68.h`, in the
wrapper itself and in a comment in `SX126xReset.h`. No variant sets
`WRAPPER_CLASS` to `CustomLLCC68Wrapper`, so no environment pulls the header into
a translation unit and no compiler ever looks at it.

The history is in the log rather than a guess. Commit 6f491f30 (9 August 2026,
"RX Boosted Gain was set back to the compiled in value rather than the
current/flash value set by the user when the ACG reset is triggered") added the
`rx_boost_gain` parameter and updated five files with it:
`CustomLR1110Wrapper.h`, `CustomSTM32WLxWrapper.h`, `CustomSX1262Wrapper.h`,
`CustomSX1268Wrapper.h` and `LR11x0Reset.h`. `CustomLLCC68Wrapper.h` is not among
them. Nothing reported it, because nothing compiles it.

## Decision

Call it the way the other three do:

```cpp
void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
```

The line is now character for character the same as in the other three wrappers,
which is the point: the four members of one chip family should not differ here.

## Upstream

Do not propose this as its own upstream pull request. The identical change is
already in the open PR meshcore-dev/MeshCore#3496, "KISS modem: AGC, FEM gain and
RX boosted gain control", character for character:

```
-  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }
+  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
```

That PR is open, not merged, so `dev` still carries the defect and this fork still
needs the fix. But #3496 changes 24 files, and a separate one line PR for a line
it already contains would compete with it. If the fix is wanted upstream sooner,
the useful move is a comment on #3496, not a new pull request.

Found by searching the upstream repository for the function name rather than the
file: the API text search covers only the pull request text, not the files a pull
request changes, so a search by file name misses this.

## Alternatives considered

- **Pass a literal `false`.** Shorter, but wrong: the other three re-apply the
  mode the radio is actually in, and `SX126xReset.h` re-applies it only when
  `SX126X_RX_BOOSTED_GAIN` is defined. A literal would silently clear boosted
  gain after every AGC reset on any board that enables it.
- **Delete the dead files.** `CustomLLCC68.h` and `CustomLLCC68Wrapper.h` have no
  users at all, so deleting them would also remove the defect. Rejected: the
  LLCC68 is a real chip in the SX126x family, the driver looks complete and
  someone adding an LLCC68 board would want it. Deleting it is a separate
  decision that does not belong in a one line fix.
- **Add an LLCC68 variant so the file is built.** That is the real cure for the
  blind spot, but it needs hardware nobody has here, and inventing a variant to
  satisfy the compiler is worse than the compile check below.

## Compatibility

None affected, and the claim rests on the include graph rather than on binary
sizes. Nothing includes `CustomLLCC68Wrapper.h`, so no translation unit contains
it: on `integration` the name appears only in the header itself, in the
architecture review that records this defect, in this note and in the diagnostic
patch. The artefact side agrees: an `xtensa-esp32-elf-nm` of a built
`Heltec_v3_companion_radio_ble` image finds zero LLCC68 symbols, and nothing
under that environment's build directory references the wrapper.

Identical flash and RAM across the build matrix is listed below as corroboration,
not as the proof. Unchanged size does not by itself mean a file is not compiled:
a file can be compiled while the linker drops its unused code, which is exactly
what happens to `ESPNOWRadio.cpp` in the ESP32 companion environments.

## Verification

### Compile check

Since no environment compiles the header, the check has to create one. The
diagnostic patch `tools/refactoring/diag-llcc68-wrapper.patch` adds a single
include to `variants/heltec_v3/target.cpp`, which pulls the wrapper into a real
environment. It is a test aid and part of no fix.

Before the fix, `Heltec_v3_repeater` with that patch:

```
src/helpers/radiolib/CustomLLCC68Wrapper.h:38:63: error: too few arguments to function 'void sx126xResetAGC(SX126x*, bool)'
*** [.pio/build/Heltec_v3_repeater/variants/heltec_v3/target.cpp.o] Error 1
==================== 1 failed, 0 succeeded in 00:01:30.621 ====================
```

After the fix, same patch, same environment:

```
========================= 1 succeeded in 00:00:52.124 =========================
```

### Mutation tests

Five deliberate errors in the fixed line, each built with the diagnostic patch.
Every one has to fail, otherwise the compile check above proves nothing.

| # | Mutation | Result |
|---|----------|--------|
| 1 | second argument dropped (the original defect) | `error: too few arguments to function 'void sx126xResetAGC(SX126x*, bool)'` |
| 2 | a third argument added | `error: too many arguments to function 'void sx126xResetAGC(SX126x*, bool)'` |
| 3 | second argument replaced by a `PacketMillis` | `error: cannot convert 'PacketMillis' to 'bool'` |
| 4 | first argument replaced by `_board` | `error: cannot convert 'mesh::MainBoard*' to 'SX126x*'` |
| 5 | function name misspelled | `error: 'sx126xResetAgc' was not declared in this scope` |

All five failed to build.

### Host tests

`pio test -e native -e native_kiss_modem`: 54 of 54 test cases passed. The
suites `native_core` and `native_ofast` do not exist on this base.

### Builds

The 18 environment matrix on this commit against a `dev` worktree with the same
toolchain: all 18 built, and flash and RAM are identical to `dev` in every one of
them, to the byte.

That result is consistent with the file reaching no binary, and it rules out a
regression, but on its own it does not establish it. The include graph and the
symbol check under Compatibility are what establish it.

### Hardware

Not applicable. There is no board with an LLCC68 radio in this tree, and no
firmware contains the changed code, so there is nothing to observe at runtime.
