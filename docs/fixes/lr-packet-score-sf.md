# Fix: the packet score assumed spreading factor 10 on LR radios

Status: ready for review. Behaviour change: yes on LR1110, LR1121 and LR2021
boards, scoped under Behaviour change below.

## Context

`Dispatcher::checkRecv()` scores every received packet and uses the score to
decide how long to sit on a flood packet before rebroadcasting it. A node that
received a packet well rebroadcasts sooner, a node that barely heard it waits, so
the better placed neighbour gets the air. `src/Dispatcher.cpp:207` fetches the
score, `:240` restricts it to flood routed packets, `:243` turns it into a delay
and `:244` processes immediately below 50 ms.

The score depends on the spreading factor, because the SNR a packet needs to be
decodable rises as the spreading factor falls. `RadioLibWrapper::packetScoreInt()`
holds one threshold per factor, from -7.5 dB at SF7 to -20 dB at SF12.

The base class did not ask for the factor. `RadioLibWrappers.h:76`:

```cpp
float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10
```

Five of the eight wrappers overrode it, all five in the same shape: read the
factor off the radio, then call `packetScoreInt`. Three did not.

| Wrapper | overrides `packetScore` | overrides `getSpreadingFactor` |
|---------|------------------------|-------------------------------|
| `CustomLLCC68Wrapper.h` | yes, line 32 | yes |
| `CustomSTM32WLxWrapper.h` | yes, line 32 | yes |
| `CustomSX1262Wrapper.h` | yes, line 35 | yes |
| `CustomSX1268Wrapper.h` | yes, line 35 | yes |
| `CustomSX1276Wrapper.h` | yes, line 31 | yes |
| `CustomLR1110Wrapper.h` | **no** | yes, line 44 |
| `CustomLR1121Wrapper.h` | **no** | yes, line 45 |
| `CustomLR2021Wrapper.h` | **no** | yes, line 94 |

These are all eight subclasses of `RadioLibWrapper`, and `ESPNOWRadio` (which
returns 0) is the only other radio in the tree.

The decisive detail is in the right hand column: all eight already report the
factor through `getSpreadingFactor()`, and in the five overriding wrappers that
function reads exactly the same member the override reads. For example
`CustomSX1262Wrapper.h:36` uses `((CustomSX1262 *)_radio)->spreadingFactor` inside
`packetScore` and line 39 returns the same expression from `getSpreadingFactor()`.
So the five overrides were, line for line, what the base class could have computed
by asking.

So the three LR wrappers scored every packet as if the radio ran at SF10, on every
role, while the information needed was one virtual call away.

## Decision

The base class asks instead of assuming, and the five overrides that said the same
thing go away:

```cpp
float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, getSpreadingFactor(), packet_len); }
```

Net effect on the tree: five insertions, 21 deletions.

Two reasons for fixing it in the base rather than adding three overrides:

- A default that guesses reproduces this defect for the next driver. A default
  that asks cannot: a new wrapper that reports its factor is scored correctly
  without doing anything, and one that reports nothing falls back to the build's
  `LORA_SF`, which is at least that build's actual configuration rather than an
  unrelated constant.
- It is what makes the five existing chips provably unchanged. Each removed
  override read the same member that `getSpreadingFactor()` on the same wrapper
  returns, so the value fed into `packetScoreInt` is identical. Their code is not
  byte identical afterwards, because a direct member read becomes a virtual call,
  and the build matrix shows that as a handful of bytes. What is unchanged is the
  score, and the host test pins that at SF10 and across the range.

## Alternatives considered

- **Add the missing override to the three LR wrappers.** The smallest diff that
  fixes the symptom, and the shape upstream already uses. Rejected: it leaves the
  guessing default in place as a trap, and leaves the same four lines copied eight
  times.
- **Keep the literal but make it `LORA_SF`.** Cheaper than a virtual call and
  correct for boards that never retune. Rejected: the spreading factor is a
  runtime setting (`set radio ...`), so a compile-time constant is wrong as soon
  as an operator changes it, which is precisely the case on the board measured
  below (configured SF7, build default SF8).
- **Make `packetScore` non-virtual in the base.** Tidier still, but
  `mesh::Radio::packetScore` is pure virtual and `ESPNOWRadio` needs its own
  answer, so the hook has to stay.
- **Return 0 when no factor is known, to force every driver to report one.** Would
  surface future omissions loudly. Rejected: a zero score means maximum delay, so
  the failure mode is a node that stops rebroadcasting, which is worse than a
  slightly wrong score.

## Behaviour change

On LR1110, LR1121 and LR2021 boards the score changes whenever the radio does not
run at SF10. Below SF10 the old code scored too high, because the SF10 threshold
of -15 dB is more generous than the real one, so those nodes rebroadcast too
eagerly. Above SF10 it scored too low and they held back too long. At SF10 nothing
changes, which is the probe on the arithmetic.

What the score feeds into matters for how far this reaches. `calcRxDelay` in
`src/Dispatcher.cpp:55` uses a base of 10, but **all four roles override it** and
use the configured `rxdelay` as the base instead:

