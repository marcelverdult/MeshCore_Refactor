// The parts of a variant's target.cpp that every board with a RadioLib radio
// writes the same way, defined once here. The functions are weak: a variant (or a
// downstream fork's variant) that still defines its own keeps compiling, and its
// definition takes precedence. WRAPPER_CLASS is what tells a RadioLib variant
// apart; the ESP-NOW variants have no LoRa radio and define their own.
#include "target.h"

#ifdef WRAPPER_CLASS

#include <helpers/radiolib/RadioLibWrappers.h>

// The radio, its wrapper and the RTC clock, for the variants whose target.h
// asks for them with RADIOLIB_TARGET_GLOBALS. Built from the board's
// description: the P_LORA_* pins and the platform. Not weak, unlike the
// functions below: a weak object is still constructed here, into the
// variant's object where the variant defines its own, so a variant that keeps
// its globals simply does not define the macro.
#ifdef RADIOLIB_TARGET_GLOBALS

#if !defined(NRF52_PLATFORM)
  #error "RADIOLIB_TARGET_GLOBALS: the radio globals are described for nRF52 only so far; define them in target.cpp"
#endif
#if defined(USE_SX1276)
  #error "RADIOLIB_TARGET_GLOBALS: the SX1276 pin order is not described yet; define the radio in target.cpp"
#endif

#include <SPI.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/AutoDiscoverRTCClock.h>

// On nRF52 the radio is on SPI, passed to RadioLib, so that std_init() and not
// RadioLib starts the bus.
static RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#endif

// A new random identity, from the noise of the radio radio_driver wraps (which
// is the variant's global radio). Slow, so only for creating the node's identity.
__attribute__((weak)) mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio_driver);
  return mesh::LocalIdentity(&rng);
}

// The radio_init() of the nRF52 variants with an SX1262: start the RTC, then the
// radio on the default SPI bus. Only there, because elsewhere the variants also
// start a fallback clock or use another SPI bus, which they still write out
// themselves; a board there without its own radio_init() fails to link instead
// of getting this one.
#if defined(NRF52_PLATFORM) && defined(USE_SX1262)

#include <SPI.h>
#include <Wire.h>

// The radio a driver wraps, as the variant's RADIO_CLASS (which it is, since the
// variant constructs radio_driver from its global radio).
struct RadioLibTargetAccess {
  template <class RADIO>
  static RADIO& radio(RadioLibWrapper& driver) { return static_cast<RADIO&>(*driver._radio); }
};

__attribute__((weak)) bool radio_init() {
  rtc_clock.begin(Wire);
  return RadioLibTargetAccess::radio<RADIO_CLASS>(radio_driver).std_init(&SPI);
}

#endif

#endif
