// The boot sequence of the room server, which step 4f moves into the shared base.
// The expectations live in test/mocks/BootSequenceChecks.h, shared with the repeater
// and the sensor; main() is in test_server_base.cpp.

#include "MyMesh.h"

using BootRole = MyMesh;

#include <BootSequenceChecks.h>