```cpp
if (_prefs.rx_delay_base <= 0.0f) return 0;
return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
```

(`examples/companion_radio/MyMesh.cpp:273`, `examples/simple_repeater/MyMesh.cpp:542`,
`examples/simple_room_server/MyMesh.cpp:277`, `examples/simple_sensor/SensorMesh.cpp:325`.)

And `rx_delay_base` is **0 by default in every role**: companion
`NodePrefs.h:29`, repeater `MyMesh.cpp:892` ("turn off by default, was 10.0"),
room server `MyMesh.cpp:648`, sensor `SensorMesh.cpp:800`. The companion even has
the old default commented out at `MyMesh.cpp:934` with "enable once new algo
fixed".

So on a default configuration the score reaches only the logging
(`Dispatcher.cpp:224` and the `logRx` hook at `:238`) and the delay stays 0. The
rebroadcast timing of an LR board changes only where an operator has set
`rxdelay` above 0. That is a narrower claim than "LR boards rebroadcast at the
wrong time", and it is the one the measurements support.

With `rxdelay` at 10, the historical default, for a 64 byte packet and 500 ms
airtime:

| SF | SNR | score old | delay old | score new | delay new |
|----|-----|-----------|-----------|-----------|-----------|
| 7 | 0 dB | 1.000 | 0 ms | 0.562 | 469 ms |
| 7 | -5 dB | 0.750 | 129 ms | 0.188 | 1799 ms |
| 8 | -5 dB | 0.750 | 129 ms | 0.375 | 993 ms |
| 9 | -5 dB | 0.750 | 129 ms | 0.562 | 469 ms |
| 10 | any | unchanged | unchanged | unchanged | unchanged |
| 11 | -15 dB | 0.000 | 3040 ms | 0.188 | 1799 ms |
| 12 | -10 dB | 0.375 | 993 ms | 0.750 | 129 ms |

The 18 affected variants' environments are listed under Builds.

## Compatibility

`/prefs.json` and the CLI are untouched. No setting changes meaning, so upgrade
and downgrade are unaffected. The five SX126x and SX127x family wrappers compute
the same score as before, because the removed overrides read the same member that
`getSpreadingFactor()` returns on those wrappers. Their binaries do shift by a few
bytes, since the base class now reaches the factor through a virtual call; the
build matrix lists the exact figures per environment.

## Verification

### Host tests

The existing native environments cover prefs and tables, not the radio wrappers,
so this fix adds one. `native_radio_wrappers` builds the real
`src/helpers/radiolib/RadioLibWrappers.cpp` against a minimal stand-in for
RadioLib's `PhysicalLayer` in `test/mocks/RadioLib.h`, the same approach the
`native` environments already take for the Arduino core and `Mesh`. The code under
test is the shipping code, not a copy of it: the test subclass only supplies the
three pure virtuals and reports a spreading factor.

`LORA_SF` is deliberately set to 9 in that environment, so a score that silently
assumes 10 fails rather than passes by coincidence.

`test/mocks/Arduino.h` gains `min`, `max` and the two argument `random()`, which
the wrapper needs and the existing suites never reached. `test/mocks/RadioLib.h`
includes `Arduino.h`, mirroring the real `RadioLib.h`.

Before the fix, `pio test -e native_radio_wrappers`:

```
RadioWrapperFixture.ScoreFollowsTheReportedSpreadingFactor            [FAILED]
RadioWrapperFixture.DoesNotAssumeSpreadingFactorTen                   [FAILED]
RadioWrapperFixture.AgreesWithTheOldAssumptionAtSpreadingFactorTen    [PASSED]
RadioWrapperFixture.FallsBackToTheBuildsSpreadingFactor               [FAILED]
RadioWrapperFixture.LongerPacketsScoreLower                           [PASSED]
RadioWrapperFixture.BelowThresholdScoresZero                          [FAILED]
```

The two that pass are the ones that must: SF10 is where old and new agree, and the
length penalty is independent of the factor.

After the fix, all seven pass, and the three existing suites with them:

```
native                 test_mesh_tables        PASSED
native                 test_config_serializer  PASSED
native                 test_utils              PASSED
native                 test_utf8_helpers       PASSED
native                 test_routing_policy     PASSED
native_radio_wrappers  test_radio_wrappers     PASSED
native_kiss_modem      test_kiss_modem         PASSED
================= 61 test cases: 61 succeeded =================
```

`native_core` and `native_ofast` do not exist on this base.

### Mutation tests

Eight deliberate errors against the suite above.

| # | Mutation | Result |
|---|----------|--------|
| 1 | `getSpreadingFactor() + 1` | caught, 6 failures |
| 2 | `getSpreadingFactor() - 1` | caught, 4 failures |
| 3 | back to the literal `10` (the original defect) | caught, 5 failures |
| 4 | SF8 threshold moved from -10 to -11 | caught, 1 failure |
| 5 | length penalty replaced by 1.0 | caught, 2 failures |
| 6 | clamp removed | caught, 1 failure |
| 7 | threshold guard loosened by 1 dB | not caught, equivalent mutant |
| 8 | threshold guard tightened by 1 dB | caught, 1 failure |

