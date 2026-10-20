// Link-time stand-ins for the parts of mesh::Identity that the core and the server
// helpers reference. The real implementations need the board crypto libraries,
// which do not exist on the host.
//
// These are deterministic, NOT cryptographic. The roles sign every advertisement
// they schedule, so a stub that refused to run would turn ordinary operation into a
// test failure; instead each operation derives its output from its inputs, so a
// round trip is self consistent and a test stays repeatable. No test in this suite
// asserts a cryptographic property, and none may: a passing signature here says
// nothing about Ed25519.

#include <gtest/gtest.h>
#include <Identity.h>

#include <cstring>

namespace {

// A stable byte mix, so output depends on every input byte.
void derive(uint8_t* dest, size_t dest_len, const uint8_t* a, size_t a_len,
            const uint8_t* b, size_t b_len) {
  uint8_t acc = 0x5A;
  for (size_t i = 0; i < a_len; i++) acc = (uint8_t)(acc * 31 + a[i]);
  for (size_t i = 0; i < b_len; i++) acc = (uint8_t)(acc * 31 + b[i]);
  for (size_t i = 0; i < dest_len; i++) {
    acc = (uint8_t)(acc * 31 + (uint8_t)i);
    dest[i] = acc;
  }
}

}  // namespace

namespace mesh {

Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

LocalIdentity::LocalIdentity() { memset(prv_key, 0, sizeof(prv_key)); }

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  derive(sig, SIGNATURE_SIZE, prv_key, PRV_KEY_SIZE, message, (size_t)msg_len);
}

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
  // Cannot be checked without the private key, so accept anything. A test that
  // needs to tell a good signature from a bad one must not use this stub.
  return true;
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub_key) const {
  derive(secret, PUB_KEY_SIZE, prv_key, PRV_KEY_SIZE, other_pub_key, PUB_KEY_SIZE);
}

// The identity file and the identity CLI commands are not exercised here, so these
// only have to satisfy the linker, and any call is a test failure.

bool Identity::readFrom(Stream& s) {
  ADD_FAILURE() << "Identity::readFrom must not be called by the repeater role tests";
  return false;
}

bool Identity::writeTo(Stream& s) const {
  ADD_FAILURE() << "Identity::writeTo must not be called by the repeater role tests";
  return false;
}

void Identity::printTo(Stream& s) const {
  ADD_FAILURE() << "Identity::printTo must not be called by the repeater role tests";
}

bool LocalIdentity::readFrom(Stream& s) {
  ADD_FAILURE() << "LocalIdentity::readFrom must not be called by the repeater role tests";
  return false;
}

bool LocalIdentity::writeTo(Stream& s) const {
  ADD_FAILURE() << "LocalIdentity::writeTo must not be called by the repeater role tests";
  return false;
}

void LocalIdentity::printTo(Stream& s) const {
  ADD_FAILURE() << "LocalIdentity::printTo must not be called by the repeater role tests";
}

size_t LocalIdentity::writeTo(uint8_t* dest, size_t max_len) {
  ADD_FAILURE() << "LocalIdentity::writeTo must not be called by the repeater role tests";
  return 0;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  ADD_FAILURE() << "LocalIdentity::readFrom must not be called by the repeater role tests";
}

bool LocalIdentity::validatePrivateKey(const uint8_t prv[64]) {
  ADD_FAILURE() << "LocalIdentity::validatePrivateKey must not be called by the repeater role tests";
  return false;
}

}  // namespace mesh
