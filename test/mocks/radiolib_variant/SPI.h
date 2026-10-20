#pragma once

// Stand-in for the Arduino SPI library: only the type and the global, so a test
// can see which bus the radio is started on.
class SPIClass { };

inline SPIClass SPI;
