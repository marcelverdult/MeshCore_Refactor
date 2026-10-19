#include "CommonRadioPrefs.h"
#include "TxtDataHelpers.h"
#include "Utils.h"
#include "target.h"
#include <stddef.h>

namespace {

enum FieldType : uint8_t { FIELD_F32, FIELD_U8, FIELD_I8 };

// How the generic code handles a setting on the CLI.
enum CliKind : uint8_t {
  CLI_NONE,       // no generic command (none at all, or a special one in handleCommand())
  CLI_GET_ONLY,   // "get <name>" only
  CLI_FLOAT,      // "set <name> <value>": atof(), accepted if 0 <= value <= max (NaN and infinity rejected)
  CLI_UINT8,      // "set <name> <value>": atoi() into uint8_t, accepted if value <= max (max < 0: no limit)
  CLI_ON_OFF,     // "set <name> <value>": on if the value starts with "on", off otherwise
};

struct RadioSetting {
  const char* json_key;      // key in the "radio" object of /prefs.json (persisted format, never rename)
  uint8_t offset;            // the field in RadioPrefsFields
  FieldType type;
  const char* cli_name;      // command name, or NULL
  CliKind cli;
  float max;                 // upper limit, see CliKind
  const char* range_error;   // reply when a value is out of range
  bool key_value;            // readable and writable through the key/value store (used by boards)
};

#define FIELD(f)  offsetof(RadioPrefsFields, f)

/**
 * One row per radio setting. The order of the rows is the order of the keys in the
 * "radio" object of /prefs.json. A new setting needs a field in RadioPrefsFields and a
 * row here; settings whose CLI does not fit a CliKind get CLI_NONE and a branch in
 * handleCommand().
 */
const RadioSetting SETTINGS[] = {
  // json key      field                                type        CLI name           CLI kind      max   range error
  { "freq",       FIELD(freq),                       FIELD_F32, "freq",           CLI_GET_ONLY,  -1,  NULL, false },
  { "bw",         FIELD(bw),                         FIELD_F32, NULL,             CLI_NONE,      -1,  NULL, false },  // "radio"
  { "sf",         FIELD(sf),                         FIELD_U8,  NULL,             CLI_NONE,      -1,  NULL, false },  // "radio"
  { "cr",         FIELD(cr),                         FIELD_U8,  NULL,             CLI_NONE,      -1,  NULL, false },  // "radio"
  { "cad",        FIELD(cad_enabled),                FIELD_U8,  "cad",            CLI_ON_OFF,    -1,  NULL, false },
  { "int_thr",    FIELD(interference_threshold),     FIELD_U8,  "int.thresh",     CLI_UINT8,     -1,  NULL, false },
  { "rxgain",     FIELD(rx_boosted_gain),            FIELD_U8,  NULL,             CLI_NONE,      -1,  NULL, false },  // "radio.rxgain"
  { "fem_rxgain", FIELD(radio_fem_rxgain),           FIELD_U8,  NULL,             CLI_NONE,      -1,  NULL, true },   // board CLI
  { "fem_txgain", FIELD(radio_fem_txgain),           FIELD_U8,  NULL,             CLI_NONE,      -1,  NULL, true },   // board CLI
  { "tx",         FIELD(tx_power_dbm),               FIELD_I8,  NULL,             CLI_NONE,      -1,  NULL, false },  // "tx"
  { "af",         FIELD(airtime_factor),             FIELD_F32, NULL,             CLI_NONE,      -1,  NULL, false },  // "af", "dutycycle"
  { "rxdelay",    FIELD(rx_delay_base),              FIELD_F32, "rxdelay",        CLI_FLOAT,     20,  "Error, must be 0-20", false },
  { "f_txdelay",  FIELD(tx_delay_factor),            FIELD_F32, "txdelay",        CLI_FLOAT,      2,  "Error, must be 0-2", false },
  { "d_txdelay",  FIELD(direct_tx_delay_factor),     FIELD_F32, "direct.txdelay", CLI_FLOAT,      2,  "Error, must be 0-2", false },
  { "agc_int",    FIELD(agc_reset_interval),         FIELD_U8,  NULL,             CLI_NONE,      -1,  NULL, false },  // "agc.reset.interval"
  { "hash_mode",  FIELD(path_hash_mode),             FIELD_U8,  "path.hash.mode", CLI_UINT8,      2,  "Error, must be 0,1, or 2", false },
  { "multi_ack",  FIELD(multi_acks),                 FIELD_U8,  "multi.acks",     CLI_UINT8,     -1,  NULL, false },
};

const int NUM_SETTINGS = sizeof(SETTINGS) / sizeof(SETTINGS[0]);

// True if v is a number in [0, max]. NaN and infinity are recognised by their bits:
// nRF52 builds use -Ofast, and its -ffast-math lets float comparisons assume that
// NaN and infinity never occur, so a comparison alone may let them through.
bool inRange(float v, float max) {
  uint32_t bits;
  memcpy(&bits, &v, sizeof(bits));
  if ((bits & 0x7F800000) == 0x7F800000) return false;
  return v >= 0 && v <= max;
}

// "get <name>"
bool isGet(const char* command, const char* name) {
  return memcmp(command, "get ", 4) == 0 && strcmp(&command[4], name) == 0;
}

// "set <name> <value>": returns the value, or NULL if the command is something else
const char* setValue(const char* command, const char* name) {
  size_t len = strlen(name);
  if (memcmp(command, "set ", 4) != 0 || memcmp(&command[4], name, len) != 0 || command[4 + len] != ' ') return NULL;
  return &command[5 + len];
}

}  // namespace

