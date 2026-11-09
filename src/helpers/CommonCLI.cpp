#include <Arduino.h>
#include "CommonCLI.h"
#include "TxtDataHelpers.h"
#include "AdvertDataHelpers.h"
#include "TxtDataHelpers.h"
#include <RTClib.h>

#ifndef BRIDGE_MAX_BAUD
#define BRIDGE_MAX_BAUD 115200
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

static bool isValidName(const char *n) {
  while (*n) {
    if (*n == '[' || *n == ']' || *n == '\\' || *n == ':' || *n == ',' || *n == '?' || *n == '*') return false;
    n++;
  }
  return true;
}

// The binary loader has sanitised these values since before /prefs.json existed.
// Both loaders call it now, so a file that was edited by hand or written by
// another tool cannot leave the node on values its own CLI would refuse.
// Non-finite floats never reach this point: ConfigSerializer keeps the default
// in place for those, and constrain() would pass a NaN through unchanged.
void CommonCLI::sanitisePrefs() {
  // sanitise bad pref values
  _prefs->rx_delay_base = constrain(_prefs->rx_delay_base, 0, 20.0f);
  _prefs->tx_delay_factor = constrain(_prefs->tx_delay_factor, 0, 2.0f);
  _prefs->direct_tx_delay_factor = constrain(_prefs->direct_tx_delay_factor, 0, 2.0f);
  // 99 is what the CLI can produce, not the 9 of "set af": "set dutycycle 1"
  // stores 100/1 - 1. The old limit of 9 predates that command by a year
  // (01d84d5d 2025-03-05 against 0aa0ec1f 2026-03-07) and would throw away a
  // duty cycle below 10 percent at the next boot.
  _prefs->airtime_factor = constrain(_prefs->airtime_factor, 0, 99.0f);
  _prefs->freq = constrain(_prefs->freq, 150.0f, 2500.0f);
  _prefs->bw = constrain(_prefs->bw, 7.8f, 500.0f);
  _prefs->sf = constrain(_prefs->sf, 5, 12);
  _prefs->cr = constrain(_prefs->cr, 5, 8);
  _prefs->tx_power_dbm = constrain(_prefs->tx_power_dbm, -9, 30);
  _prefs->multi_acks = constrain(_prefs->multi_acks, 0, 1);
  _prefs->adc_multiplier = constrain(_prefs->adc_multiplier, 0.0f, 10.0f);
  _prefs->path_hash_mode = constrain(_prefs->path_hash_mode, 0, 2);   // NOTE: mode 3 reserved for future

  // sanitise bad bridge pref values
  _prefs->bridge_enabled = constrain(_prefs->bridge_enabled, 0, 1);
  _prefs->bridge_delay = constrain(_prefs->bridge_delay, 0, 10000);
  _prefs->bridge_pkt_src = constrain(_prefs->bridge_pkt_src, 0, 1);
  _prefs->bridge_baud = constrain(_prefs->bridge_baud, 9600, BRIDGE_MAX_BAUD);
  _prefs->bridge_channel = constrain(_prefs->bridge_channel, 0, 14);

  _prefs->powersaving_enabled = constrain(_prefs->powersaving_enabled, 0, 1);

  _prefs->gps_enabled = constrain(_prefs->gps_enabled, 0, 1);
  _prefs->advert_loc_policy = constrain(_prefs->advert_loc_policy, 0, 2);

  // sanitise settings
  _prefs->rx_boosted_gain = constrain(_prefs->rx_boosted_gain, 0, 1); // boolean
  _prefs->radio_fem_rxgain = constrain(_prefs->radio_fem_rxgain, 0, 1); // boolean
  _prefs->radio_fem_txgain = constrain(_prefs->radio_fem_txgain, 0, 1); // boolean
  _prefs->cad_enabled = constrain(_prefs->cad_enabled, 0, 1); // boolean
}

void CommonCLI::loadPrefs(FILESYSTEM* fs) {
  if (fs->exists("/prefs.json")) {
#if defined(RP2040_PLATFORM)
    File file = fs->open("/prefs.json", "r");
#else
    File file = fs->open("/prefs.json");
#endif
    if (file) {
      if (!_prefs->loadSerial(file)) {
        MESH_DEBUG_PRINTLN("loadPrefs: /prefs.json did not parse cleanly");
      }
      file.close();
      sanitisePrefs();
    }
  } else if (fs->exists("/com_prefs")) {
    loadPrefsInt(fs, "/com_prefs");
    if (savePrefs(fs)) {  // save to new Serial prefs
  //    fs->remove("/com_prefs");  // remove old
    }
  }
}

void CommonCLI::loadPrefsInt(FILESYSTEM* fs, const char* filename) {  // Legacy prefs loader
#if defined(RP2040_PLATFORM)
  File file = fs->open(filename, "r");
#else
  File file = fs->open(filename);
#endif
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs->airtime_factor, sizeof(_prefs->airtime_factor));    // 0
    file.read((uint8_t *)&_prefs->node_name, sizeof(_prefs->node_name));              // 4
    file.read(pad, 4);                                                                // 36
    file.read((uint8_t *)&_prefs->node_lat, sizeof(_prefs->node_lat));                // 40
    file.read((uint8_t *)&_prefs->node_lon, sizeof(_prefs->node_lon));                // 48
    file.read((uint8_t *)&_prefs->password[0], sizeof(_prefs->password));             // 56
    file.read((uint8_t *)&_prefs->freq, sizeof(_prefs->freq));                        // 72
    file.read((uint8_t *)&_prefs->tx_power_dbm, sizeof(_prefs->tx_power_dbm));        // 76
    file.read((uint8_t *)&_prefs->disable_fwd, sizeof(_prefs->disable_fwd));          // 77
    file.read((uint8_t *)&_prefs->advert_interval, sizeof(_prefs->advert_interval));  // 78
    file.read(pad, 1);                                                                // 79 : 1 byte unused (was rx_boosted_gain in v1.14.1, moved to end for upgrade compat)
    file.read((uint8_t *)&_prefs->rx_delay_base, sizeof(_prefs->rx_delay_base));      // 80
    file.read((uint8_t *)&_prefs->tx_delay_factor, sizeof(_prefs->tx_delay_factor));  // 84
    file.read((uint8_t *)&_prefs->guest_password[0], sizeof(_prefs->guest_password)); // 88
    file.read((uint8_t *)&_prefs->direct_tx_delay_factor, sizeof(_prefs->direct_tx_delay_factor)); // 104
    file.read(pad, 4); // 108 : 4 bytes unused
    file.read((uint8_t *)&_prefs->sf, sizeof(_prefs->sf));                                         // 112
    file.read((uint8_t *)&_prefs->cr, sizeof(_prefs->cr));                                         // 113
    file.read((uint8_t *)&_prefs->allow_read_only, sizeof(_prefs->allow_read_only));               // 114
    file.read((uint8_t *)&_prefs->multi_acks, sizeof(_prefs->multi_acks));                         // 115
    file.read((uint8_t *)&_prefs->bw, sizeof(_prefs->bw));                                         // 116
    file.read((uint8_t *)&_prefs->agc_reset_interval, sizeof(_prefs->agc_reset_interval));         // 120
    file.read((uint8_t *)&_prefs->path_hash_mode, sizeof(_prefs->path_hash_mode));                 // 121
    file.read((uint8_t *)&_prefs->loop_detect, sizeof(_prefs->loop_detect));                       // 122
    file.read(pad, 1);                                                                             // 123
    file.read((uint8_t *)&_prefs->flood_max, sizeof(_prefs->flood_max));                           // 124
    file.read((uint8_t *)&_prefs->flood_advert_interval, sizeof(_prefs->flood_advert_interval));   // 125
    file.read((uint8_t *)&_prefs->interference_threshold, sizeof(_prefs->interference_threshold)); // 126
    file.read((uint8_t *)&_prefs->bridge_enabled, sizeof(_prefs->bridge_enabled));                 // 127
    file.read((uint8_t *)&_prefs->bridge_delay, sizeof(_prefs->bridge_delay));                     // 128
    file.read((uint8_t *)&_prefs->bridge_pkt_src, sizeof(_prefs->bridge_pkt_src));                 // 130
    file.read((uint8_t *)&_prefs->bridge_baud, sizeof(_prefs->bridge_baud));                       // 131
    file.read((uint8_t *)&_prefs->bridge_channel, sizeof(_prefs->bridge_channel));                 // 135
    file.read((uint8_t *)&_prefs->bridge_secret, sizeof(_prefs->bridge_secret));                   // 136
    file.read((uint8_t *)&_prefs->powersaving_enabled, sizeof(_prefs->powersaving_enabled));       // 152
    file.read(pad, 3);                                                                             // 153
    file.read((uint8_t *)&_prefs->gps_enabled, sizeof(_prefs->gps_enabled));                       // 156
    file.read((uint8_t *)&_prefs->gps_interval, sizeof(_prefs->gps_interval));                     // 157
    file.read((uint8_t *)&_prefs->advert_loc_policy, sizeof (_prefs->advert_loc_policy));          // 161
    file.read((uint8_t *)&_prefs->discovery_mod_timestamp, sizeof(_prefs->discovery_mod_timestamp)); // 162
    file.read((uint8_t *)&_prefs->adc_multiplier, sizeof(_prefs->adc_multiplier));                 // 166
    file.read((uint8_t *)_prefs->owner_info, sizeof(_prefs->owner_info));                          // 170
    file.read((uint8_t *)&_prefs->rx_boosted_gain, sizeof(_prefs->rx_boosted_gain));               // 290
    file.read((uint8_t *)&_prefs->flood_max_unscoped, sizeof(_prefs->flood_max_unscoped));         // 291
    file.read((uint8_t *)&_prefs->flood_max_advert, sizeof(_prefs->flood_max_advert));             // 292
    file.read((uint8_t *)&_prefs->radio_fem_rxgain, sizeof(_prefs->radio_fem_rxgain));             // 293
    file.read((uint8_t *)&_prefs->cad_enabled, sizeof(_prefs->cad_enabled));                       // 294
    // next: 295


    file.close();
    sanitisePrefs();
  }
}

