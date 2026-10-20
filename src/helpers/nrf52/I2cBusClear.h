#pragma once

#include <stdint.h>

// I2C bus clear (NXP UM10204 section 3.1.16), for the first-boot case where a
// soft reset (DFU, watchdog, reset button) caught a slave mid-byte. Slaves are
// not reset by the MCU, so such a slave keeps holding SDA low and waits for
// more SCL clocks. The Adafruit nRF52 TwoWire driver then busy-waits forever
// on the first transfer (no timeout, no bus clear), which looks like a dead
// firmware before radio_init().
//
// The logic is a template over a pin interface so the clock sequence can be
// tested on the host. Both lines are only ever driven low or released (never
// driven high), so a slave that stretches the clock is not overridden.
//
// Pin interface requirements:
//   void sdaLow();  void sdaRelease();  bool sdaRead();
//   void sclLow();  void sclRelease();  bool sclRead();
//   void delayUs(uint32_t us);
// "Release" means high-impedance with the pull-up active.

struct I2cBusClearResult {
  bool sda_low_at_start;   // SDA was held low when the clear began
  uint8_t pulses;          // SCL clocks issued
  bool ok;                 // both lines high on return
};

#define I2C_BUS_CLEAR_MAX_PULSES 9
#define I2C_BUS_CLEAR_HALF_PERIOD_US 5          // ~100 kHz
#define I2C_BUS_CLEAR_STRETCH_LIMIT_US 1000     // max wait for SCL to rise
#define I2C_BUS_CLEAR_SETTLE_US 2000            // let the pull-ups charge the lines after reset

template <class Pins>
static bool i2cBusClearWaitScl(Pins& p) {
  for (uint32_t waited = 0; !p.sclRead(); waited += 10) {
    if (waited >= I2C_BUS_CLEAR_STRETCH_LIMIT_US) return false;
    p.delayUs(10);
  }
  return true;
}

template <class Pins>
I2cBusClearResult i2cBusClear(Pins& p) {
  I2cBusClearResult r = {false, 0, false};

  p.sdaRelease();
  p.sclRelease();
  p.delayUs(I2C_BUS_CLEAR_SETTLE_US);

  // SCL held low (by a slave or a short): clocking is impossible, give up
  // without touching anything.
  if (!i2cBusClearWaitScl(p)) return r;

  r.sda_low_at_start = !p.sdaRead();
  if (!r.sda_low_at_start) {
    r.ok = true;   // healthy bus: do nothing, no STOP either
    return r;
  }

  while (r.pulses < I2C_BUS_CLEAR_MAX_PULSES && !p.sdaRead()) {
    p.sclLow();
    p.delayUs(I2C_BUS_CLEAR_HALF_PERIOD_US);
    p.sclRelease();
    r.pulses++;
    if (!i2cBusClearWaitScl(p)) return r;
    p.delayUs(I2C_BUS_CLEAR_HALF_PERIOD_US);
  }

  // Still stuck after nine clocks: not a mid-byte slave, a STOP would not help.
  if (!p.sdaRead()) return r;

  // STOP: SDA low -> high while SCL is high, so the slave's state machine ends
  // in idle.
  p.sdaLow();
  p.delayUs(I2C_BUS_CLEAR_HALF_PERIOD_US);
  p.sdaRelease();
  p.delayUs(I2C_BUS_CLEAR_HALF_PERIOD_US);

  r.ok = p.sdaRead() && p.sclRead();
  return r;
}

#ifdef NRF52_PLATFORM
#include <Arduino.h>

// Open-drain pin access on the nRF52 Arduino core: OUTPUT_S0D1 sinks but
// never sources, INPUT_PULLUP releases.
struct Nrf52I2cPins {
  uint8_t sda, scl;
  void sdaLow()     { pinMode(sda, OUTPUT_S0D1); digitalWrite(sda, LOW); }
  void sdaRelease() { pinMode(sda, INPUT_PULLUP); }
  bool sdaRead()    { return digitalRead(sda) != LOW; }
  void sclLow()     { pinMode(scl, OUTPUT_S0D1); digitalWrite(scl, LOW); }
  void sclRelease() { pinMode(scl, INPUT_PULLUP); }
  bool sclRead()    { return digitalRead(scl) != LOW; }
  void delayUs(uint32_t us) { delayMicroseconds(us); }
};

// Call before Wire.begin(). Leaves both pins released.
static inline I2cBusClearResult i2cBusClearNrf52(uint8_t sda, uint8_t scl) {
  Nrf52I2cPins pins = {sda, scl};
  return i2cBusClear(pins);
}
#endif
