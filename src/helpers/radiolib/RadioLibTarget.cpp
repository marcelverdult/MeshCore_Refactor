// The parts of a variant's target.cpp that every board with a RadioLib radio
// writes the same way, defined once here. They are weak: a variant (or a
// downstream fork's variant) that still defines its own keeps compiling, and its
// definition takes precedence. WRAPPER_CLASS is what tells a RadioLib variant
// apart; the ESP-NOW variants have no LoRa radio and define their own.
#include "target.h"

#ifdef WRAPPER_CLASS

#include <helpers/radiolib/RadioLibWrappers.h>

// A new random identity, from the noise of the radio radio_driver wraps (which
// is the variant's global radio). Slow, so only for creating the node's identity.
__attribute__((weak)) mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio_driver);
  return mesh::LocalIdentity(&rng);
}

#endif
