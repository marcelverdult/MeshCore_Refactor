// The boot sequence of the sensor, which step 4f moves into the shared base. The
// expectations live in test/mocks/BootSequenceChecks.h, shared with the repeater and
// the room server; main() is in test_sensor_role.cpp.

#include "SensorMesh.h"

// SensorMesh is abstract on purpose: the application supplies what to read and how
// to answer a series query. This is the smallest concrete role, and it adds nothing
// to the boot sequence under test.
class BootSensor : public SensorMesh {
public:
  using SensorMesh::SensorMesh;

protected:
  void onSensorDataRead() override { }
  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago,
                      MinMaxAvg dest[], int max_num) override {
    return 0;
  }
};

using BootRole = BootSensor;

// Before prefs version 1 the sensor ignored the two hop limits and stored a zero
// for both, so a zero in an unversioned file was never chosen.
#define BOOT_ROLE_LIFTS_UNVERSIONED_ZERO_HOP_LIMITS

#include <BootSequenceChecks.h>
