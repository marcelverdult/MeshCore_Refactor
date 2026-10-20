// The boot sequence of the repeater, the role whose begin() differs from the other
// two: it starts the bridge when one is built in, and logs the RX gain it applied.
// The expectations live in test/mocks/BootSequenceChecks.h, shared with the room
// server and the sensor; main() is in test_repeater_role.cpp.

#include "MyMesh.h"

using BootRole = MyMesh;

// Of the three roles only the repeater logs the gain mode it applied (debug builds).
#define BOOT_ROLE_LOGS_RX_GAIN

#include <BootSequenceChecks.h>
