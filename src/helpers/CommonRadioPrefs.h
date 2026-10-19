#pragma once
#include "ConfigSerializer.h"
#include "KeyValueStore.h"
#include <RadioPrefsFields.h>

/**
 * \brief  The "radio" object of /prefs.json, the radio commands of the CLI and the
 *         key/value access boards use, all bound to one RadioPrefsFields.
 *
 * NodePrefs owns the fields and an instance of this class bound to them. Setters
 * mark this object dirty, which NodePrefs::isDirty() reports, so the owner knows
 * the prefs need saving.
 */
class CommonRadioPrefs : public ConfigSerializer, public KeyValueStore {
  RadioPrefsFields* _f;
  bool _is_dirty = false;

protected:
  // JSON keys and their order are the persisted format: do not rename or reorder.
  void structure() override {
    def("freq", _f->freq);
    def("bw", _f->bw);
    def("sf", _f->sf);
    def("cr", _f->cr);
    def("cad", _f->cad_enabled);
    def("int_thr", _f->interference_threshold);
    def("rxgain", _f->rx_boosted_gain);
    def("fem_rxgain", _f->radio_fem_rxgain);
    def("fem_txgain", _f->radio_fem_txgain);
    def("tx", _f->tx_power_dbm);
    def("af", _f->airtime_factor);
    def("rxdelay", _f->rx_delay_base);
    def("f_txdelay", _f->tx_delay_factor);
    def("d_txdelay", _f->direct_tx_delay_factor);
    def("agc_int", _f->agc_reset_interval);
    def("hash_mode", _f->path_hash_mode);
    def("multi_ack", _f->multi_acks);
  }

public:
  explicit CommonRadioPrefs(RadioPrefsFields& fields) : _f(&fields) { }

  void markDirty() { _is_dirty = true; }
  void clearDirty() override { _is_dirty = false; }
  bool isDirty() const override { return _is_dirty; }

  float getFreq() const { return _f->freq; }
  void setFreq(float f) { _f->freq = f; markDirty(); }

  float getBandwidth() const { return _f->bw; }
  void setBandwidth(float bw) { _f->bw = bw; markDirty(); }

  uint8_t getSpreadFactor() const { return _f->sf; }
  void setSpreadFactor(uint8_t sf) { _f->sf = sf; markDirty(); }

  uint8_t getCodingRate() const { return _f->cr; }
  void setCodingRate(uint8_t cr) { _f->cr = cr; markDirty(); }

  float getAirtimeFactor() const { return _f->airtime_factor; }
  void setAirtimeFactor(float af) { _f->airtime_factor = af; markDirty(); }

  bool isCadEnabled() const { return _f->cad_enabled; }
  void setCadEnabled(bool en) { _f->cad_enabled = en; markDirty(); }

  uint8_t getIntThresh() const { return _f->interference_threshold; }
  void setIntThresh(uint8_t t) { _f->interference_threshold = t; markDirty(); }

  uint8_t getRxGain() const { return _f->rx_boosted_gain; }
  void setRxGain(uint8_t g) { _f->rx_boosted_gain = g; markDirty(); }

  uint8_t getTxPower() const { return _f->tx_power_dbm; }
  void setTxPower(uint8_t dbm) { _f->tx_power_dbm = dbm; markDirty(); }

  float getRxDelay() const { return _f->rx_delay_base; }
  void setRxDelay(float d) { _f->rx_delay_base = d; markDirty(); }

  // stored in units of 4 seconds
  uint16_t getAgcResetInt() const { return _f->agc_reset_interval * 4; }
  void setAgcResetInt(uint16_t secs) { _f->agc_reset_interval = secs / 4; markDirty(); }

  uint8_t getHashMode() const { return _f->path_hash_mode; }
  void setHashMode(uint8_t m) { _f->path_hash_mode = m; markDirty(); }

  uint8_t getMultiAcks() const { return _f->multi_acks; }
  void setMultiAcks(uint8_t m) { _f->multi_acks = m; markDirty(); }

  float getFloodTxDelay() const { return _f->tx_delay_factor; }
  void setFloodTxDelay(float d) { _f->tx_delay_factor = d; markDirty(); }

  float getDirectTxDelay() const { return _f->direct_tx_delay_factor; }
  void setDirectTxDelay(float d) { _f->direct_tx_delay_factor = d; markDirty(); }

  uint8_t getFEMRxGain() const { return _f->radio_fem_rxgain; }
  void setFEMRxGain(uint8_t g) { _f->radio_fem_rxgain = g; markDirty(); }

  uint8_t getFEMTxGain() const { return _f->radio_fem_txgain; }
  void setFEMTxGain(uint8_t g) { _f->radio_fem_txgain = g; markDirty(); }

  bool handleCommand(const char* command, uint32_t sender_timestamp, char* reply);

  bool setByKey(const char* key, const char* value) override;   // for dynamic key/value access
  bool getByKey(const char* key, char* value, size_t max_len) override;
};
