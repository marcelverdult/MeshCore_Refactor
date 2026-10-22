#pragma once

#include <stdint.h>
#include "MeshCore.h"

// Decides from a chip's preamble and header IRQ bits whether a packet is on the
// air, which the dispatcher uses to hold back a transmission. A preamble counts
// for preambleMillis, a valid header for maxPayloadMillis (both set by the
// wrapper from the modem settings); past a deadline the reception is given up so
// a stuck flag cannot block the transmitter for good.
//
// Shared by every chip class with a preamble IRQ. It only sees three booleans and
// the time, so the chips keep their own register access and IRQ bit names, and
// the logic can be tested on the host without a radio.
class RxActivityTracker {
  uint32_t _preambleMillis = 66;
  uint32_t _maxPayloadMillis = 3934;
  uint32_t _activityAt = 0;    // 0 means "not started"
  bool _headerSeen = false;

public:
  enum Result : uint8_t {
    IDLE,            // nothing on the air
    RECEIVING,       // a packet is on the air
    CLEAR_PREAMBLE,  // not receiving; the caller clears the preamble bit
    CLEAR_ALL,       // not receiving; the caller clears the preamble and header bits
  };

  Result update(bool preamble, bool header, bool headerErr, uint32_t now) {
    if (headerErr) {
      _activityAt = 0;
      _headerSeen = false;
      return CLEAR_ALL;
    }
    if (!header && _headerSeen) {
      // something cleared the header flag, reset our state.
      _activityAt = 0; _headerSeen = false;
      return IDLE;
    }
    if (header) {
      if (!_headerSeen) { _headerSeen = true; _activityAt = now; }
      if (now - _activityAt > _maxPayloadMillis) {
        MESH_DEBUG_PRINTLN("Clearing header IRQ after %ums", _maxPayloadMillis);
        _activityAt = 0; _headerSeen = false;
        return CLEAR_ALL;
      }
      return RECEIVING;
    }
    if (preamble) {
      if (_activityAt == 0) _activityAt = now;
      if (now - _activityAt > _preambleMillis) {
        _activityAt = 0;
        MESH_DEBUG_PRINTLN("Clearing preamble IRQ after %ums", _preambleMillis);
        return CLEAR_PREAMBLE;
      }
      return RECEIVING;
    }
    _activityAt = 0; _headerSeen = false;
    return IDLE;
  }

  void setPreambleMillis(uint32_t preambleMillis) {
    _preambleMillis = preambleMillis;
    MESH_DEBUG_PRINTLN("Set _preambleMillis=%u", _preambleMillis);
  }
  void setMaxPayloadMillis(uint32_t payloadMillis) {
    _maxPayloadMillis = payloadMillis;
    MESH_DEBUG_PRINTLN("Set _maxPayloadMillis=%u", _maxPayloadMillis);
  }
};