bool CommonCLI::savePrefs(FILESYSTEM* fs) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove("/prefs.json");
  File file = fs->open("/prefs.json", FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = fs->open("/prefs.json", "w");
#else
  File file = fs->open("/prefs.json", "w", true);
#endif
  if (file) {
    bool success = _prefs->saveSerial(file);
    file.close();
    return success;
  }
  return false;
}

#define MIN_LOCAL_ADVERT_INTERVAL   60

void CommonCLI::savePrefs() {
  if (_prefs->advert_interval * 2 < MIN_LOCAL_ADVERT_INTERVAL) {
    _prefs->advert_interval = 0;  // turn it off, now that device has been manually configured
  }
  _callbacks->savePrefs();
  _prefs->clearDirty();
}

uint8_t CommonCLI::buildAdvertData(uint8_t node_type, uint8_t* app_data) {
  if (_prefs->advert_loc_policy == ADVERT_LOC_NONE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name);
    return builder.encodeTo(app_data);
  } else if (_prefs->advert_loc_policy == ADVERT_LOC_SHARE) {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _sensors->node_lat, _sensors->node_lon);
    return builder.encodeTo(app_data);
  } else {
    AdvertDataBuilder builder(node_type, _prefs->node_name, _prefs->node_lat, _prefs->node_lon);
    return builder.encodeTo(app_data);
  }
}

// ---------------------------------------------------------------------------
// Command tables (step 13b)
//
// One row per command. A row says how the start of the command line is
// compared (rule), whether it only works on the serial console (flag), and
// which handler runs. The tables are const, so they live in flash.
//
// Rows are tried top to bottom and the first match wins, exactly like the
// `else if` chains they replace. Order is part of the behaviour: wherever one
// name is the start of another ("advert.zerohop" and "advert", "flood.max.advert"
// and "flood.max", "log start" and "log"), the longer name must stay above.
// A serial-only row that does not match because the sender is remote is
// skipped, and the search goes on with the next row.
// ---------------------------------------------------------------------------

namespace {

enum CliMatch : uint8_t {
  CLI_PREFIX = 0,   // the command line starts with the name
  CLI_WORD,         // ... and the name is followed by the end of the line or a space
  CLI_EXACT,        // ... and the name is followed by the end of the line
};

enum CliFlags : uint8_t {
  CLI_ANY = 0,
  CLI_SERIAL = 1,   // only when sender_timestamp == 0 (the serial console)
};

// What a handler gets. `command` is the text the table was searched with (the
// whole line for the top level table, the text after "set " or "get " for those
// two), `arg` is the text after the matched name.
struct CliCall {
  uint32_t ts;
  char* command;
  char* arg;
  char* reply;
};

typedef void (*CliFn)(CommonCLI& c, CliCall& k);

struct CliCmd {
  const char* name;
  uint8_t len;
  uint8_t match;
  uint8_t flags;
  CliFn fn;
};

// len comes from the literal, so the name and its length cannot drift apart
#define CLI_CMD(str, match, flags, fn) { str, (uint8_t)(sizeof(str) - 1), match, flags, fn }

template <size_t N>
const CliCmd* cliFind(const CliCmd (&table)[N], uint32_t ts, const char* text) {
  for (size_t i = 0; i < N; i++) {
    const CliCmd& r = table[i];
    if ((r.flags & CLI_SERIAL) && ts != 0) continue;
    if (memcmp(text, r.name, r.len) != 0) continue;
    if (r.match == CLI_WORD && !(text[r.len] == 0 || text[r.len] == ' ')) continue;
    if (r.match == CLI_EXACT && text[r.len] != 0) continue;
    return &r;
  }
  return nullptr;
}

template <size_t N>
bool cliRun(const CliCmd (&table)[N], CommonCLI& c, uint32_t ts, char* text, char* reply) {
  const CliCmd* r = cliFind(table, ts, text);
  if (r == nullptr) return false;
  CliCall k = { ts, text, text + r->len, reply };
  r->fn(c, k);
  return true;
}

}  // namespace

// The handlers are static members of this struct so that they can reach the
// private members of CommonCLI (it is a friend).
struct CommonCLIImpl {
  // ---- top level commands ------------------------------------------------