void CommonRadioPrefs::structure() {
  for (int i = 0; i < NUM_SETTINGS; i++) {
    const RadioSetting& s = SETTINGS[i];
    uint8_t* field = (uint8_t *) _f + s.offset;
    switch (s.type) {
      case FIELD_F32: def(s.json_key, *(float *) field); break;
      case FIELD_U8:  def(s.json_key, *(uint8_t *) field); break;
      case FIELD_I8:  def(s.json_key, *(int8_t *) field); break;
    }
  }
}

bool CommonRadioPrefs::getByKey(const char* key, char* value, size_t max_len) {
  for (int i = 0; i < NUM_SETTINGS; i++) {
    const RadioSetting& s = SETTINGS[i];
    if (s.key_value && s.type == FIELD_U8 && strcmp(key, s.json_key) == 0) {
      snprintf(value, max_len, "%d", (uint32_t) *((uint8_t *) _f + s.offset));
      return true;
    }
  }
  return false;
}
bool CommonRadioPrefs::setByKey(const char* key, const char* value) {
  for (int i = 0; i < NUM_SETTINGS; i++) {
    const RadioSetting& s = SETTINGS[i];
    if (s.key_value && s.type == FIELD_U8 && strcmp(key, s.json_key) == 0) {
      *((uint8_t *) _f + s.offset) = atoi(value);
      markDirty();
      return true;
    }
  }
  return false;
}

