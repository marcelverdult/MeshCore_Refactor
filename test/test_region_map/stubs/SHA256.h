#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// A deterministic stand-in for the SHA256 class RegionMap and TransportKeyStore use.
// It is not cryptographic. It only has to give a different key per region name and a
// different transport code per key, so a test can tell one region from another. The
// shared mock in test/mocks leaves finalizeHMAC() empty, which would hand
// calcTransportCode() an uninitialised code.
class SHA256 {
  uint8_t _state[32];
  size_t _len;

  void mix(const uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
      uint8_t b = bytes[i];
      _state[_len % 32] ^= b;
      _state[(_len + 1) % 32] += (uint8_t)((b >> 1) | (b << 7));
      _len++;
    }
  }

public:
  SHA256() : _len(0) { memset(_state, 0, sizeof(_state)); }

  void update(const void* data, size_t len) { mix((const uint8_t*)data, len); }

  void finalize(void* hash, size_t hashLen) {
    for (size_t i = 0; i < hashLen; i++) ((uint8_t*)hash)[i] = _state[i % 32];
  }

  void resetHMAC(const void* key, size_t keyLen) {
    memset(_state, 0, sizeof(_state));
    _len = 0;
    mix((const uint8_t*)key, keyLen);
  }

  void finalizeHMAC(const void* key, size_t keyLen, void* hash, size_t hashLen) {
    mix((const uint8_t*)key, keyLen);
    finalize(hash, hashLen);
  }
};
