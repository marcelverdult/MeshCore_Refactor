#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Stream.h"

// TxtDataHelpers.cpp is built for StrHelper::strncpy() and also holds code that
// calls Arduino's ltoa(), which is not in the C library. Decimal is all it needs here.
inline char* ltoa(long value, char* buf, int) {
  snprintf(buf, 21, "%ld", value);
  return buf;
}
