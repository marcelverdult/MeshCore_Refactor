#pragma once

#include <cstdint>

// Stand-in for the Arduino SPI library: the type, the global and the bus
// numbers, so a test can see which bus the radio is started on and which host
// an ESP32 variant's own bus is on. FSPI and HSPI as on the ESP32-S3 and C3;
// the default host is HSPI, as in the ESP32 core.
#ifndef FSPI
#define FSPI 0
#define HSPI 1
#endif

class SPIClass {
public:
  uint8_t bus;
  SPIClass(uint8_t spi_bus = HSPI) : bus(spi_bus) { }
};

inline SPIClass SPI;