  static void poweroff(CommonCLI& c, CliCall& k) {
    c._board->powerOff();  // doesn't return
  }
  static void reboot(CommonCLI& c, CliCall& k) {
    c._board->reboot();  // doesn't return
  }
  static void clkreboot(CommonCLI& c, CliCall& k) {
    // Reset clock
    c.getRTCClock()->setCurrentTime(1715770351);  // 15 May 2024, 8:50pm
    c._board->reboot();  // doesn't return
  }
  static void advertZerohop(CommonCLI& c, CliCall& k) {
    // send zerohop advert
    c._callbacks->sendSelfAdvertisement(1500, false);  // longer delay, give CLI response time to be sent first
    strcpy(k.reply, "OK - zerohop advert sent");
  }
  static void advert(CommonCLI& c, CliCall& k) {
    // send flood advert
    c._callbacks->sendSelfAdvertisement(1500, true);  // longer delay, give CLI response time to be sent first
    strcpy(k.reply, "OK - Advert sent");
  }
  static void clockSync(CommonCLI& c, CliCall& k) {
    uint32_t curr = c.getRTCClock()->getCurrentTime();
    if (k.ts > curr) {
      c.getRTCClock()->setCurrentTime(k.ts + 1);
      uint32_t now = c.getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(k.reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else {
      strcpy(k.reply, "ERR: clock cannot go backwards");
    }
  }
  static void startOta(CommonCLI& c, CliCall& k) {
    if (!c._board->startOTAUpdate(c._prefs->node_name, k.reply)) {
      strcpy(k.reply, "Error");
    }
  }
  static void clock(CommonCLI& c, CliCall& k) {
    uint32_t now = c.getRTCClock()->getCurrentTime();
    DateTime dt = DateTime(now);
    sprintf(k.reply, "%02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
  }
  static void time(CommonCLI& c, CliCall& k) {  // set time (to epoch seconds)
    uint32_t secs = _atoi(k.arg);
    uint32_t curr = c.getRTCClock()->getCurrentTime();
    if (secs > curr) {
      c.getRTCClock()->setCurrentTime(secs);
      uint32_t now = c.getRTCClock()->getCurrentTime();
      DateTime dt = DateTime(now);
      sprintf(k.reply, "OK - clock set: %02d:%02d - %d/%d/%d UTC", dt.hour(), dt.minute(), dt.day(), dt.month(), dt.year());
    } else {
      strcpy(k.reply, "(ERR: clock cannot go backwards)");
    }
  }
  static void neighbors(CommonCLI& c, CliCall& k) {
    c._callbacks->formatNeighborsReply(k.reply);
  }
  static void neighborRemove(CommonCLI& c, CliCall& k) {
    const char* hex = k.arg;
    uint8_t pubkey[PUB_KEY_SIZE];
    int hex_len = min((int)strlen(hex), PUB_KEY_SIZE*2);
    int pubkey_len = hex_len / 2;
    if (mesh::Utils::fromHex(pubkey, pubkey_len, hex)) {
      c._callbacks->removeNeighbor(pubkey, pubkey_len);
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "ERR: bad pubkey");
    }
  }
  // true if the text, with its terminator, fits CommonCLI::tmp
  static bool fitsTmp(const CommonCLI& c, const char* text) {
    return strlen(text) < sizeof(c.tmp);
  }
  static void tempradio(CommonCLI& c, CliCall& k) {
    if (!fitsTmp(c, k.arg)) {
      strcpy(k.reply, "Error, invalid params");
      return;
    }
    strcpy(c.tmp, k.arg);
    const char *parts[5];
    int num = mesh::Utils::parseTextParts(c.tmp, parts, 5);
    float freq  = num > 0 ? strtof(parts[0], nullptr) : 0.0f;
    float bw    = num > 1 ? strtof(parts[1], nullptr) : 0.0f;
    uint8_t sf  = num > 2 ? atoi(parts[2]) : 0;
    uint8_t cr  = num > 3 ? atoi(parts[3]) : 0;
    int temp_timeout_mins  = num > 4 ? atoi(parts[4]) : 0;
    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f && temp_timeout_mins > 0) {
      c._callbacks->applyTempRadioParams(freq, bw, sf, cr, temp_timeout_mins);
      sprintf(k.reply, "OK - temp params for %d mins", temp_timeout_mins);
    } else {
      strcpy(k.reply, "Error, invalid params");
    }
  }
  static void password(CommonCLI& c, CliCall& k) {
    // change admin password
    StrHelper::strncpy(c._prefs->password, k.arg, sizeof(c._prefs->password));
    c.savePrefs();
    sprintf(k.reply, "password now: ");
    StrHelper::strncpy(&k.reply[14], c._prefs->password, 160-15);   // echo back just to let admin know for sure!!
  }
  static void clearStats(CommonCLI& c, CliCall& k) {
    c._callbacks->clearStats();
    strcpy(k.reply, "(OK - stats reset)");
  }
  static void get(CommonCLI& c, CliCall& k) {
    c.handleGetCmd(k.ts, k.command, k.reply);
  }
  static void set(CommonCLI& c, CliCall& k) {
    c.handleSetCmd(k.ts, k.command, k.reply);
  }
  static void erase(CommonCLI& c, CliCall& k) {
    bool s = c._callbacks->formatFileSystem();
    sprintf(k.reply, "File system erase: %s", s ? "OK" : "Err");
  }
  static void ver(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "%s (Build: %s)", c._callbacks->getFirmwareVer(), c._callbacks->getBuildDate());
  }
  static void board(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "%s", c._board->getManufacturerName());
  }
  static void power(CommonCLI& c, CliCall& k) {
    const char* usb_state = c._board->hasUsbPowerDetect()
                                ? (c._board->isUsbPowered() ? "yes" : "no")
                                : "n/a";
    const char* solar_state = c._board->hasSolarChargerDetect()
                                  ? (c._board->isSolarChargerActive() ? "yes" : "no")
                                  : "n/a";
    snprintf(k.reply, 160, "batt:%u mV usb:%s solar_chg:%s ext:%s charger:%s",
            (unsigned)c._board->getBattMilliVolts(),
            usb_state,
            solar_state,
            c._board->isExternalPowered() ? "yes" : "no",
            c._board->isChargerActive() ? "yes" : "no");
  }
  static void sensorGet(CommonCLI& c, CliCall& k) {
    const char* key = k.arg;
    const char* val = c._sensors->getSettingByKey(key);
    if (val != NULL) {
      sprintf(k.reply, "> %s", val);
    } else {
      strcpy(k.reply, "null");
    }
  }
  static void sensorSet(CommonCLI& c, CliCall& k) {
    if (!fitsTmp(c, k.arg)) {
      strcpy(k.reply, "can't find custom var");
      return;
    }
    strcpy(c.tmp, k.arg);
    const char *parts[2];
    int num = mesh::Utils::parseTextParts(c.tmp, parts, 2, ' ');
    const char *key = (num > 0) ? parts[0] : "";
    const char *value = (num > 1) ? parts[1] : "null";
    if (c._sensors->setSettingValue(key, value)) {
      strcpy(k.reply, "ok");
    } else {
      strcpy(k.reply, "can't find custom var");
    }
  }
  static void sensorList(CommonCLI& c, CliCall& k) {
    char* reply = k.reply;
    char* command = k.command;
    char* dp = reply;
    int start = 0;
    int end = c._sensors->getNumSettings();
    if (strlen(command) > 11) {
      start = _atoi(command+12);
    }
    if (start >= end) {
      strcpy(reply, "no custom var");
    } else {
      const int reply_max = 160;    // callers pass a 160-byte reply buffer
      const int next_reserve = 16;  // room for "... next:NNN"
      sprintf(dp, "%d vars\n", end);
      dp = strchr(dp, 0);
      int i;
      for (i = start; i < end; i++) {
        const char* name = c._sensors->getSettingName(i);
        const char* value = c._sensors->getSettingValue(i);
        if (name == NULL) name = "?";
        if (value == NULL) value = "";
        int room = reply_max - next_reserve - (int)(dp - reply);
        int need = snprintf(NULL, 0, "%s=%s\n", name, value);
        if (need >= room && i > start) break;   // continue on the next page
        snprintf(dp, room, "%s=%s\n", name, value);   // a single oversized entry is truncated
        dp = strchr(dp, 0);
        if (need >= room) { i++; break; }
      }
      if (i < end) {
        snprintf(dp, reply_max - (int)(dp - reply), "... next:%d", i);
      } else if (dp > reply && *(dp-1) == '\n') {
        *(dp-1) = 0; // remove last CR
      }
    }
  }
  static void region(CommonCLI& c, CliCall& k) {
    c.handleRegionCmd(k.command, k.reply);
  }