bool CommonRadioPrefs::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  // Settings whose CLI does not fit a CliKind.
  if (strcmp(command, "get radio") == 0) {
    char freq[16], bw[16];
    strcpy(freq, StrHelper::ftoa(getFreq()));
    strcpy(bw, StrHelper::ftoa3(getBandwidth()));
    sprintf(reply, "> %s,%s,%d,%d", freq, bw, (uint32_t)getSpreadFactor(), (uint32_t)getCodingRate());
    return true;
  }
  if (memcmp(command, "set radio ", 10) == 0) {
    char tmp[132];
    strcpy(tmp, &command[10]);
    const char *parts[4];
    int num = mesh::Utils::parseTextParts(tmp, parts, 4);
    float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
    float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
    uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
    uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      setSpreadFactor(sf);
      setCodingRate(cr);
      setFreq(freq);
      setBandwidth(bw);
      strcpy(reply, "OK - reboot to apply");
    } else {
      strcpy(reply, "Error, invalid radio params");
    }
    return true;
  }

  if (strcmp(command, "get af") == 0) {
    sprintf(reply, "> %s", StrHelper::ftoa(getAirtimeFactor()));
    return true;
  }
  if (memcmp(command, "set af ", 7) == 0) {
    char* end;
    float af = strtof(&command[7], &end);
    // inRange() rejects NaN and infinity by their bits. Two comparisons alone
    // cannot: no ordered comparison is true for a NaN, so "af < 0 || af > 9"
    // let "set af nan" through, which a T-Echo accepted and reported back as 0.
    if (end == &command[7] || !inRange(af, 9)) {
      strcpy(reply, "ERROR: af must be 0-9");
    } else {
      setAirtimeFactor(af);
      strcpy(reply, "OK");
    }
    return true;
  }

  if (strcmp(command, "get dutycycle") == 0) {
    float dc = 100.0f / (getAirtimeFactor() + 1.0f);
    int dc_int = (int)dc;
    int dc_frac = (int)((dc - dc_int) * 10.0f + 0.5f);
    sprintf(reply, "> %d.%d%%", dc_int, dc_frac);
    return true;
  }
  if (memcmp(command, "set dutycycle ", 14) == 0) {
    float dc = atof(&command[14]);
    if (!inRange(dc, 100) || dc < 1) {
      strcpy(reply, "ERROR: dutycycle must be 1-100");
    } else {
      setAirtimeFactor((100.0f / dc) - 1.0f);
      float actual = 100.0f / (getAirtimeFactor() + 1.0f);
      int a_int = (int)actual;
      int a_frac = (int)((actual - a_int) * 10.0f + 0.5f);
      sprintf(reply, "OK - %d.%d%%", a_int, a_frac);
    }
    return true;
  }

  if (strcmp(command, "get radio.rxgain") == 0) {
    sprintf(reply, "> %s", getRxGain() != 0 ? "on" : "off");
    return true;
  }
  if (memcmp(command, "set radio.rxgain ", 17) == 0) {
    bool enabled = memcmp(&command[17], "on", 2) == 0;
    setRxGain(enabled);
    if (radio_driver.setRxBoostedGainMode(enabled)) {
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error: unsupported");
    }
    return true;
  }

  if (memcmp(command, "get tx", 6) == 0 && (command[6] == 0 || command[6] == ' ')) {
    sprintf(reply, "> %d", (int32_t) getTxPower());
    return true;
  }
  if (memcmp(command, "set tx ", 7) == 0) {
    setTxPower(atoi(&command[7]));
    radio_driver.setTxPower(getTxPower());
    strcpy(reply, "OK");
    return true;
  }

  if (strcmp(command, "get agc.reset.interval") == 0) {
    sprintf(reply, "> %d", (uint32_t) getAgcResetInt());
    return true;
  }
  if (memcmp(command, "set agc.reset.interval ", 23) == 0) {
    int secs = atoi(&command[23]);
    secs = constrain(secs, 0, 255 * 4);
    setAgcResetInt((uint16_t) secs);
    sprintf(reply, "OK - interval rounded to %d", (uint32_t) getAgcResetInt());
    return true;
  }

  // Every other setting, driven by SETTINGS.
  for (int i = 0; i < NUM_SETTINGS; i++) {
    const RadioSetting& s = SETTINGS[i];
    if (s.cli == CLI_NONE) continue;
    uint8_t* field = (uint8_t *) _f + s.offset;

    if (isGet(command, s.cli_name)) {
      if (s.cli == CLI_ON_OFF) {
        sprintf(reply, "> %s", *field ? "on" : "off");
      } else if (s.type == FIELD_F32) {
        sprintf(reply, "> %s", StrHelper::ftoa(*(float *) field));
      } else {
        sprintf(reply, "> %d", (uint32_t) *field);
      }
      return true;
    }
    if (s.cli == CLI_GET_ONLY) continue;

    const char* value = setValue(command, s.cli_name);
    if (value == NULL) continue;
    if (s.cli == CLI_FLOAT) {
      float v = atof(value);
      if (!inRange(v, s.max)) {
        strcpy(reply, s.range_error);
        return true;
      }
      *(float *) field = v;
    } else if (s.cli == CLI_UINT8) {
      uint8_t v = atoi(value);
      if (s.max >= 0 && v > s.max) {
        strcpy(reply, s.range_error);
        return true;
      }
      *field = v;
    } else {  // CLI_ON_OFF
      *field = memcmp(value, "on", 2) == 0;
    }
    markDirty();
    strcpy(reply, "OK");
    return true;
  }

  return false; // not handled
}
