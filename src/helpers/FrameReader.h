#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Reads the fields of a frame in order, and never past its end.
//
//   FrameReader r(cmd_frame, len, 1);   // the fields start behind the code byte
//   uint32_t secs = r.u32();
//   uint8_t* key = r.bytes(PUB_KEY_SIZE);
//   if (r.has(4)) alt = r.i32();        // an optional field
//
// A read that would go past `len` returns 0 (or nullptr), leaves the cursor where it
// is and clears ok(), which stays false. Values of more than one byte are copied
// with memcpy, so they come out in the target's byte order, as the code that read
// cmd_frame at fixed offsets got them.
//
// The buffer must have room for len + 1 bytes when restAsCString() is used: it
// writes the terminator at buf[len]. The companion's cmd_frame is
// MAX_FRAME_SIZE + 1 bytes for that reason.

#ifdef FRAME_READER_COUNT_OVERRUNS
extern int frame_reader_overruns;   // host tests count the reads that ran out
#endif

class FrameReader {
public:
  FrameReader(uint8_t* buf, size_t len, size_t pos = 0)
    : _buf(buf), _len(len), _pos(pos <= len ? pos : len), _ok(pos <= len) { }

  uint8_t u8() { return fits(1) ? _buf[_pos++] : 0; }
  uint16_t u16() { uint16_t v = 0; copy(&v, sizeof(v)); return v; }
  uint32_t u32() { uint32_t v = 0; copy(&v, sizeof(v)); return v; }
  int32_t i32() { int32_t v = 0; copy(&v, sizeof(v)); return v; }

  // n bytes at the cursor, inside the frame; nullptr if they are not all there.
  uint8_t* bytes(size_t n) {
    if (!fits(n)) return nullptr;
    uint8_t* p = &_buf[_pos];
    _pos += n;
    return p;
  }
  void skip(size_t n) { if (fits(n)) _pos += n; }

  // The rest of the frame as a C string: writes 0 at buf[len] and consumes it all.
  char* restAsCString() {
    _buf[_len] = 0;
    char* p = (char*)&_buf[_pos];
    _pos = _len;
    return p;
  }

  bool has(size_t n) const { return _ok && n <= _len - _pos; }
  size_t remaining() const { return _len - _pos; }
  size_t pos() const { return _pos; }
  bool ok() const { return _ok; }

private:
  uint8_t* _buf;
  size_t _len;
  size_t _pos;
  bool _ok;

  bool fits(size_t n) {
    if (has(n)) return true;
    _ok = false;
#ifdef FRAME_READER_COUNT_OVERRUNS
    frame_reader_overruns++;
#endif
    return false;
  }
  void copy(void* dest, size_t n) {
    if (fits(n)) {
      memcpy(dest, &_buf[_pos], n);
      _pos += n;
    }
  }
};
