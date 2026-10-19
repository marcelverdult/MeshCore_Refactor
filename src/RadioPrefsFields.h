#pragma once

#include <stdint.h>

/**
 * \brief  The radio settings every node role persists: LoRa modulation, TX power,
 *         timing factors and radio front-end options.
 *
 * Defined once here and inherited by both NodePrefs classes (the server roles'
 * one in helpers/CommonNodePrefs.h and the companion radio's one), so a radio setting has
 * exactly one name, type and default for all firmwares. Inheriting, rather than
 * holding it as a member, keeps the existing `prefs.freq` style of access working
 * for every caller.
 *
 * The core reads the tuning values it needs from here once a node attaches its
 * settings with Dispatcher::useRadioPrefs().
 */
struct RadioPrefsFields {
  // ordered by size, so the struct has no padding inside
  float freq = 0;                    // MHz
  float bw = 0;                      // kHz
  float airtime_factor = 0;          // duty cycle budget: 100% / (airtime_factor + 1)
  float rx_delay_base = 0;
  float tx_delay_factor = 0;         // flood retransmit delay factor
  float direct_tx_delay_factor = 0;  // direct retransmit delay factor
  uint8_t sf = 0;                    // spreading factor
  uint8_t cr = 0;                    // coding rate (4/cr)
  int8_t tx_power_dbm = 0;
  uint8_t interference_threshold = 0;
  uint8_t agc_reset_interval = 0;    // seconds / 4
  uint8_t rx_boosted_gain = 0;       // RX boosted gain mode (0 = power saving, 1 = boosted)
  uint8_t radio_fem_rxgain = 0;      // external LoRa FEM RX gain (LNA)
  uint8_t radio_fem_txgain = 0;      // external LoRa FEM TX gain
  uint8_t path_hash_mode = 0;        // which path hash size to use when sending
  uint8_t multi_acks = 0;
  uint8_t cad_enabled = 0;           // hardware Channel Activity Detection before TX (boolean)
};
