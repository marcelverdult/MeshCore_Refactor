#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Mock AES128 class for testing
// Not a cipher: each block is XORed with the key, so the output depends only on
// key and input. Utils::encrypt() writes straight into the packet buffer, and a
// mock that left the output untouched gave packets whatever that buffer held
// before (docs/refactoring/0010-companion-command-table.md, open finding 3).
// XOR is its own inverse, so decryptBlock() undoes encryptBlock().
class AES128 {
  uint8_t _key[16] = {};

  void xorBlock(uint8_t* output, const uint8_t* input) const {
    for (int i = 0; i < 16; i++) output[i] = input[i] ^ _key[i];
  }

public:
  void setKey(const uint8_t* key, size_t keySize) {
    memset(_key, 0, sizeof(_key));
    memcpy(_key, key, keySize < sizeof(_key) ? keySize : sizeof(_key));
  }
  void encryptBlock(uint8_t* output, const uint8_t* input) { xorBlock(output, input); }
  void decryptBlock(uint8_t* output, const uint8_t* input) { xorBlock(output, input); }
};
