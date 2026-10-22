#pragma once

#include <stddef.h>
#include <stdint.h>

// RegionMap::load() and save() name the file system types, so they have to exist for
// the file to compile. The test never loads or saves, hence a file system with
// nothing in it.
namespace fs {

class File {
public:
  explicit operator bool() const { return false; }
  int read(uint8_t*, size_t) { return 0; }
  size_t write(const uint8_t*, size_t) { return 0; }
  void close() {}
};

class FS {
public:
  bool exists(const char*) { return false; }
  File open(const char*, const char* = "r", bool = false) { return File(); }
  bool remove(const char*) { return false; }
  bool mkdir(const char*) { return false; }
};

}  // namespace fs

using fs::File;