#if ENV_INCLUDE_GPS == 1
  static void gpsOn(CommonCLI& c, CliCall& k) {
    if (c._sensors->setSettingValue("gps", "1")) {
      c._prefs->gps_enabled = 1;
      c.savePrefs();
      strcpy(k.reply, "ok");
    } else {
      strcpy(k.reply, "gps toggle not found");
    }
  }
  static void gpsOff(CommonCLI& c, CliCall& k) {
    if (c._sensors->setSettingValue("gps", "0")) {
      c._prefs->gps_enabled = 0;
      c.savePrefs();
      strcpy(k.reply, "ok");
    } else {
      strcpy(k.reply, "gps toggle not found");
    }
  }
  static void gpsSync(CommonCLI& c, CliCall& k) {
    LocationProvider * l = c._sensors->getLocationProvider();
    if (l != NULL) {
      l->syncTime();
      strcpy(k.reply, "ok");
    } else {
      strcpy(k.reply, "gps provider not found");
    }
  }
  static void gpsDiag(CommonCLI& c, CliCall& k) {
    char* reply = k.reply;
    LocationProvider * l = c._sensors->getLocationProvider();
    if (l != NULL) {
      const char* setting = c._sensors->getSettingByKey("gps");
      bool requested = setting != NULL && strcmp(setting, "1") == 0;
      int prefix_len = snprintf(reply, 160, "req:%u ", requested ? 1U : 0U);
      if (prefix_len < 0 || prefix_len >= 160) {
        reply[159] = 0;
      } else {
        l->formatDiagnostics(reply + prefix_len, 160 - prefix_len);
      }
    } else {
      strcpy(reply, "gps provider not found");
    }
  }
  static void gpsSetloc(CommonCLI& c, CliCall& k) {
    c._prefs->node_lat = c._sensors->node_lat;
    c._prefs->node_lon = c._sensors->node_lon;
    c.savePrefs();
    strcpy(k.reply, "ok");
  }
  static void gpsAdvert(CommonCLI& c, CliCall& k) {
    char* reply = k.reply;
    char* command = k.command;
    if (strlen(command) == 10) {
      switch (c._prefs->advert_loc_policy) {
        case ADVERT_LOC_NONE:
          strcpy(reply, "> none");
          break;
        case ADVERT_LOC_PREFS:
          strcpy(reply, "> prefs");
          break;
        case ADVERT_LOC_SHARE:
          strcpy(reply, "> share");
          break;
        default:
          strcpy(reply, "error");
      }
    } else if (memcmp(command+11, "none", 4) == 0) {
      c._prefs->advert_loc_policy = ADVERT_LOC_NONE;
      c.savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command+11, "share", 5) == 0) {
      c._prefs->advert_loc_policy = ADVERT_LOC_SHARE;
      c.savePrefs();
      strcpy(reply, "ok");
    } else if (memcmp(command+11, "prefs", 5) == 0) {
      c._prefs->advert_loc_policy = ADVERT_LOC_PREFS;
      c.savePrefs();
      strcpy(reply, "ok");
    } else {
      strcpy(reply, "error");
    }
  }
  static void gps(CommonCLI& c, CliCall& k) {
    LocationProvider * l = c._sensors->getLocationProvider();
    if (l != NULL) {
      bool enabled = l->isEnabled(); // is EN pin on ?
      bool fix = l->isValid();       // has fix ?
      int sats = l->satellitesCount();
      const char* setting = c._sensors->getSettingByKey("gps");
      bool active = setting != NULL && strcmp(setting, "1") == 0;
      if (enabled) {
        sprintf(k.reply, "on, %s, %s, %d sats",
          active?"active":"deactivated",
          fix?"fix":"no fix",
          sats);
      } else {
        strcpy(k.reply, "off");
      }
    } else {
      strcpy(k.reply, "Can't find GPS");
    }
  }
