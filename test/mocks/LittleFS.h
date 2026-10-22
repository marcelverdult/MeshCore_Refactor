#pragma once

// Stand-in for the RP2040 LittleFS global, which the roles call to erase the file
// system. It is the same in-memory instance a test hands to the role, so a format
// through this global is visible to the test.

#include "FS.h"

inline fs::FS LittleFS;
