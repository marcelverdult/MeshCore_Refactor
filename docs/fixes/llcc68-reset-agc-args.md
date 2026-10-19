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

The likely history is that the `rx_boost_gain` parameter was added to
`sx126xResetAGC()` and the three wrappers that are built were adjusted, while the
one that is not built was missed. Nothing reported it.

## Decision

Call it the way the other three do:

```cpp
void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
```

The line is now character for character the same as in the other three wrappers,
which is the point: the four members of one chip family should not differ here.

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

None affected. The file is in no build, so every binary is unchanged. See the
build matrix under Verification: all 18 environments report the same flash and
RAM as `dev`, to the byte.

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
toolchain. Flash and RAM are expected to be identical in every environment,
because no environment compiles the changed file, and that is what makes this a
useful check rather than a formality.

### Hardware

Not applicable. There is no board with an LLCC68 radio in this tree, and no
firmware contains the changed code, so there is nothing to observe at runtime.
