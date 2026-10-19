#pragma once

// An in-memory stand-in for a variant's file system, so the server roles and the
// helpers that persist their state (CommonCLI, ClientACL, RegionMap, IdentityStore)
// run in the native env. It implements the calls the firmware actually makes, in
// the forms the native build reaches: the platform guards for NRF52, STM32 and
// RP2040 are not set there, so the "w"/"a"/"r" modes with the optional create flag
// are the ones that matter.
//
// Files live in one map that survives a close, so a test can write, reopen and read
// back, and reboot a role by constructing it again against the same instance.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Stream.h"

#ifndef FILE_O_WRITE
  #define FILE_O_WRITE 1
#endif

namespace fs { class FS; }
using MockFS = fs::FS;

class File : public Stream {
  fs::FS* _fs = nullptr;
  std::string _name;
  size_t _pos = 0;
  bool _valid = false;
  bool _writable = false;

  std::vector<uint8_t>* data() const;

public:
  File() { }
  File(fs::FS* fsp, const std::string& name, bool writable)
    : _fs(fsp), _name(name), _pos(0), _valid(true), _writable(writable) { }

  operator bool() const { return _valid; }

  // Stream
  int available() override;
  int read() override;
  int peek() override { return -1; }

  // Print
  size_t write(uint8_t b) override { return write(&b, (size_t)1); }
  size_t write(const uint8_t* src, size_t len) override;

  // The cores accept any buffer type here, and the firmware relies on that.
  int read(void* dest, size_t len);
  size_t write(const void* src, size_t len) {
    return write(static_cast<const uint8_t*>(src), len);
  }

  void close() { _valid = false; }

  // Test helper: the position reached so far.
  size_t position() const { return _pos; }
};

namespace fs {

class FS {
public:
  std::map<std::string, std::vector<uint8_t>> files;

  File open(const char* name) { return open(name, "r"); }

  File open(const char* name, const char* mode, bool create = false) {
    std::string n(name);
    bool writing = (mode[0] == 'w' || mode[0] == 'a');
    if (!writing) {
      if (files.find(n) == files.end()) return File();   // missing: a falsy File
      return File(this, n, false);
    }
    if (mode[0] == 'w') files[n].clear();                // truncate
    else files[n];                                       // append: create if absent
    File f(this, n, true);
    if (mode[0] == 'a') f = File(this, n, true);
    return f;
  }

  // The NRF52 and STM32 branches reach this form.
  File open(const char* name, int flags) {
    return open(name, flags == FILE_O_WRITE ? "a" : "r", true);
  }

  // Directories are not modelled: the firmware only ever creates one for the
  // identity store, and a flat map needs no directory to hold "/dir/file".
  bool mkdir(const char* path) { return true; }

  bool exists(const char* name) { return files.find(std::string(name)) != files.end(); }
  bool remove(const char* name) { return files.erase(std::string(name)) > 0; }
  bool format() { files.clear(); return true; }

  // Test helpers.
  void reset() { files.clear(); }
  size_t sizeOf(const char* name) {
    auto it = files.find(std::string(name));
    return it == files.end() ? 0 : it->second.size();
  }
};

}  // namespace fs

inline std::vector<uint8_t>* File::data() const {
  if (!_fs || !_valid) return nullptr;
  auto it = _fs->files.find(_name);
  return it == _fs->files.end() ? nullptr : &it->second;
}

inline int File::read(void* dest, size_t len) {
  auto* d = data();
  if (!d) return 0;
  size_t n = d->size() > _pos ? d->size() - _pos : 0;
  if (n > len) n = len;
  if (n) memcpy(dest, d->data() + _pos, n);
  _pos += n;
  return (int)n;
}

inline int File::read() {
  uint8_t b;
  return read(&b, (size_t)1) == 1 ? (int)b : -1;
}

inline size_t File::write(const uint8_t* src, size_t len) {
  auto* d = data();
  if (!d || !_writable) return 0;
  d->insert(d->end(), src, src + len);
  _pos = d->size();
  return len;
}

inline int File::available() {
  auto* d = data();
  if (!d) return 0;
  return (int)(d->size() > _pos ? d->size() - _pos : 0);
}