#endif
  static void powersavingOn(CommonCLI& c, CliCall& k) {
#if defined(NRF52_PLATFORM)
    c._prefs->powersaving_enabled = 1;
    c.savePrefs();
    strcpy(k.reply, "on - Immediate effect");
#elif defined(ESP32) && !defined(WITH_BRIDGE)
    c._prefs->powersaving_enabled = 1;
    c.savePrefs();
    strcpy(k.reply, "on - After 2 minutes");
#elif defined(WITH_BRIDGE)
    strcpy(k.reply, "Bridge not supported");
#else
    strcpy(k.reply, "Board not supported");
#endif
  }
  static void powersavingOff(CommonCLI& c, CliCall& k) {
    c._prefs->powersaving_enabled = 0;
    c.savePrefs();
    strcpy(k.reply, "off");
  }
  static void powersaving(CommonCLI& c, CliCall& k) {
    if (c._prefs->powersaving_enabled) {
      strcpy(k.reply, "on");
    } else {
      strcpy(k.reply, "off");
    }
  }
  static void logStart(CommonCLI& c, CliCall& k) {
    c._callbacks->setLoggingOn(true);
    strcpy(k.reply, "   logging on");
  }
  static void logStop(CommonCLI& c, CliCall& k) {
    c._callbacks->setLoggingOn(false);
    strcpy(k.reply, "   logging off");
  }
  static void logErase(CommonCLI& c, CliCall& k) {
    c._callbacks->eraseLogFile();
    strcpy(k.reply, "   log erased");
  }
  static void logDump(CommonCLI& c, CliCall& k) {
    c._callbacks->dumpLogFile();
    strcpy(k.reply, "   EOF");
  }
  static void statsPackets(CommonCLI& c, CliCall& k) {
    c._callbacks->formatPacketStatsReply(k.reply);
  }
  static void statsRadio(CommonCLI& c, CliCall& k) {
    c._callbacks->formatRadioStatsReply(k.reply);
  }
  static void statsCore(CommonCLI& c, CliCall& k) {
    c._callbacks->formatStatsReply(k.reply);
  }

  // ---- set <key> <value> -------------------------------------------------
  // k.arg is the text after the key and its space.

  static void setAllowReadOnly(CommonCLI& c, CliCall& k) {
    c._prefs->allow_read_only = memcmp(k.arg, "on", 2) == 0;
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
  static void setFloodAdvertInterval(CommonCLI& c, CliCall& k) {
    int hours = _atoi(k.arg);
    if ((hours > 0 && hours < 3) || (hours > 168)) {
      strcpy(k.reply, "Error: interval range is 3-168 hours");
    } else {
      c._prefs->flood_advert_interval = (uint8_t)(hours);
      c._callbacks->updateFloodAdvertTimer();
      c.savePrefs();
      strcpy(k.reply, "OK");
    }
  }
  static void setAdvertInterval(CommonCLI& c, CliCall& k) {
    int mins = _atoi(k.arg);
    if ((mins > 0 && mins < MIN_LOCAL_ADVERT_INTERVAL) || (mins > 240)) {
      sprintf(k.reply, "Error: interval range is %d-240 minutes", MIN_LOCAL_ADVERT_INTERVAL);
    } else {
      c._prefs->advert_interval = (uint8_t)(mins / 2);
      c._callbacks->updateAdvertTimer();
      c.savePrefs();
      strcpy(k.reply, "OK");
    }
  }
  static void setGuestPassword(CommonCLI& c, CliCall& k) {
    StrHelper::strncpy(c._prefs->guest_password, k.arg, sizeof(c._prefs->guest_password));
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
  static void setPrvKey(CommonCLI& c, CliCall& k) {
    uint8_t prv_key[PRV_KEY_SIZE];
    bool success = mesh::Utils::fromHex(prv_key, PRV_KEY_SIZE, k.arg);
    // only allow rekey if key is valid
    if (success && mesh::LocalIdentity::validatePrivateKey(prv_key)) {
      mesh::LocalIdentity new_id;
      new_id.readFrom(prv_key, PRV_KEY_SIZE);
      c._callbacks->saveIdentity(new_id);
      strcpy(k.reply, "OK, reboot to apply! New pubkey: ");
      mesh::Utils::toHex(&k.reply[33], new_id.pub_key, PUB_KEY_SIZE);
    } else {
      strcpy(k.reply, "Error, bad key");
    }
  }
  static void setName(CommonCLI& c, CliCall& k) {
    if (isValidName(k.arg)) {
      StrHelper::strncpy(c._prefs->node_name, k.arg, sizeof(c._prefs->node_name));
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "Error, bad chars");
    }
  }
  static void setRepeat(CommonCLI& c, CliCall& k) {
    c._prefs->disable_fwd = memcmp(k.arg, "off", 3) == 0;
    c.savePrefs();
    strcpy(k.reply, c._prefs->disable_fwd ? "OK - repeat is now OFF" : "OK - repeat is now ON");
  }
  static void setLat(CommonCLI& c, CliCall& k) {
    c._prefs->node_lat = atof(k.arg);
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
  static void setLon(CommonCLI& c, CliCall& k) {
    c._prefs->node_lon = atof(k.arg);
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
  static void setFloodMaxUnscoped(CommonCLI& c, CliCall& k) {
    uint8_t m = atoi(k.arg);
    if (m <= 64) {
      c._prefs->flood_max_unscoped = m;
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "Error, max 64");
    }
  }
  static void setFloodMaxType(CommonCLI& c, CliCall& k) {
    uint8_t type, hops;
    int rc = mesh::parseFloodMaxTypeCmd(k.arg, &type, &hops);
    if (rc == FLOOD_MAX_CMD_BAD_TYPE) {
      strcpy(k.reply, "Error, unknown packet type");
    } else if (rc != FLOOD_MAX_CMD_OK) {
      strcpy(k.reply, "Error, hops 0..64 or off");
    } else if (!mesh::setFloodMaxForType(c._prefs->flood_max_types, sizeof(c._prefs->flood_max_types),
                                         type, hops)) {
      strcpy(k.reply, "Error, type list full");
    } else {
      c.savePrefs();
      strcpy(k.reply, "OK");
    }
  }
  static void setFloodMaxAdvert(CommonCLI& c, CliCall& k) {
    uint8_t m = atoi(k.arg);
    if (m <= 64) {
      c._prefs->flood_max_advert = m;
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "Error, max 64");
    }
  }
  static void setFloodMax(CommonCLI& c, CliCall& k) {
    uint8_t m = atoi(k.arg);
    if (m <= 64) {
      c._prefs->flood_max = m;
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "Error, max 64");
    }
  }
  static void setOwnerInfo(CommonCLI& c, CliCall& k) {
    const char* config = k.arg;
    char *dp = c._prefs->owner_info;
    while (*config && dp - c._prefs->owner_info < sizeof(c._prefs->owner_info)-1) {
      *dp++ = (*config == '|') ? '\n' : *config;    // translate '|' to newline chars
      config++;
    }
    *dp = 0;
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
  static void setLoopDetect(CommonCLI& c, CliCall& k) {
    const char* config = k.arg;
    uint8_t mode;
    if (memcmp(config, "off", 3) == 0) {
      mode = LOOP_DETECT_OFF;
    } else if (memcmp(config, "minimal", 7) == 0) {
      mode = LOOP_DETECT_MINIMAL;
    } else if (memcmp(config, "moderate", 8) == 0) {
      mode = LOOP_DETECT_MODERATE;
    } else if (memcmp(config, "strict", 6) == 0) {
      mode = LOOP_DETECT_STRICT;
    } else {
      mode = 0xFF;
      strcpy(k.reply, "Error, must be: off, minimal, moderate, or strict");
    }
    if (mode != 0xFF) {
      c._prefs->loop_detect = mode;
      c.savePrefs();
      strcpy(k.reply, "OK");
    }
  }
  static void setFreq(CommonCLI& c, CliCall& k) {
    c._prefs->freq = atof(k.arg);
    c.savePrefs();
    strcpy(k.reply, "OK - reboot to apply");
  }
#ifdef WITH_BRIDGE
  static void setBridgeEnabled(CommonCLI& c, CliCall& k) {
    c._prefs->bridge_enabled = memcmp(k.arg, "on", 2) == 0;
    c._callbacks->setBridgeState(c._prefs->bridge_enabled);
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
  static void setBridgeDelay(CommonCLI& c, CliCall& k) {
    int delay = _atoi(k.arg);
    if (delay >= 0 && delay <= 10000) {
      c._prefs->bridge_delay = (uint16_t)delay;
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "Error: delay must be between 0-10000 ms");
    }
  }
  static void setBridgeSource(CommonCLI& c, CliCall& k) {
    c._prefs->bridge_pkt_src = memcmp(k.arg, "rx", 2) == 0;
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
#endif
#ifdef WITH_RS232_BRIDGE
  static void setBridgeBaud(CommonCLI& c, CliCall& k) {
    uint32_t baud = atoi(k.arg);
    if (baud >= 9600 && baud <= BRIDGE_MAX_BAUD) {
      c._prefs->bridge_baud = (uint32_t)baud;
      c._callbacks->restartBridge();
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      sprintf(k.reply, "Error: baud rate must be between 9600-%d",BRIDGE_MAX_BAUD);
    }
  }
#endif
#ifdef WITH_ESPNOW_BRIDGE
  static void setBridgeChannel(CommonCLI& c, CliCall& k) {
    int ch = atoi(k.arg);
    if (ch > 0 && ch < 15) {
      c._prefs->bridge_channel = (uint8_t)ch;
      c._callbacks->restartBridge();
      c.savePrefs();
      strcpy(k.reply, "OK");
    } else {
      strcpy(k.reply, "Error: channel must be between 1-14");
    }
  }
  static void setBridgeSecret(CommonCLI& c, CliCall& k) {
    StrHelper::strncpy(c._prefs->bridge_secret, k.arg, sizeof(c._prefs->bridge_secret));
    c._callbacks->restartBridge();
    c.savePrefs();
    strcpy(k.reply, "OK");
  }
#endif
  static void setAdcMultiplier(CommonCLI& c, CliCall& k) {
    c._prefs->adc_multiplier = atof(k.arg);
    if (c._board->setAdcMultiplier(c._prefs->adc_multiplier)) {
      c.savePrefs();
      if (c._prefs->adc_multiplier == 0.0f) {
        strcpy(k.reply, "OK - using default board multiplier");
      } else {
        sprintf(k.reply, "OK - multiplier set to %.3f", c._prefs->adc_multiplier);
      }
    } else {
      c._prefs->adc_multiplier = 0.0f;
      strcpy(k.reply, "Error: unsupported");
    }
  }
#if defined(USE_LR2021)
  static void setExtraSf(CommonCLI& c, CliCall& k) {
    if (!fitsTmp(c, k.arg)) {
      sprintf(k.reply, "Invalid extra SF config");
      return;
    }
    strcpy(c.tmp, k.arg);
    const char *parts[4];
    uint8_t sideDetSFs[4];
    int num = mesh::Utils::parseTextParts(c.tmp, parts, 4);
    if (num > 3) {
      sprintf(k.reply, "Invalid extra SF config");
    } else {
      for (int i = 0; i < num; i++) {
        sideDetSFs[i] = atoi(parts[i]);
      }
      sideDetSFs[num] = 0;
      if (c._callbacks->configSideDetectors(sideDetSFs, num, c._prefs->bw)) {
        for (int i = 0; i <= num; i++) c._prefs->extra_sf[i] = sideDetSFs[i];
        c.savePrefs();
        sprintf(k.reply, "OK - extra SFs set");
      } else {
        sprintf(k.reply, "Invalid extra SF config");
      }
    }
  }
#endif

  // ---- get <key> ---------------------------------------------------------
  // The keys match by prefix and ignore what follows them.

  static void getAllowReadOnly(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->allow_read_only ? "on" : "off");
  }
  static void getFloodAdvertInterval(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", ((uint32_t) c._prefs->flood_advert_interval));
  }
  static void getAdvertInterval(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", ((uint32_t) c._prefs->advert_interval) * 2);
  }
  static void getGuestPassword(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->guest_password);
  }
  static void getPrvKey(CommonCLI& c, CliCall& k) {  // from serial command line only
    uint8_t prv_key[PRV_KEY_SIZE];
    int len = c._callbacks->getSelfId().writeTo(prv_key, PRV_KEY_SIZE);
    mesh::Utils::toHex(c.tmp, prv_key, len);
    sprintf(k.reply, "> %s", c.tmp);
  }
  static void getName(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->node_name);
  }
  static void getRepeat(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->disable_fwd ? "off" : "on");
  }
  static void getLat(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", StrHelper::ftoa(c._prefs->node_lat));
  }
  static void getLon(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", StrHelper::ftoa(c._prefs->node_lon));
  }
  static void getFloodMaxType(CommonCLI& c, CliCall& k) {
    mesh::formatFloodMaxTypes(c._prefs->flood_max_types, k.reply, 160);
  }
  static void getFloodMaxAdvert(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", (uint32_t)c._prefs->flood_max_advert);
  }
  static void getFloodMaxUnscoped(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", (uint32_t)c._prefs->flood_max_unscoped);
  }
  static void getFloodMax(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", (uint32_t)c._prefs->flood_max);
  }
  static void getOwnerInfo(CommonCLI& c, CliCall& k) {
    char* reply = k.reply;
    auto start = reply;
    *reply++ = '>';
    *reply++ = ' ';
    const char* sp = c._prefs->owner_info;
    while (*sp && reply - start < 159) {
      *reply++ = (*sp == '\n') ? '|' : *sp;    // translate newline back to orig '|'
      sp++;
    }
    *reply = 0;  // set null terminator
  }
  static void getLoopDetect(CommonCLI& c, CliCall& k) {
    if (c._prefs->loop_detect == LOOP_DETECT_OFF) {
      strcpy(k.reply, "> off");
    } else if (c._prefs->loop_detect == LOOP_DETECT_MINIMAL) {
      strcpy(k.reply, "> minimal");
    } else if (c._prefs->loop_detect == LOOP_DETECT_MODERATE) {
      strcpy(k.reply, "> moderate");
    } else {
      strcpy(k.reply, "> strict");
    }
  }
  static void getPublicKey(CommonCLI& c, CliCall& k) {
    strcpy(k.reply, "> ");
    mesh::Utils::toHex(&k.reply[2], c._callbacks->getSelfId().pub_key, PUB_KEY_SIZE);
  }
  static void getRole(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._callbacks->getRole());
  }
  static void getBridgeType(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s",
#ifdef WITH_RS232_BRIDGE
            "rs232"
#elif WITH_ESPNOW_BRIDGE
            "espnow"
#else
            "none"
#endif
    );
  }
