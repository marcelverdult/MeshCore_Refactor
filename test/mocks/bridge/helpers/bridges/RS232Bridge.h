#pragma once

// Stand-in for the repeater's RS232 bridge, so the repeater builds on the host with
// WITH_RS232_BRIDGE set. It keeps the real bridge's interface and does nothing but
// remember that it was started, in the shared event log, so a test can see when the
// repeater's begin() starts the bridge relative to the rest of the boot.
//
// It sits in a directory of its own that the "full" repeater environment puts ahead
// of src on the include path, so `helpers/bridges/RS232Bridge.h` resolves here and
// nowhere else sees it.

#include <Mesh.h>
#include <RTClib.h>
#include <helpers/AbstractBridge.h>
#include <helpers/CommonCLI.h>

#include <EventLog.h>

class RS232Bridge : public AbstractBridge {
  bool _running = false;

public:
  // WITH_RS232_BRIDGE names a serial port on a board; on the host it is just a
  // number, and the stand-in does not use it.
  RS232Bridge(NodePrefs* prefs, int serial, mesh::PacketManager* mgr, mesh::RTCClock* rtc) { }

  void begin() override { _running = true; logEvent("bridge.begin"); }
  void end() override { _running = false; logEvent("bridge.end"); }
  bool isRunning() const override { return _running; }
  void loop() override { }
  void sendPacket(mesh::Packet* packet) override { }
  void onPacketReceived(mesh::Packet* packet) override { }
};
