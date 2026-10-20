#pragma once

// Stand-in for the Arduino Wire library: only the type and the global, so a
// test can see which bus the RTC clock is started on.
class TwoWire { };

inline TwoWire Wire;