#ifdef WITH_BRIDGE
  static void getBridgeEnabled(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->bridge_enabled ? "on" : "off");
  }
  static void getBridgeDelay(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", (uint32_t)c._prefs->bridge_delay);
  }
  static void getBridgeSource(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->bridge_pkt_src ? "logRx" : "logTx");
  }
#endif
#ifdef WITH_RS232_BRIDGE
  static void getBridgeBaud(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", (uint32_t)c._prefs->bridge_baud);
  }
#endif
#ifdef WITH_ESPNOW_BRIDGE
  static void getBridgeChannel(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %d", (uint32_t)c._prefs->bridge_channel);
  }
  static void getBridgeSecret(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> %s", c._prefs->bridge_secret);
  }
#endif
  static void getBootloaderVer(CommonCLI& c, CliCall& k) {
#ifdef NRF52_PLATFORM
    char ver[32];
    if (c._board->getBootloaderVersion(ver, sizeof(ver))) {
      sprintf(k.reply, "> %s", ver);
    } else {
      strcpy(k.reply, "> unknown");
    }
#else
    strcpy(k.reply, "Error: unsupported");
#endif
  }
  static void getAdcMultiplier(CommonCLI& c, CliCall& k) {
    float adc_mult = c._board->getAdcMultiplier();
    if (adc_mult == 0.0f) {
      strcpy(k.reply, "Error: unsupported");
    } else {
      sprintf(k.reply, "> %.3f", adc_mult);
    }
  }
  // Power management commands
  static void getPwrmgtSupport(CommonCLI& c, CliCall& k) {
#ifdef NRF52_POWER_MANAGEMENT
    strcpy(k.reply, "> supported");
#else
    strcpy(k.reply, "> unsupported");
#endif
  }
  static void getPwrmgtSource(CommonCLI& c, CliCall& k) {
#ifdef NRF52_POWER_MANAGEMENT
    strcpy(k.reply, c._board->isExternalPowered() ? "> external" : "> battery");
#else
    strcpy(k.reply, "ERROR: Power management not supported");
#endif
  }
  static void getPwrmgtBootreason(CommonCLI& c, CliCall& k) {
    sprintf(k.reply, "> Reset: %s; Shutdown: %s",
      c._board->getResetReasonString(c._board->getResetReason()),
      c._board->getShutdownReasonString(c._board->getShutdownReason()));
  }
  static void getPwrmgtBootmv(CommonCLI& c, CliCall& k) {
#ifdef NRF52_POWER_MANAGEMENT
    sprintf(k.reply, "> %u mV", c._board->getBootVoltage());
#else
    strcpy(k.reply, "ERROR: Power management not supported");
#endif
  }
  static void getExtraSf(CommonCLI& c, CliCall& k) {
    char* reply = k.reply;
    char* tmp = reply;
    for (int i = 0; i < 3 && c._prefs->extra_sf[i] != 0; i++) {
      tmp += sprintf(tmp, "%s%d", (i == 0) ? "" : ",", c._prefs->extra_sf[i]);
    }
    if (tmp == reply) {
      sprintf(reply, "No extra SF configured");
    }
  }

  // ---- region <sub> ------------------------------------------------------
  // parts[0] is "region", parts[1] the sub command, parts[2] and parts[3]
  // its arguments; n is the number of parts the line split into.

  static void regionLoad(CommonCLI& c, const char* const* parts, int n, char* reply) {
    c._callbacks->startRegionsLoad();
  }
  static void regionSave(CommonCLI& c, const char* const* parts, int n, char* reply) {
    c._prefs->discovery_mod_timestamp = c.getRTCClock()->getCurrentTime();   // this node is now 'modified' (for discovery info)
    c.savePrefs();
    bool success = c._callbacks->saveRegions();
    strcpy(reply, success ? "OK" : "Err - save failed");
  }
  static void regionAllowf(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto region = c._region_map->findByNamePrefix(parts[2]);
    if (region) {
      region->flags &= ~REGION_DENY_FLOOD;
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Err - unknown region");
    }
  }
  static void regionDenyf(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto region = c._region_map->findByNamePrefix(parts[2]);
    if (region) {
      region->flags |= REGION_DENY_FLOOD;
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Err - unknown region");
    }
  }
  static void regionGet(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto region = c._region_map->findByNamePrefix(parts[2]);
    if (region) {
      auto parent = c._region_map->findById(region->parent);
      if (parent && parent->id != 0) {
        sprintf(reply, " %s (%s) %s", region->name, parent->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
      } else {
        sprintf(reply, " %s %s", region->name, (region->flags & REGION_DENY_FLOOD) ? "" : "F");
      }
    } else {
      strcpy(reply, "Err - unknown region");
    }
  }
  static void regionHomeSet(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto home = c._region_map->findByNamePrefix(parts[2]);
    if (home) {
      c._region_map->setHomeRegion(home);
      sprintf(reply, " home is now %s", home->name);
    } else {
      strcpy(reply, "Err - unknown region");
    }
  }
  static void regionHomeGet(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto home = c._region_map->getHomeRegion();
    sprintf(reply, " home is %s", home ? home->name : "*");
  }
  static void regionDefaultSet(CommonCLI& c, const char* const* parts, int n, char* reply) {
    if (strcmp(parts[2], "<null>") == 0) {
      c._region_map->setDefaultRegion(NULL);
      c._callbacks->onDefaultRegionChanged(NULL);
      c._callbacks->saveRegions();  // persist in one atomic step
      sprintf(reply, " default scope is now <null>");
    } else {
      auto def = c._region_map->findByNamePrefix(parts[2]);
      if (def == NULL) {
        def = c._region_map->putRegion(parts[2], 0);  // auto-create the default region
      }
      if (def) {
        def->flags = 0;   // make sure allow flood enabled
        c._region_map->setDefaultRegion(def);
        c._callbacks->onDefaultRegionChanged(def);
        c._callbacks->saveRegions();  // persist in one atomic step
        sprintf(reply, " default scope is now %s", def->name);
      } else {
        strcpy(reply, "Err - region table full");
      }
    }
  }
  static void regionDefaultGet(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto def = c._region_map->getDefaultRegion();
    sprintf(reply, " default scope is %s", def ? def->name : "<null>");
  }
  static void regionPut(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto parent = n >= 4 ? c._region_map->findByNamePrefix(parts[3]) : &(c._region_map->getWildcard());
    if (parent == NULL) {
      strcpy(reply, "Err - unknown parent");
    } else {
      auto region = c._region_map->putRegion(parts[2], parent->id);
      if (region == NULL) {
        strcpy(reply, "Err - unable to put");
      } else {
        region->flags = 0;   // New default: enable flood
        strcpy(reply, "OK - (flood allowed)");
      }
    }
  }
  static void regionRemove(CommonCLI& c, const char* const* parts, int n, char* reply) {
    auto region = c._region_map->findByName(parts[2]);
    if (region) {
      if (c._region_map->removeRegion(*region)) {
        strcpy(reply, "OK");
      } else {
        strcpy(reply, "Err - not empty");
      }
    } else {
      strcpy(reply, "Err - not found");
    }
  }
  static void regionList(CommonCLI& c, const char* const* parts, int n, char* reply) {
    uint8_t mask = 0;
    bool invert = false;

    if (strcmp(parts[2], "allowed") == 0) {
      mask = REGION_DENY_FLOOD;
      invert = false;  // list regions that DON'T have DENY flag
    } else if (strcmp(parts[2], "denied") == 0) {
      mask = REGION_DENY_FLOOD;
      invert = true;   // list regions that DO have DENY flag
    } else {
      strcpy(reply, "Err - use 'allowed' or 'denied'");
      return;
    }

    int len = c._region_map->exportNamesTo(reply, 160, mask, invert);
    if (len == 0) {
      strcpy(reply, "-none-");
    }
  }
};

