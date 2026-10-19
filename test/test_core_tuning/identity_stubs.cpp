// Link-time stand-ins for the parts of mesh::Identity that Mesh.cpp references.
// The real implementations need the board crypto libraries, which do not exist on
// the host. The tuning tests never sign, verify or derive secrets, so any call is a
// test failure.

#include <gtest/gtest.h>
#include <Identity.h>

namespace mesh {

Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

LocalIdentity::LocalIdentity() { memset(prv_key, 0, sizeof(prv_key)); }

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
  ADD_FAILURE() << "Identity::verify must not be called by the tuning tests";
  return false;
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  ADD_FAILURE() << "LocalIdentity::sign must not be called by the tuning tests";
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub_key) const {
  ADD_FAILURE() << "LocalIdentity::calcSharedSecret must not be called by the tuning tests";
}

}  // namespace mesh
