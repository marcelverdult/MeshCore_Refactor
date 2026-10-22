#pragma once

#include <stdint.h>

namespace mesh {

/**
 * \brief  Has a scheduled millis() deadline passed?
 * \param  now       the current millis() reading
 * \param  deadline  a deadline previously formed as 'millis() + interval'
 * \returns  true once now is strictly past deadline
 *
 * Periodic work is scheduled by storing 'millis() + interval' and asking once per loop
 * pass whether that moment has arrived. Asking with a plain 'now > deadline' breaks in
 * the last interval before the 32-bit counter reaches 2^32: the stored deadline has
 * already wrapped to a small number while now is still large, so the compare is true on
 * every pass and the block runs on each pass instead of once per interval, until the
 * counter wraps as well. Comparing the wrapped difference instead stays correct, because
 * for two readings less than 2^31 apart the modular subtraction is their real distance.
 *
 * The horizon is therefore half the counter: a deadline up to 2^31 - 1 ahead reads as
 * future, and beyond that future and past are no longer distinguishable. Every interval
 * scheduled in this firmware is orders of magnitude inside that horizon.
 *
 * The widths are fixed deliberately rather than written as long. long is 32 bits on the
 * firmware targets but 64 bits on the test host, and a 64-bit deadline does not preserve
 * the behaviour at the wrap, it changes it into a stall for a whole counter period. Fixed
 * widths give the host test the same arithmetic the target performs.
 *
 * This is the question Dispatcher::millisHasNowPassed answers for code that holds a
 * Dispatcher, written here for code that does not, and spelled without that version's
 * conversion of an out-of-range unsigned value to a signed one, which the standard leaves
 * to the implementation. test_millis_deadline pins the two forms to each other.
 */
inline bool millisHasPassed(uint32_t now, uint32_t deadline) {
  const uint32_t elapsed = now - deadline;
  return elapsed != 0 && elapsed < 0x80000000u;
}

}  // namespace mesh