namespace {

// Top level commands. Order matters, see the note at the top of this section.
const CliCmd kTopCmds[] = {
  CLI_CMD("poweroff",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::poweroff),
  CLI_CMD("shutdown",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::poweroff),
  CLI_CMD("reboot",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::reboot),
  CLI_CMD("clkreboot",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::clkreboot),
  CLI_CMD("advert.zerohop", CLI_WORD,   CLI_ANY,    CommonCLIImpl::advertZerohop),
  CLI_CMD("advert",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::advert),
  CLI_CMD("clock sync",     CLI_PREFIX, CLI_ANY,    CommonCLIImpl::clockSync),
  CLI_CMD("start ota",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::startOta),
  CLI_CMD("clock",          CLI_PREFIX, CLI_ANY,    CommonCLIImpl::clock),
  CLI_CMD("time ",          CLI_PREFIX, CLI_ANY,    CommonCLIImpl::time),
  CLI_CMD("neighbors",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::neighbors),
  CLI_CMD("neighbor.remove ", CLI_PREFIX, CLI_ANY,  CommonCLIImpl::neighborRemove),
  CLI_CMD("tempradio ",     CLI_PREFIX, CLI_ANY,    CommonCLIImpl::tempradio),
  CLI_CMD("password ",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::password),
  CLI_CMD("clear stats",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::clearStats),
  CLI_CMD("get ",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::get),
  CLI_CMD("set ",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::set),
  CLI_CMD("erase",          CLI_EXACT,  CLI_SERIAL, CommonCLIImpl::erase),
  CLI_CMD("ver",            CLI_PREFIX, CLI_ANY,    CommonCLIImpl::ver),
  CLI_CMD("board",          CLI_PREFIX, CLI_ANY,    CommonCLIImpl::board),
  CLI_CMD("power",          CLI_EXACT,  CLI_ANY,    CommonCLIImpl::power),
  CLI_CMD("sensor get ",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::sensorGet),
  CLI_CMD("sensor set ",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::sensorSet),
  CLI_CMD("sensor list",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::sensorList),
  CLI_CMD("region",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::region),
#if ENV_INCLUDE_GPS == 1
  CLI_CMD("gps on",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::gpsOn),
  CLI_CMD("gps off",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::gpsOff),
  CLI_CMD("gps sync",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::gpsSync),
  CLI_CMD("gps diag",       CLI_EXACT,  CLI_ANY,    CommonCLIImpl::gpsDiag),
  CLI_CMD("gps setloc",     CLI_PREFIX, CLI_ANY,    CommonCLIImpl::gpsSetloc),
  CLI_CMD("gps advert",     CLI_PREFIX, CLI_ANY,    CommonCLIImpl::gpsAdvert),
  CLI_CMD("gps",            CLI_PREFIX, CLI_ANY,    CommonCLIImpl::gps),
#endif
  CLI_CMD("powersaving on", CLI_PREFIX, CLI_ANY,    CommonCLIImpl::powersavingOn),
  CLI_CMD("powersaving off", CLI_PREFIX, CLI_ANY,   CommonCLIImpl::powersavingOff),
  CLI_CMD("powersaving",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::powersaving),
  CLI_CMD("log start",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::logStart),
  CLI_CMD("log stop",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::logStop),
  CLI_CMD("log erase",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::logErase),
  CLI_CMD("log",            CLI_PREFIX, CLI_SERIAL, CommonCLIImpl::logDump),
  CLI_CMD("stats-packets",  CLI_WORD,   CLI_SERIAL, CommonCLIImpl::statsPackets),
  CLI_CMD("stats-radio",    CLI_WORD,   CLI_SERIAL, CommonCLIImpl::statsRadio),
  CLI_CMD("stats-core",     CLI_WORD,   CLI_SERIAL, CommonCLIImpl::statsCore),
};

// set <key> <value>: the names include the space that ends the key.
const CliCmd kSetCmds[] = {
  CLI_CMD("allow.read.only ",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setAllowReadOnly),
  CLI_CMD("flood.advert.interval ", CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setFloodAdvertInterval),
  CLI_CMD("advert.interval ",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setAdvertInterval),
  CLI_CMD("guest.password ",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setGuestPassword),
  CLI_CMD("prv.key ",               CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setPrvKey),
  CLI_CMD("name ",                  CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setName),
  CLI_CMD("repeat ",                CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setRepeat),
  CLI_CMD("lat ",                   CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setLat),
  CLI_CMD("lon ",                   CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setLon),
  CLI_CMD("flood.max.unscoped ",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setFloodMaxUnscoped),
  CLI_CMD("flood.max.type ",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setFloodMaxType),
  CLI_CMD("flood.max.advert ",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setFloodMaxAdvert),
  CLI_CMD("flood.max ",             CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setFloodMax),
  CLI_CMD("owner.info ",            CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setOwnerInfo),
  CLI_CMD("loop.detect ",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setLoopDetect),
  CLI_CMD("freq ",                  CLI_PREFIX, CLI_SERIAL, CommonCLIImpl::setFreq),
#ifdef WITH_BRIDGE
  CLI_CMD("bridge.enabled ",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setBridgeEnabled),
  CLI_CMD("bridge.delay ",          CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setBridgeDelay),
  CLI_CMD("bridge.source ",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setBridgeSource),
#endif
#ifdef WITH_RS232_BRIDGE
  CLI_CMD("bridge.baud ",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setBridgeBaud),
#endif
#ifdef WITH_ESPNOW_BRIDGE
  CLI_CMD("bridge.channel ",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setBridgeChannel),
  CLI_CMD("bridge.secret ",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setBridgeSecret),
#endif
  CLI_CMD("adc.multiplier ",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setAdcMultiplier),
#if defined(USE_LR2021)
  CLI_CMD("extra.sf ",              CLI_PREFIX, CLI_ANY,    CommonCLIImpl::setExtraSf),
#endif
};

// get <key>: the keys have no trailing space and ignore what follows them.
const CliCmd kGetCmds[] = {
  CLI_CMD("allow.read.only",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getAllowReadOnly),
  CLI_CMD("flood.advert.interval", CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getFloodAdvertInterval),
  CLI_CMD("advert.interval",       CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getAdvertInterval),
  CLI_CMD("guest.password",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getGuestPassword),
  CLI_CMD("prv.key",               CLI_PREFIX, CLI_SERIAL, CommonCLIImpl::getPrvKey),
  CLI_CMD("name",                  CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getName),
  CLI_CMD("repeat",                CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getRepeat),
  CLI_CMD("lat",                   CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getLat),
  CLI_CMD("lon",                   CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getLon),
  CLI_CMD("flood.max.type",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getFloodMaxType),
  CLI_CMD("flood.max.advert",      CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getFloodMaxAdvert),
  CLI_CMD("flood.max.unscoped",    CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getFloodMaxUnscoped),
  CLI_CMD("flood.max",             CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getFloodMax),
  CLI_CMD("owner.info",            CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getOwnerInfo),
  CLI_CMD("loop.detect",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getLoopDetect),
  CLI_CMD("public.key",            CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getPublicKey),
  CLI_CMD("role",                  CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getRole),
  CLI_CMD("bridge.type",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeType),
#ifdef WITH_BRIDGE
  CLI_CMD("bridge.enabled",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeEnabled),
  CLI_CMD("bridge.delay",          CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeDelay),
  CLI_CMD("bridge.source",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeSource),
#endif
#ifdef WITH_RS232_BRIDGE
  CLI_CMD("bridge.baud",           CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeBaud),
#endif
#ifdef WITH_ESPNOW_BRIDGE
  CLI_CMD("bridge.channel",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeChannel),
  CLI_CMD("bridge.secret",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBridgeSecret),
#endif
  CLI_CMD("bootloader.ver",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getBootloaderVer),
  CLI_CMD("adc.multiplier",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getAdcMultiplier),
  CLI_CMD("pwrmgt.support",        CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getPwrmgtSupport),
  CLI_CMD("pwrmgt.source",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getPwrmgtSource),
  CLI_CMD("pwrmgt.bootreason",     CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getPwrmgtBootreason),
  CLI_CMD("pwrmgt.bootmv",         CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getPwrmgtBootmv),
  CLI_CMD("extra.sf",              CLI_PREFIX, CLI_ANY,    CommonCLIImpl::getExtraSf),
};

// region <sub>: matched with strcmp on the second word, and by the number of
// words on the line (n, counting "region" itself). A row applies when
// min_n <= n <= max_n. `region` alone (n == 1) lists the tree and is handled
// before the table.
typedef void (*RegionFn)(CommonCLI& c, const char* const* parts, int n, char* reply);

struct RegionCmd {
  const char* name;
  uint8_t min_n;
  uint8_t max_n;
  RegionFn fn;
};

const uint8_t REGION_N_ANY = 255;

const RegionCmd kRegionCmds[] = {
  { "load",    2, REGION_N_ANY, CommonCLIImpl::regionLoad },
  { "save",    2, REGION_N_ANY, CommonCLIImpl::regionSave },
  { "allowf",  3, REGION_N_ANY, CommonCLIImpl::regionAllowf },
  { "denyf",   3, REGION_N_ANY, CommonCLIImpl::regionDenyf },
  { "get",     3, REGION_N_ANY, CommonCLIImpl::regionGet },
  { "home",    3, REGION_N_ANY, CommonCLIImpl::regionHomeSet },
  { "home",    2, 2,            CommonCLIImpl::regionHomeGet },
  { "default", 3, REGION_N_ANY, CommonCLIImpl::regionDefaultSet },
  { "default", 2, 2,            CommonCLIImpl::regionDefaultGet },
  { "put",     3, REGION_N_ANY, CommonCLIImpl::regionPut },
  { "remove",  3, REGION_N_ANY, CommonCLIImpl::regionRemove },
  { "list",    3, REGION_N_ANY, CommonCLIImpl::regionList },
};

}  // namespace

