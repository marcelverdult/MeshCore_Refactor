# T-Echo boot hangs after a soft reset because a slave holds SDA low

## Context

After a soft reset (DFU by 1200 baud touch, the CLI `reboot`, the reset button,
a watchdog) a LilyGo T-Echo sometimes never finishes booting. The USB port
enumerates, the console stays silent, the radio never starts.

The boot path on `upstream/dev` 428a95c6:

```cpp
void TechoBoard::begin() {
  NRF52Board::begin();
  Wire.begin();                     // variants/lilygo_techo/TechoBoard.cpp:11
  ...
}

bool radio_init() {
  rtc_clock.begin(Wire);            // variants/lilygo_techo/target.cpp:29
  return radio.std_init(&SPI);
}
```

`AutoDiscoverRTCClock::begin()` probes up to four RTC addresses, and a probe is
`beginTransmission()` plus `endTransmission()`
(`src/helpers/AutoDiscoverRTCClock.cpp:23-26`, first probe at `:31`).

The Adafruit nRF52 `TwoWire` driver (the pinned fork, commit d541301) waits for
the TWIM peripheral in loops that have no timeout:

```cpp
while(!_p_twim->EVENTS_TXSTARTED && !_p_twim->EVENTS_ERROR);   // Wire_nRF52.cpp:230
while(!_p_twim->EVENTS_LASTTX && !_p_twim->EVENTS_ERROR);      // :234
while(!_p_twim->EVENTS_STOPPED);                               // :241, no error exit
```

`requestFrom()` has the same pattern (`:166`, `:169`, `:175`). `begin()` only
configures the pins (`:50-70`); nothing in the driver or in the MeshCore board
code clears a stuck bus.

### What the board showed

A reset does not reset the I2C slaves. The T-Echo has a PCF8563 RTC and, on
the sensor build, a BME280 on the same bus (P0.26 SDA, P0.27 SCL,
`variants/lilygo_techo/variant.h:48-49`). If the reset lands in the middle of
a transfer, a slave can be left holding SDA low and waiting for more clocks.

Measured on a T-Echo sensor build with a log line before and after each boot
step:

- Without any bus handling, the reset path through DFU ended in a hang at
  "before RTC autodiscovery" in 4 of 4 boots of an upstream `dev` build. The
  same hang was reported for a 1.17.1 build; that one was not measured again
  for this note.
- Reading the pins before `Wire.begin()` showed SDA low in the hanging boots.
- One SCL clock was enough to release the bus in the cases seen, and the boot
  then completed.

What was not measured: which of the three loops the firmware sits in, and
which slave holds SDA. Neither matters for the fix.

### Why the failure is intermittent

Whether SDA is low depends on where in a transfer the reset lands. Plain DFU
cycles of the sensor build produced no hang at all in a later series (8 of 8
and 20 of 20 boots, SDA high every time), so the state is not reproducible on
demand with a plain image. To force it, a test image polls the PCF8563 in a
tight loop after boot, so that nearly every reset lands inside a transfer.
That is a test device, not part of this change.

## Decision

Run the bus clear of the I2C specification (UM10204 section 3.1.16) before
`Wire.begin()`. It lives in a small header, `src/helpers/nrf52/I2cBusClear.h`,
so other boards can reuse it, and `TechoBoard::begin()` is the only caller.

The sequence:

1. Release both lines (input with pull-up) and wait 2 ms for the pull-ups.
2. SCL low after the wait: give up without touching anything.
3. SDA high: do nothing. No clocks, no STOP.
4. SDA low: up to nine SCL clocks at about 100 kHz, stop as soon as SDA reads
   high, then generate a STOP (SDA low, SCL high, SDA released).
5. SDA still low after nine clocks: no STOP, report failure, continue the boot.

Both lines are only ever driven low or released (`OUTPUT_S0D1` and
`INPUT_PULLUP`), never driven high. A slave that stretches the clock is not
overridden, and a wait for SCL to rise is bounded to 1 ms.

The pin access is a template parameter, so the clock sequence runs unchanged
in a host test against an open-drain bus model.

### Why not the alternatives

- **A timeout in the driver** (adafruit/Adafruit_nRF52_Arduino#873, open)
  bounds the hang but leaves the bus stuck, so the probe fails and the RTC is
  not found. It is a separate defect and worth having, but it does not remove
  the cause.
- **Skipping the RTC probe** on a bare board (the approach of #3331 for the
  Xiao) hides the symptom on boards without an RTC and does nothing for boards
  that have one.
- **Doing it inside `NRF52Board::begin()`.** The I2C pins differ per board
  (the Xiao moves Wire to other pins in #2499) and boards without I2C should
  not pay for it. A per board call with the pins as arguments is explicit.
- **Copying the sequence into `TechoBoard.cpp`** (#3455). The same logic would
  be copied into every board that needs it, with no way to test it on the
  host.

## Verification

Host:

- `pio test -e native -e native_kiss_modem`: 64 of 64 test cases.
- `test/test_i2c_bus_clear` has ten tests against a bus model: healthy bus
  (no clock, no STOP), release after 1, 5 and 9 clocks, SDA stuck forever (nine
  clocks, no STOP), SCL stuck low (nothing touched, bounded), clock
  stretching, stretch beyond the limit, SDA low again after the STOP, clock
  rate.

Mutation tests (each defect built into the header, the suite must go red):

| mutation | result |
|---|---|
| STOP removed | red |
| nine clocks reduced to eight | red |
| healthy bus is clocked anyway | red |
| bound on the SCL wait removed | red |
| keep clocking after SDA is free | red |
| success flag always true | red |
| STOP issued with SDA stuck | red |
| abort on SCL held low removed | red |

Board, LilyGo T-Echo sensor build, DFU reset before each boot, a test image
that polls the PCF8563 continuously:

| image | boots that completed | SDA low at start | outcome |
|---|---|---|---|
| without the bus clear | 2 of 10 | 8 of 10 | all 8 hangs at "before RTC autodiscovery" |
| with the bus clear | 12 of 12 | 5 of 12 | cleared with 1, 3, 1, 1, 1 clocks, boot completed every time |

Without the continuous polling the failure did not occur (8 of 8 and 20 of 20
boots, SDA high), so those runs show only that the helper does no harm on a
healthy bus. The board was returned to the original 1.17.1 build and all 31
reference values matched the state before the test.

Build matrix of 23 environments against `dev`, same toolchain: all build, flash
grows by 192 bytes in the five T-Echo environments (repeater, room server,
companion BLE, companion USB, KISS modem) and by 0 bytes in all others.

## Not covered

- Other nRF52 boards. Many call `Wire.begin()` in their own `Board::begin()`
  and can take the helper with one line (`i2cBusClearNrf52(PIN_WIRE_SDA,
  PIN_WIRE_SCL)`). They are not changed because no hardware was available to
  verify them. The Lite and Card variants of the T-Echo use other pins and are
  also unchanged.
- ESP32. Its driver is a different code path; whether it can hang the same way
  was not checked.
- How often the state occurs in normal operation. The stress image forces it.

## Related upstream work

- #3455 (draft) proposes the same recovery for the T-Echo only, as a static
  function in `TechoBoard.cpp`. Both patches change the same lines of
  `TechoBoard::begin()`, so they conflict. The helper here is meant to replace
  that local function.
- adafruit/Adafruit_nRF52_Arduino#873 adds timeouts to the TWIM wait loops. It
  is complementary: with it a stuck bus no longer blocks the boot, with this
  change the bus is released in the first place.
- #3331 skips the RTC probe on the Xiao repeater for the same driver defect.
  It does not touch the T-Echo.
