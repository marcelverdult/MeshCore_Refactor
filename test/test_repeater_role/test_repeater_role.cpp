// The repeater on the host. The repeater and the room server both define `MyMesh`,
// so each needs a binary of its own, and this is the repeater's. It exists so the
// boot sequence of all three server roles can be pinned (see
// test/mocks/BootSequenceChecks.h and docs/refactoring/0007-boot-sequence.md); the
// repeater's other behaviour is not characterized here.

#include <gtest/gtest.h>

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