void CommonCLI::handleCommand(uint32_t sender_timestamp, char* command, char* reply) {
    if (_prefs->getRadioPrefs()->handleCommand(command, sender_timestamp, reply)) {   // is a radio CLI command?
      if (_prefs->getRadioPrefs()->isDirty()) { savePrefs(); }
      return;
    }
    // hook for variant-specific CLI processing
    if (_board->handleCommand(command, sender_timestamp, reply)) {
      if (_prefs->isDirty()) { savePrefs(); }
      return;
    }

    if (!cliRun(kTopCmds, *this, sender_timestamp, command, reply)) {
      strcpy(reply, "Unknown command");
    }
}

void CommonCLI::handleSetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  char* config = &command[4];

  if (!cliRun(kSetCmds, *this, sender_timestamp, config, reply)) {
    strcpy(reply, "unknown config: ");
    StrHelper::strncpy(&reply[16], config, 160-17);
  }
}

void CommonCLI::handleGetCmd(uint32_t sender_timestamp, char* command, char* reply) {
  char* config = &command[4];

  if (!cliRun(kGetCmds, *this, sender_timestamp, config, reply)) {
    snprintf(reply, 160, "??: %s", config);   // callers pass a 160-byte reply buffer
  }
}

static char* skipSpaces(char* s) {
  while (*s == ' ') s++;
  return s;
}

static void rtrimSpaces(char* s) {
  char* e = s + strlen(s);
  while (e > s && e[-1] == ' ') *--e = '\0';
}

static char* takeToken(char** cursor) {
  char* p = skipSpaces(*cursor);
  if (*p == '\0') { *cursor = p; return nullptr; }
  char* tok = p;
  while (*p && *p != ' ') p++;
  if (*p) *p++ = '\0';
  *cursor = p;
  return tok;
}

static char* splitNameJump(char* tok) {
  for (char* q = tok; *q; q++) {
    if (*q == '|' || *q == ',') {
      *q = '\0';
      char* jump = skipSpaces(q + 1);
      rtrimSpaces(jump);
      return jump;
    }
  }
  return nullptr;
}

static bool processRegionDefSegment(RegionMap* map, char* tok, RegionEntry** cursor, char* reply) {
  char* jump = splitNameJump(tok);
  char* name = skipSpaces(tok);
  if (*name == '\0') { snprintf(reply, 160, "Err - empty name"); return false; }
  if (jump && *jump == '\0') { snprintf(reply, 160, "Err - empty jump"); return false; }

  RegionEntry* r = map->putRegion(name, (*cursor)->id);
  if (r == NULL) { snprintf(reply, 160, "Err - put failed: %s", name); return false; }
  r->flags = 0;

  if (jump) {
    RegionEntry* j = map->findByNamePrefix(jump);
    if (j == NULL) { snprintf(reply, 160, "Err - unknown jump: %s", jump); return false; }
    *cursor = j;
  } else {
    *cursor = r;
  }
  return true;
}

void CommonCLI::handleRegionCmd(char* command, char* reply) {
  reply[0] = 0;

  // `region def`: must run before parseTextParts mutates the buffer
  char* cmd = skipSpaces(command);
  if (strncmp(cmd, "region def", 10) == 0 && (cmd[10] == ' ' || cmd[10] == '\0')) {
    char* payload = skipSpaces(cmd + 10);
    rtrimSpaces(payload);
    if (*payload == '\0') { snprintf(reply, 160, "Err - empty def"); return; }

    RegionEntry* cursor = &_region_map->getWildcard();
    for (char* tok; (tok = takeToken(&payload)) != nullptr; ) {
      if (!processRegionDefSegment(_region_map, tok, &cursor, reply)) return;
    }
    _region_map->exportTo(reply, 160);
    return;
  }

  const char* parts[4];
  int n = mesh::Utils::parseTextParts(command, parts, 4, ' ');
  if (n == 1) {
    _region_map->exportTo(reply, 160);
    return;
  }
  for (size_t i = 0; i < sizeof(kRegionCmds) / sizeof(kRegionCmds[0]); i++) {
    const RegionCmd& r = kRegionCmds[i];
    if (n >= r.min_n && n <= r.max_n && strcmp(parts[1], r.name) == 0) {
      r.fn(*this, parts, n, reply);
      return;
    }
  }
  strcpy(reply, "Err - ??");
}
