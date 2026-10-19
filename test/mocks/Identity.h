#pragma once

#include <cstdint>
#include <cstring>
#include "Stream.h"
#include "Utils.h"

namespace mesh {

class Identity {
public:
  uint8_t pub_key[PUB_KEY_SIZE];

  Identity() {
    std::memset(pub_key, 0, sizeof(pub_key));
  }

  explicit Identity(const uint8_t* src) {
    std::memcpy(pub_key, src, PUB_KEY_SIZE);
  }

  bool verify(const uint8_t*, const uint8_t*, int) const {
    return true;
  }

  // Copies of the real serialisation in src/Identity.cpp: a short read or a
  // short write is a failure, which is what the file based tests rely on.
  bool readFrom(Stream& s) {
    return s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE;
  }

  bool writeTo(Stream& s) const {
    return s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE;
  }
};

class LocalIdentity : public Identity {
public:
  uint8_t prv_key[PRV_KEY_SIZE];

  LocalIdentity() : Identity() {
    std::memset(prv_key, 0, sizeof(prv_key));
  }

  void sign(uint8_t* sig, const uint8_t*, int) const {
    std::memset(sig, 0x5A, SIGNATURE_SIZE);
  }

  void calcSharedSecret(uint8_t* secret, const uint8_t*) const {
    std::memset(secret, 0x11, PUB_KEY_SIZE);
  }

  bool readFrom(Stream& s) {
    bool success = s.readBytes(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE;
    success = success && s.readBytes(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE;
    return success;
  }

  bool writeTo(Stream& s) const {
    bool success = s.write(pub_key, PUB_KEY_SIZE) == PUB_KEY_SIZE;
    success = success && s.write(prv_key, PRV_KEY_SIZE) == PRV_KEY_SIZE;
    return success;
  }
};

}