Mutation 7 is not a gap. Loosening the guard lets an SNR between the threshold and
1 dB below it through, where the rate is negative and the clamp turns it back into
zero, so the mutant is behaviourally identical:

```
snr=-10.5: rate=-0.050 product=-0.0375 after clamp=0.0
snr=-10.2: rate=-0.020 product=-0.0150 after clamp=0.0
```

Tightening the guard, mutation 8, is a real change and is caught. Mutations 4 and 8
were only caught after the suite gained absolute expected values worked out by
hand; the first version compared the score against itself and would have let both
through. That weakness was found by mutation 8 and fixed before this was written.

### Builds

`RadioLibWrappers.h` is built by 612 environments across five toolchains, counted
from the resolved configuration (`pio project config --json-output`, environments
carrying `WRAPPER_CLASS`): 315 on espressif32@6.11.0, 240 on nordicnrf52, 25 on
rp2040, 16 on ststm32 and 16 on pioarduino-esp32. The first draft of this change
was built on four of those five, which left the pioarduino platform, with its own
ESP-IDF, entirely untested. All five are covered now.

Built on this commit against a `dev` worktree with the same toolchain: the 18
environment matrix, all 67 environments of the 11 affected variants, and all 16
pioarduino environments.

LR1110: `minewsemi_me25ls01`, `t1000-e`, `thinknode_m3`, `thinknode_m4`,
`thinknode_m7`, `thinknode_m9`, `wio_wm1110`. LR1121: `muzi_base`,
`waveshare_esp32s3_lr1121`. LR2021: `meshnology_w12`, `meshtracker_x1`.

Of the 85 environments in the first two groups, 84 built on both sides. Flash
moves between -8 and +48 bytes, and RAM does not move at all in any of them:

| Change | Environments |
|--------|--------------|
| +48 bytes | 10 |
| +32 bytes | 37 |
| +24 bytes | 1 |
| +16 bytes | 3 |
| +12 bytes | 1 |
| +8 bytes | 28 |
| +4 bytes | 2 |
| unchanged | 1, `wio-e5-mini_sensor` |
| -8 bytes | 1, `wio-e5-mini_repeater` |

That spread is what replacing a direct member read with a virtual call costs, and
it is the measured answer to the question the first draft of this note got wrong
by claiming the five existing chips stayed byte identical.

All 16 pioarduino environments built on both sides, with flash moving +12 to +16
bytes and RAM again not moving:

| Change | Environments |
|--------|--------------|
| +16 bytes | 1, `LilyGo_Tlora_C6_kiss_modem` |
| +14 bytes | 14 |
| +12 bytes | 1, `M5Stack_Unit_C6L_kiss_modem` |

`MeshTracker_X1_kiss_modem` is the one environment that does not build, and it
fails identically on both sides with `fatal error: MicroNMEA.h: No such file or
directory`. That is a pre-existing defect of the variant, where only this one of
its five environments does not list the dependency; it is unrelated to this change
and is fixed separately.

### Hardware

Measured on a Seeed T1000-E, which carries an LR1110, with marked
`t1000e_companion_radio_usb` images so the running artefact is not in doubt. The
board reports its configured radio settings as 869.4321289 MHz, 62.5 kHz, **SF7**,
CR5. SF7 is the most affected case, and it is not the build default (869.618,
SF8), which also confirms the stored settings were really loaded.

A diagnostic command, `tools/refactoring/diag-packet-score.patch`, prints the
configured factor, the factor the radio reports and the score at several SNR
values, scaled by 1000. It is a test aid and part of no fix.

```
dev:  ver => v1.18.0 (Build: f2dev-428a95c6)
      diag score => cfg=7 rep=7 s0=1000 s5=750 s10=375 s15=0 rx5=129
fix:  ver => v1.18.0 (Build: f2fix6-score-sf)
      diag score => cfg=7 rep=7 s0=562 s5=187 s10=0 s15=0 rx5=1798
```

Three things are settled by this, which no host test could settle:

- `rep=7` on both images: the LR1110 really does report its configured spreading
  factor at runtime. The fix rests on that, and it is now measured rather than
  argued from the source.
- The scores match the hand calculation for SF7 exactly: 0.5625, 0.1875 and 0
  against the old 1.0, 0.75 and 0.375.
- `rx5` was 0 on both images until `rxdelay` was raised from its stored 0 to 10,
  which is how the scoping above was found rather than assumed. At `rxdelay` 10 the
  rebroadcast delay at -5 dB SNR goes from 129 ms to 1798 ms, matching the table.

`rxdelay` was set back to 0 afterwards and all 15 settings read back identical to
the values read before the measurement. The board was then returned to its stock
`companion_radio_ble` v1.17.1 image.
