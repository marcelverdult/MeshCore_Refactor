// Link-time stand-ins for the parts of mesh::Identity that CommonCLI references
// (the "set prv.key", "get prv.key" and "get public.key" commands). The real ones need
// the board crypto libraries, which do not exist on the host.
//
// Deterministic, NOT cryptographic. A private key is accepted when the test says so
// (identityControl().validate_ok), and the public key derived from a private key is
// its first 32 bytes XOR 0x5A, so a test can tell which key a command used. No test
// in this suite asserts a cryptographic property, and none may.

#include <Identity.h>
#include <helpers/sensors/LocationProvider.h>

#include <cstring>

#include "CliHarness.h"

IdentityStubControl& identityControl() {
  static IdentityStubControl c;
  return c;
}

namespace mesh {

Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

LocalIdentity::LocalIdentity() { memset(prv_key, 0, sizeof(prv_key)); }

bool LocalIdentity::validatePrivateKey(const uint8_t prv[64]) {
  IdentityStubControl& c = identityControl();
  c.validate_calls++;
  memcpy(c.last_validated, prv, PRV_KEY_SIZE);
  return c.validate_ok;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  identityControl().read_from_calls++;
  memcpy(prv_key, src, len < PRV_KEY_SIZE ? len : PRV_KEY_SIZE);
  for (int i = 0; i < PUB_KEY_SIZE; i++) pub_key[i] = prv_key[i] ^ 0x5A;
}

size_t LocalIdentity::writeTo(uint8_t* dest, size_t max_len) const {
  size_t n = max_len < PRV_KEY_SIZE ? max_len : PRV_KEY_SIZE;
  memcpy(dest, prv_key, n);
  return n;
}

}  // namespace mesh
