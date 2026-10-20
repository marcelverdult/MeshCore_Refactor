#include <Arduino.h>
#include <Wire.h>

#include "TechoBoard.h"
#include <helpers/nrf52/I2cBusClear.h>

#ifdef LILYGO_TECHO

void TechoBoard::begin() {
  NRF52Board::begin();

  // A soft reset can leave the RTC or sensor holding SDA low; without this the
  // first Wire transfer (RTC autodiscovery) never returns.
  I2cBusClearResult i2c = i2cBusClearNrf52(PIN_WIRE_SDA, PIN_WIRE_SCL);
  if (i2c.sda_low_at_start) {
    MESH_DEBUG_PRINTLN("I2C bus clear: %d clocks, %s", i2c.pulses, i2c.ok ? "released" : "still stuck");
  }
  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up
}

uint16_t TechoBoard::getBattMilliVolts() {
  int adcvalue = 0;

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(10);

  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  adcvalue = analogRead(PIN_VBAT_READ);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  return (uint16_t)((float)adcvalue * REAL_VBAT_MV_PER_LSB);
}
#endif
