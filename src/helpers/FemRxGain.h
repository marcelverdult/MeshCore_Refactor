#pragma once

#include <stdio.h>
#include <string.h>
#include <helpers/KeyValueStore.h>

/*
 * The RX gain (LNA) setting of a LoRa front end module: the `fem_rxgain` key in
 * the key/value store the role attaches, and the CLI commands
 * `get radio.fem.rxgain` and `set radio.fem.rxgain on|off`.
 *
 * BOARD is the board class. It needs a public `loRaFEMControl` with
 * isLnaCanControl(), and the two members setLoRaFemLnaEnabled(bool), which
 * returns whether the LNA was switched, and isLoRaFemLnaEnabled(). These may be
 * private if the board names this class a friend. The board keeps one FemRxGain
 * member and forwards attachDynamicPrefs() and handleCommand() to it:
 *
 *   class MyBoard : public ESP32Board {
 *     friend class FemRxGain<MyBoard>;
 *     FemRxGain<MyBoard> fem_rxgain;
 *     ...
 *   void MyBoard::attachDynamicPrefs(KeyValueStore* prefs) { fem_rxgain.attachPrefs(*this, prefs); }
 *
 * The store is NULL until attachPrefs() is called. A role that calls
 * handleCommand() without attaching one gets the same replies, and `set` then
 * switches the LNA and stores nothing.
 */
template <class BOARD>
class FemRxGain {
  KeyValueStore* _prefs = NULL;

public:
  void attachPrefs(BOARD& board, KeyValueStore* prefs) {
    _prefs = prefs;

    char radio_fem_rxgain[8] = { 0 };
    _prefs->getByKey("fem_rxgain", radio_fem_rxgain, 7);  // get initial values

    board.setLoRaFemLnaEnabled(strcmp(radio_fem_rxgain, "1") == 0);
  }

  bool handleCommand(BOARD& board, const char* command, char* reply) {
    if (strcmp(command, "get radio.fem.rxgain") == 0) {
      if (!board.loRaFEMControl.isLnaCanControl()) {
        strcpy(reply, "Error: unsupported");
      } else {
        sprintf(reply, "> %s", board.isLoRaFemLnaEnabled() ? "on" : "off");
      }
      return true;
    }
    if (memcmp(command, "set radio.fem.rxgain ", 21) == 0) {
      if (!board.loRaFEMControl.isLnaCanControl()) {
        strcpy(reply, "Error: unsupported");
      } else if (memcmp(&command[21], "on", 2) == 0) {
        if (board.setLoRaFemLnaEnabled(true)) {
          if (_prefs) _prefs->setByKey("fem_rxgain", "1");
          strcpy(reply, "OK - LoRa FEM RX gain on");
        } else {
          strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
        }
      } else if (memcmp(&command[21], "off", 3) == 0) {
        if (board.setLoRaFemLnaEnabled(false)) {
          if (_prefs) _prefs->setByKey("fem_rxgain", "0");
          strcpy(reply, "OK - LoRa FEM RX gain off");
        } else {
          strcpy(reply, "Error: failed to apply LoRa FEM RX gain");
        }
      } else {
        strcpy(reply, "Error: state must be on or off");
      }
      return true;
    }

    return false; // not handled
  }
};
