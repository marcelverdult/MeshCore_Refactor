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
// description: the P_LORA_* pins, the chip, the platform, and on ESP32
// P_LORA_SCLK (an own SPI bus) and P_LORA_SPI_HOST (its host, if not the
// SPIClass default). Not weak, unlike the functions below: a weak object is
// still constructed here, into the variant's object where the variant defines
// its own, so a variant that keeps its globals simply does not define the macro.
#ifdef RADIOLIB_TARGET_GLOBALS

#if !defined(NRF52_PLATFORM) && !defined(ESP32)
  #error "RADIOLIB_TARGET_GLOBALS: the radio globals are described for nRF52 and ESP32 only so far; define them in target.cpp"
#endif

#include <SPI.h>
#include <helpers/AutoDiscoverRTCClock.h>
#if defined(ESP32)
  #include <helpers/ESP32Board.h>
#else
  #include <helpers/ArduinoHelpers.h>
#endif

// The pins in the order the chip's Module takes them.
#if defined(USE_SX1276)
  #define RADIOLIB_TARGET_PINS P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1
#else
  #define RADIOLIB_TARGET_PINS P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY
#endif

// On nRF52 the radio is on SPI, passed to RadioLib, so that std_init() and not
// RadioLib starts the bus. On ESP32 it has a bus of its own where the board
// names the SPI pins, and otherwise RadioLib starts its default bus itself.
#if defined(ESP32)
  #if defined(P_LORA_SCLK)
    #if defined(P_LORA_SPI_HOST)
      static SPIClass spi(P_LORA_SPI_HOST);
    #else
      static SPIClass spi;
    #endif
    static RADIO_CLASS radio = new Module(RADIOLIB_TARGET_PINS, spi);
  #else
    static RADIO_CLASS radio = new Module(RADIOLIB_TARGET_PINS);
  #endif
#else
  static RADIO_CLASS radio = new Module(RADIOLIB_TARGET_PINS, SPI);
#endif

WRAPPER_CLASS radio_driver(radio, board);

#if defined(ESP32)
  ESP32RTCClock fallback_clock;
#else
  VolatileRTCClock fallback_clock;
#endif
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// The radio_init() of these variants on ESP32 with a chip that has std_init():
// start the fallback clock and the RTC, then the radio on its bus.
#if defined(ESP32) && (defined(USE_SX1262) || defined(USE_SX1268) || defined(USE_SX1276))

#include <Wire.h>

__attribute__((weak)) bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

#endif

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
