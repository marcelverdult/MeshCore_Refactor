// Link-time stand-ins for the parts of mesh::Identity that the core, the chat layer
// and the companion reference. The real implementations need the board crypto
// libraries, which do not exist on the host.
//
// The stream and buffer forms copy bytes exactly as src/Identity.cpp does, so the
// identity file and CMD_EXPORT_PRIVATE_KEY / CMD_IMPORT_PRIVATE_KEY carry the same
// layout as on a board. The cryptographic operations are deterministic byte mixes,
// NOT cryptography: a round trip is self consistent and a test stays repeatable, but
// a passing signature here says nothing about Ed25519, and no test may assert a
// cryptographic property.

#include <Identity.h>
#include <Utils.h>

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

// Stands in for ed25519_derive_pub().
void derivePub(uint8_t* pub, const uint8_t* prv) {
  derive(pub, PUB_KEY_SIZE, prv, PRV_KEY_SIZE, nullptr, 0);
}

}  // namespace

namespace mesh {

Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

bool Identity::readFrom(Stream& s) {
  return (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
}

bool Identity::writeTo(Stream& s) const {
  return (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
}

void Identity::printTo(Stream& s) const {
  Utils::printHex(s, pub_key, PUB_KEY_SIZE);
}

bool Identity::verify(const uint8_t* sig, const uint8_t* message, int msg_len) const {
  // Cannot be checked without the private key, so accept anything. A test that
  // needs to tell a good signature from a bad one must not use this stub.
  return true;
}

LocalIdentity::LocalIdentity() { memset(prv_key, 0, sizeof(prv_key)); }

// The real check derives the public key and refuses a 00 or FF prefix, and a known
// test key pair. Here the derived stand-in key is checked for the same prefixes.
bool LocalIdentity::validatePrivateKey(const uint8_t prv[64]) {
  uint8_t pub[PUB_KEY_SIZE];
  derivePub(pub, prv);
  return pub[0] != 0x00 && pub[0] != 0xFF;
}

bool LocalIdentity::readFrom(Stream& s) {
  bool success = (s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.readBytes(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  return success;
}

bool LocalIdentity::writeTo(Stream& s) const {
  bool success = (s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE);
  success = success && (s.write(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE);
  return success;
}

void LocalIdentity::printTo(Stream& s) const {
  s.print("pub_key: "); Utils::printHex(s, pub_key, PUB_KEY_SIZE); s.print("\n");
  s.print("prv_key: "); Utils::printHex(s, prv_key, PRV_KEY_SIZE); s.print("\n");
}

size_t LocalIdentity::writeTo(uint8_t* dest, size_t max_len) const {
  if (max_len < PRV_KEY_SIZE) return 0;
  if (max_len < PRV_KEY_SIZE + PUB_KEY_SIZE) {
    memcpy(dest, prv_key, PRV_KEY_SIZE);
    return PRV_KEY_SIZE;
  }
  memcpy(dest, prv_key, PRV_KEY_SIZE);
  memcpy(&dest[PRV_KEY_SIZE], pub_key, PUB_KEY_SIZE);
  return PRV_KEY_SIZE + PUB_KEY_SIZE;
}

void LocalIdentity::readFrom(const uint8_t* src, size_t len) {
  if (len == PRV_KEY_SIZE + PUB_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    memcpy(pub_key, &src[PRV_KEY_SIZE], PUB_KEY_SIZE);
  } else if (len == PRV_KEY_SIZE) {
    memcpy(prv_key, src, PRV_KEY_SIZE);
    derivePub(pub_key, prv_key);
  }
}

void LocalIdentity::sign(uint8_t* sig, const uint8_t* message, int msg_len) const {
  derive(sig, SIGNATURE_SIZE, prv_key, PRV_KEY_SIZE, message, (size_t)msg_len);
}

void LocalIdentity::calcSharedSecret(uint8_t* secret, const uint8_t* other_pub_key) const {
  derive(secret, PUB_KEY_SIZE, prv_key, PRV_KEY_SIZE, other_pub_key, PUB_KEY_SIZE);
}

}  // namespace mesh
