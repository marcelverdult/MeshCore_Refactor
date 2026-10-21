#include "MyMesh.h"
#include <helpers/FrameReader.h>

#include <Arduino.h> // needed for PlatformIO
#ifdef ENABLE_WIFI_INTERFACE
#include <WiFi.h>
#endif
#include <Mesh.h>

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2
#define CMD_SEND_CHANNEL_TXT_MSG      3
#define CMD_GET_CONTACTS              4 // with optional 'since' (for efficient sync)
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_ADD_UPDATE_CONTACT        9
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_RESET_PATH                13
#define CMD_SET_ADVERT_LATLON         14
#define CMD_REMOVE_CONTACT            15
#define CMD_SHARE_CONTACT             16
#define CMD_EXPORT_CONTACT            17
#define CMD_IMPORT_CONTACT            18
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20   // was CMD_GET_BATTERY_VOLTAGE
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_SEND_RAW_DATA             25
#define CMD_SEND_LOGIN                26
#define CMD_SEND_STATUS_REQ           27
#define CMD_HAS_CONNECTION            28
#define CMD_LOGOUT                    29 // 'Disconnect'
#define CMD_GET_CONTACT_BY_KEY        30
#define CMD_GET_CHANNEL               31
#define CMD_SET_CHANNEL               32
#define CMD_SIGN_START                33
#define CMD_SIGN_DATA                 34
#define CMD_SIGN_FINISH               35
#define CMD_SEND_TRACE_PATH           36
#define CMD_SET_DEVICE_PIN            37
#define CMD_SET_OTHER_PARAMS          38
#define CMD_SEND_TELEMETRY_REQ        39  // can deprecate this
#define CMD_GET_CUSTOM_VARS           40
#define CMD_SET_CUSTOM_VAR            41
#define CMD_GET_ADVERT_PATH           42
#define CMD_GET_TUNING_PARAMS         43
// NOTE: CMD range 44..49 parked, potentially for WiFi operations
#define CMD_SEND_BINARY_REQ           50
#define CMD_FACTORY_RESET             51
#define CMD_SEND_PATH_DISCOVERY_REQ   52
#define CMD_SET_FLOOD_SCOPE_KEY       54   // v8+
#define CMD_SEND_CONTROL_DATA         55   // v8+
#define CMD_GET_STATS                 56   // v8+, second byte is stats type
#define CMD_SEND_ANON_REQ             57
#define CMD_SET_AUTOADD_CONFIG        58
#define CMD_GET_AUTOADD_CONFIG        59
#define CMD_GET_ALLOWED_REPEAT_FREQ   60
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SEND_CHANNEL_DATA         62
#define CMD_SET_DEFAULT_FLOOD_SCOPE   63
#define CMD_GET_DEFAULT_FLOOD_SCOPE   64
#define CMD_SEND_RAW_PACKET           65
#define CMD_RUN_CLI_COMMAND           66  // v14+

// Stats sub-types for CMD_GET_STATS
#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS             2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2  // first reply to CMD_GET_CONTACTS
#define RESP_CODE_CONTACT             3  // multiple of these (after CMD_GET_CONTACTS)
#define RESP_CODE_END_OF_CONTACTS     4  // last reply to CMD_GET_CONTACTS
#define RESP_CODE_SELF_INFO           5  // reply to CMD_APP_START
#define RESP_CODE_SENT                6  // reply to CMD_SEND_TXT_MSG
#define RESP_CODE_CONTACT_MSG_RECV    7  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CHANNEL_MSG_RECV    8  // a reply to CMD_SYNC_NEXT_MESSAGE (ver < 3)
#define RESP_CODE_CURR_TIME           9  // a reply to CMD_GET_DEVICE_TIME
#define RESP_CODE_NO_MORE_MESSAGES    10 // a reply to CMD_SYNC_NEXT_MESSAGE
#define RESP_CODE_EXPORT_CONTACT      11
#define RESP_CODE_BATT_AND_STORAGE    12 // a reply to a CMD_GET_BATT_AND_STORAGE
#define RESP_CODE_DEVICE_INFO         13 // a reply to CMD_DEVICE_QUERY
#define RESP_CODE_PRIVATE_KEY         14 // a reply to CMD_EXPORT_PRIVATE_KEY
#define RESP_CODE_DISABLED            15
#define RESP_CODE_CONTACT_MSG_RECV_V3 16 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_MSG_RECV_V3 17 // a reply to CMD_SYNC_NEXT_MESSAGE (ver >= 3)
#define RESP_CODE_CHANNEL_INFO        18 // a reply to CMD_GET_CHANNEL
#define RESP_CODE_SIGN_START          19
#define RESP_CODE_SIGNATURE           20
#define RESP_CODE_CUSTOM_VARS         21
#define RESP_CODE_ADVERT_PATH         22
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24   // v8+, second byte is stats type
#define RESP_CODE_AUTOADD_CONFIG      25
#define RESP_ALLOWED_REPEAT_FREQ      26
#define RESP_CODE_CHANNEL_DATA_RECV   27
#define RESP_CODE_DEFAULT_FLOOD_SCOPE 28
#define RESP_CODE_CLI_REPLY           29  // v14+, a reply to CMD_RUN_CLI_COMMAND

#define MAX_CHANNEL_DATA_LENGTH       (MAX_FRAME_SIZE - 9)

#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250
#define LAZY_CONTACTS_WRITE_DELAY       5000

#define PUBLIC_GROUP_PSK                "izOH6cXN6mrJ5e26oRXNcg=="

// these are _pushed_ to client app at any time
#define PUSH_CODE_ADVERT                0x80
#define PUSH_CODE_PATH_UPDATED          0x81
#define PUSH_CODE_SEND_CONFIRMED        0x82
#define PUSH_CODE_MSG_WAITING           0x83
#define PUSH_CODE_RAW_DATA              0x84
#define PUSH_CODE_LOGIN_SUCCESS         0x85
#define PUSH_CODE_LOGIN_FAIL            0x86
#define PUSH_CODE_STATUS_RESPONSE       0x87
#define PUSH_CODE_LOG_RX_DATA           0x88
#define PUSH_CODE_TRACE_DATA            0x89
#define PUSH_CODE_NEW_ADVERT            0x8A
#define PUSH_CODE_TELEMETRY_RESPONSE    0x8B
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define PUSH_CODE_PATH_DISCOVERY_RESPONSE 0x8D
#define PUSH_CODE_CONTROL_DATA          0x8E   // v8+
#define PUSH_CODE_CONTACT_DELETED       0x8F // used to notify client app of deleted contact when overwriting oldest
#define PUSH_CODE_CONTACTS_FULL         0x90 // used to notify client app that contacts storage is full

#define ERR_CODE_UNSUPPORTED_CMD        1
#define ERR_CODE_NOT_FOUND              2
#define ERR_CODE_TABLE_FULL             3
#define ERR_CODE_BAD_STATE              4
#define ERR_CODE_FILE_IO_ERROR          5
#define ERR_CODE_ILLEGAL_ARG            6

#define MAX_SIGN_DATA_LEN               (8 * 1024) // 8K

// Auto-add config bitmask
// Bit 0: If set, overwrite oldest non-favourite contact when contacts file is full
// Bits 1-4: these indicate which contact types to auto-add when manual_contact_mode = 0x01
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

void MyMesh::writeOKFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_OK;
  _serial->writeFrame(buf, 1);
}
void MyMesh::writeErrFrame(uint8_t err_code) {
  uint8_t buf[2];
  buf[0] = RESP_CODE_ERR;
  buf[1] = err_code;
  _serial->writeFrame(buf, 2);
}

void MyMesh::writeDisabledFrame() {
  uint8_t buf[1];
  buf[0] = RESP_CODE_DISABLED;
  _serial->writeFrame(buf, 1);
}

void MyMesh::writeContactRespFrame(uint8_t code, const ContactInfo &contact) {
  int i = 0;
  out_frame[i++] = code;
  memcpy(&out_frame[i], contact.id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  out_frame[i++] = contact.type;
  out_frame[i++] = contact.flags;
  out_frame[i++] = contact.out_path_len;
  memcpy(&out_frame[i], contact.out_path, MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  StrHelper::strzcpy((char *)&out_frame[i], contact.name, 32);
  i += 32;
  memcpy(&out_frame[i], &contact.last_advert_timestamp, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lat, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.gps_lon, 4);
  i += 4;
  memcpy(&out_frame[i], &contact.lastmod, 4);
  i += 4;
  _serial->writeFrame(out_frame, i);
}

void MyMesh::updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len) {
  int i = 0;
  uint8_t code = frame[i++]; // eg. CMD_ADD_UPDATE_CONTACT
  memcpy(contact.id.pub_key, &frame[i], PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;
  contact.type = frame[i++];
  contact.flags = frame[i++];
  contact.out_path_len = frame[i++];
  memcpy(contact.out_path, &frame[i], MAX_PATH_SIZE);
  i += MAX_PATH_SIZE;
  memcpy(contact.name, &frame[i], 32);
  i += 32;
  memcpy(&contact.last_advert_timestamp, &frame[i], 4);
  i += 4;
  if (len >= i + 8) { // optional fields
    memcpy(&contact.gps_lat, &frame[i], 4);
    i += 4;
    memcpy(&contact.gps_lon, &frame[i], 4);
    i += 4;
    if (len >= i + 4) {
      memcpy(&last_mod, &frame[i], 4);
    }
  }
}

bool MyMesh::Frame::isChannelMsg() const {
  return buf[0] == RESP_CODE_CHANNEL_MSG_RECV || buf[0] == RESP_CODE_CHANNEL_MSG_RECV_V3 ||
         buf[0] == RESP_CODE_CHANNEL_DATA_RECV;
}

void MyMesh::addToOfflineQueue(const uint8_t frame[], int len) {
  if (offline_queue_len >= OFFLINE_QUEUE_SIZE) {
    MESH_DEBUG_PRINTLN("WARN: offline_queue is full!");
    int pos = 0;
    while (pos < offline_queue_len) {
      if (offline_queue[pos].isChannelMsg()) {
        for (int i = pos; i < offline_queue_len - 1; i++) { // delete oldest channel msg from queue
          offline_queue[i] = offline_queue[i + 1];
        }
        MESH_DEBUG_PRINTLN("INFO: removed oldest channel message from queue.");
        offline_queue[offline_queue_len - 1].len = len;
        memcpy(offline_queue[offline_queue_len - 1].buf, frame, len);
        return;
      }
      pos++;
    }
    MESH_DEBUG_PRINTLN("INFO: no channel messages to remove from queue.");
  } else {
    offline_queue[offline_queue_len].len = len;
    memcpy(offline_queue[offline_queue_len].buf, frame, len);
    offline_queue_len++;
  }
}

int MyMesh::getFromOfflineQueue(uint8_t frame[]) {
  if (offline_queue_len > 0) {         // check offline queue
    size_t len = offline_queue[0].len; // take from top of queue
    memcpy(frame, offline_queue[0].buf, len);

    offline_queue_len--;
    for (int i = 0; i < offline_queue_len; i++) { // delete top item from queue
      offline_queue[i] = offline_queue[i + 1];
    }
    return len;
  }
  return 0; // queue is empty
}


void MyMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
  if (_serial->isConnected() && len + 3 <= MAX_FRAME_SIZE) {
    int i = 0;
    out_frame[i++] = PUSH_CODE_LOG_RX_DATA;
    out_frame[i++] = (int8_t)(snr * 4);
    out_frame[i++] = (int8_t)(rssi);
    memcpy(&out_frame[i], raw, len);
    i += len;

    _serial->writeFrame(out_frame, i);
  }
}

bool MyMesh::isAutoAddEnabled() const {
  return (_prefs.manual_add_contacts & 1) == 0;
}

bool MyMesh::shouldAutoAddContactType(uint8_t contact_type) const {
  if ((_prefs.manual_add_contacts & 1) == 0) {
    return true;
  }

  uint8_t type_bit = 0;
  switch (contact_type) {
    case ADV_TYPE_CHAT:
      type_bit = AUTO_ADD_CHAT;
      break;
    case ADV_TYPE_REPEATER:
      type_bit = AUTO_ADD_REPEATER;
      break;
    case ADV_TYPE_ROOM:
      type_bit = AUTO_ADD_ROOM_SERVER;
      break;
    case ADV_TYPE_SENSOR:
      type_bit = AUTO_ADD_SENSOR;
      break;
    default:
      return false;  // Unknown type, don't auto-add
  }

  return (_prefs.autoadd_config & type_bit) != 0;
}

bool MyMesh::shouldOverwriteWhenFull() const {
  return (_prefs.autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) != 0;
}

uint8_t MyMesh::getAutoAddMaxHops() const {
  return _prefs.autoadd_max_hops;
}

void MyMesh::onContactOverwrite(const uint8_t* pub_key) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE); // delete from storage
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACT_DELETED;
    memcpy(&out_frame[1], pub_key, PUB_KEY_SIZE);
    _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
  }
}

void MyMesh::onContactsFull() {
  if (_serial->isConnected()) {
    out_frame[0] = PUSH_CODE_CONTACTS_FULL;
    _serial->writeFrame(out_frame, 1);
  }
}

void MyMesh::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (_serial->isConnected()) {
    if (is_new) {
      writeContactRespFrame(PUSH_CODE_NEW_ADVERT, contact);
    } else {
      out_frame[0] = PUSH_CODE_ADVERT;
      memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
      _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE);
    }
  }

  // add inbound-path to mem cache
  if (path && mesh::Packet::isValidPathLen(path_len)) {  // check path is valid
    AdvertPath* p = advert_paths;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {   // check if already in table, otherwise evict oldest
      if (memcmp(advert_paths[i].pubkey_prefix, contact.id.pub_key, sizeof(AdvertPath::pubkey_prefix)) == 0) {
        p = &advert_paths[i];   // found
        break;
      }
      if (advert_paths[i].recv_timestamp < oldest) {
        oldest = advert_paths[i].recv_timestamp;
        p = &advert_paths[i];
      }
    }

    memcpy(p->pubkey_prefix, contact.id.pub_key, sizeof(p->pubkey_prefix));
    strcpy(p->name, contact.name);
    p->recv_timestamp = getRTCClock()->getCurrentTime();
    p->path_len = mesh::Packet::copyPath(p->path, path, path_len);
  }

  if (_listener) _listener->onDiscoveredContact(contact, is_new, path_len, path);

  if (!is_new) dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY); // only schedule lazy write for contacts that are in contacts[]
}

static int sort_by_recent(const void *a, const void *b) {
  return ((AdvertPath *) b)->recv_timestamp - ((AdvertPath *) a)->recv_timestamp;
}

int MyMesh::getRecentlyHeard(AdvertPath dest[], int max_num) {
  if (max_num > ADVERT_PATH_TABLE_SIZE) max_num = ADVERT_PATH_TABLE_SIZE;
  qsort(advert_paths, ADVERT_PATH_TABLE_SIZE, sizeof(advert_paths[0]), sort_by_recent);

  for (int i = 0; i < max_num; i++) {
    dest[i] = advert_paths[i];
  }
  return max_num;
}

void MyMesh::onContactPathUpdated(const ContactInfo &contact) {
  out_frame[0] = PUSH_CODE_PATH_UPDATED;
  memcpy(&out_frame[1], contact.id.pub_key, PUB_KEY_SIZE);
  _serial->writeFrame(out_frame, 1 + PUB_KEY_SIZE); // NOTE: app may not be connected

  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
}

ContactInfo*  MyMesh::processAck(const uint8_t *data) {
  if (_listener) {
    uint32_t ack_crc;
    memcpy(&ack_crc, data, 4);
    _listener->onACKRecv(ack_crc);
  }
  // see if matches any in a table
  for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++) {
    if (memcmp(data, &expected_ack_table[i].ack, 4) == 0) { // got an ACK from recipient
      out_frame[0] = PUSH_CODE_SEND_CONFIRMED;
      memcpy(&out_frame[1], data, 4);
      uint32_t trip_time = _ms->getMillis() - expected_ack_table[i].msg_sent;
      memcpy(&out_frame[5], &trip_time, 4);
      _serial->writeFrame(out_frame, 9);

      // NOTE: the same ACK can be received multiple times!
      expected_ack_table[i].ack = 0; // clear expected hash, now that we have received ACK
      return expected_ack_table[i].contact;
    }
  }
  return checkConnectionsAck(data);
}

void MyMesh::queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt,
                          uint32_t sender_timestamp, const uint8_t *extra, int extra_len, const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CONTACT_MSG_RECV;
  }
  memcpy(&out_frame[i], from.id.pub_key, 6);
  i += 6; // just 6-byte prefix
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = txt_type;
  memcpy(&out_frame[i], &sender_timestamp, 4);
  i += 4;
  if (extra_len > 0) {
    memcpy(&out_frame[i], extra, extra_len);
    i += extra_len;
  }
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }

  if (_listener) {
    _listener->onMessageRecv(pkt, from, txt_type, sender_timestamp, text);
    _listener->onQueueSizeChanged(offline_queue_len);
  }
}

bool MyMesh::filterRecvFloodPacket(mesh::Packet* packet) {
  // REVISIT: try to determine which Region (from transport_codes[1]) that Sender is indicating for replies/responses
  //    if unknown, fallback to finding Region from transport_codes[0], the 'scope' used by Sender
  return false;
}

bool MyMesh::allowPacketForward(const mesh::Packet* packet) {
  return _prefs.isRepeatEn();
}

void MyMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis) {
  if (scope.isNull()) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
  } else {
    uint16_t codes[2];
    codes[0] = scope.calcTransportCode(pkt);
    codes[1] = 0;  // REVISIT: set to 'home' Region, for sender/return region?
    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
  }
}

void MyMesh::sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: dynamic send_scope, depending on recipient and current 'home' Region
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}
void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);
  }
}

void MyMesh::onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_PLAIN, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp, const char *text) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  queueMessage(from, TXT_TYPE_CLI_DATA, pkt, sender_timestamp, NULL, 0, text);
}

void MyMesh::onCLICommandRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                               const char *text, char* reply) {
  markConnectionActive(from); // in case this is from a server, and we have a connection
  if (from.isRemoteCLIAllowed()) {
    if (!handleCommand(text, sender_timestamp, reply)) {
      strcat(reply, "Unknown command");   // reply may have cmd prefix from 'text'
    }
  } else {
    queueMessage(from, TXT_TYPE_CLI_COMMAND, pkt, sender_timestamp, NULL, 0, text);
  }
}

void MyMesh::onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                                 const uint8_t *sender_prefix, const char *text) {
  markConnectionActive(from);
  // from.sync_since change needs to be persisted
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  queueMessage(from, TXT_TYPE_SIGNED_PLAIN, pkt, sender_timestamp, sender_prefix, 4, text);
}

void MyMesh::onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                                  const char *text) {
  int i = 0;
  if (app_target_ver >= 3) {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV_V3;
    out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
    out_frame[i++] = 0; // reserved1
    out_frame[i++] = 0; // reserved2
  } else {
    out_frame[i++] = RESP_CODE_CHANNEL_MSG_RECV;
  }

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  uint8_t path_len = out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;

  out_frame[i++] = TXT_TYPE_PLAIN;
  memcpy(&out_frame[i], &timestamp, 4);
  i += 4;
  int tlen = strlen(text); // TODO: UTF-8 ??
  if (i + tlen > MAX_FRAME_SIZE) {
    tlen = MAX_FRAME_SIZE - i;
  }
  memcpy(&out_frame[i], text, tlen);
  i += tlen;
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
  if (_listener) {
    // Get the channel name from the channel index
    ChannelDetails channel_details;
    if (!getChannel(channel_idx, channel_details)) {
      strcpy(channel_details.name, "Unknown");
    }
    _listener->onChannelMessageRecv(pkt, channel_details, text);
    _listener->onQueueSizeChanged(offline_queue_len);
  }
}

void MyMesh::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                               const uint8_t *data, size_t data_len) {
  if (data_len > MAX_CHANNEL_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("onChannelDataRecv: dropping payload_len=%d exceeds frame limit=%d",
                       (uint32_t)data_len, (uint32_t)MAX_CHANNEL_DATA_LENGTH);
    return;
  }

  int i = 0;
  out_frame[i++] = RESP_CODE_CHANNEL_DATA_RECV;
  out_frame[i++] = (int8_t)(pkt->getSNR() * 4);
  out_frame[i++] = 0; // reserved1
  out_frame[i++] = 0; // reserved2

  uint8_t channel_idx = findChannelIdx(channel);
  out_frame[i++] = channel_idx;
  out_frame[i++] = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  out_frame[i++] = (uint8_t)(data_type & 0xFF);
  out_frame[i++] = (uint8_t)(data_type >> 8);
  out_frame[i++] = (uint8_t)data_len;

  int copy_len = (int)data_len;
  if (copy_len > 0) {
    memcpy(&out_frame[i], data, copy_len);
    i += copy_len;
  }
  addToOfflineQueue(out_frame, i);

  if (_serial->isConnected()) {
    uint8_t frame[1];
    frame[0] = PUSH_CODE_MSG_WAITING; // send push 'tickle'
    _serial->writeFrame(frame, 1);
  }
  if (_listener) {
    _listener->onChannelDataRecv(pkt, channel, data_type, data, data_len);
  }
}

uint8_t MyMesh::onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                                 uint8_t len, uint8_t *reply) {
  if (data[0] == REQ_TYPE_GET_TELEMETRY_DATA) {
    uint8_t permissions = 0;
    uint8_t cp = contact.flags >> 1; // LSB used as 'favourite' bit (so only use upper bits)

    if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_ALL) {
      permissions = TELEM_PERM_BASE;
    } else if (_prefs.telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS) {
      permissions = cp & TELEM_PERM_BASE;
    }

    if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_LOCATION;
    } else if (_prefs.telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_LOCATION;
    }

    if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_ALL) {
      permissions |= TELEM_PERM_ENVIRONMENT;
    } else if (_prefs.telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS) {
      permissions |= cp & TELEM_PERM_ENVIRONMENT;
    }

    uint8_t perm_mask = ~(data[1]);    // NEW: first reserved byte (of 4), is now inverse mask to apply to permissions
    permissions &= perm_mask;

    if (permissions & TELEM_PERM_BASE) { // only respond if base permission bit is set
      telemetry.reset();
      telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      // query other sensors -- target specific
      sensors.querySensors(permissions, telemetry);

      float temperature = board.getMCUTemperature();
      if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
        telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
      }

      memcpy(reply, &sender_timestamp,
             4); // reflect sender_timestamp back in response packet (kind of like a 'tag')

      uint8_t tlen = telemetry.getSize();
      memcpy(&reply[4], telemetry.getBuffer(), tlen);
      return 4 + tlen;
    }
  } else if (_listener) {
    return _listener->onUnhandledRequest(contact, sender_timestamp, data, len, reply);
  }
  return 0; // unknown
}

void MyMesh::onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) {
  uint32_t tag;
  memcpy(&tag, data, 4);

  if (pending_login && memcmp(&pending_login, contact.id.pub_key, 4) == 0) { // check for login response
    // yes, is response to pending sendLogin()
    pending_login = 0;

    int i = 0;
    if (memcmp(&data[4], "OK", 2) == 0) { // legacy Repeater login OK response
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = 0; // legacy: is_admin = false
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6;                                     // pub_key_prefix
    } else if (data[4] == RESP_SERVER_LOGIN_OK) { // new login response
      uint16_t keep_alive_secs = ((uint16_t)data[5]) * 16;
      if (keep_alive_secs > 0) {
        startConnection(contact, keep_alive_secs);
      }
      out_frame[i++] = PUSH_CODE_LOGIN_SUCCESS;
      out_frame[i++] = data[6]; // permissions (eg. is_admin)
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
      memcpy(&out_frame[i], &tag, 4);
      i += 4; // NEW: include server timestamp
      out_frame[i++] = data[7]; // NEW (v7): ACL permissions
      out_frame[i++] = data[12]; // FIRMWARE_VER_LEVEL
    } else {
      out_frame[i++] = PUSH_CODE_LOGIN_FAIL;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], contact.id.pub_key, 6);
      i += 6; // pub_key_prefix
    }
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && // check for status response
             pending_status &&
             memcmp(&pending_status, contact.id.pub_key, 4) == 0 // legacy matching scheme
                                                                 // FUTURE: tag == pending_status
  ) {
    pending_status = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_STATUS_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_telemetry) {  // check for matching response tag
    pending_telemetry = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], contact.id.pub_key, 6);
    i += 6; // pub_key_prefix
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else if (len > 4 && tag == pending_req) {  // check for matching response tag
    pending_req = 0;

    int i = 0;
    out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], &tag, 4);   // app needs to match this to RESP_CODE_SENT.tag
    i += 4;
    memcpy(&out_frame[i], &data[4], len - 4);
    i += (len - 4);
    _serial->writeFrame(out_frame, i);
  } else {
    bool handled;
    if (_listener && len > 4) {
      handled = _listener->onUnhandledResponse(contact, tag, &data[4], len - 4);
    } else {
      handled = false;
    }
    if (!handled) {   // let the app know about unknown/unhandled responses (eg. push telemetry packets)
      int i = 0;
      out_frame[i++] = PUSH_CODE_BINARY_RESPONSE;
      out_frame[i++] = 0; // reserved
      memcpy(&out_frame[i], data, len);
      i += len;
      _serial->writeFrame(out_frame, i);
    }
  }
}

bool MyMesh::onContactPathRecv(ContactInfo& contact, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) {
  if (extra_type == PAYLOAD_TYPE_RESPONSE && extra_len > 4) {
    uint32_t tag;
    memcpy(&tag, extra, 4);

    if (tag == pending_discovery) {  // check for matching response tag)
      pending_discovery = 0;

      if (!mesh::Packet::isValidPathLen(in_path_len) || !mesh::Packet::isValidPathLen(out_path_len)) {
        MESH_DEBUG_PRINTLN("onContactPathRecv, invalid path sizes: %d, %d", in_path_len, out_path_len);
      } else {
        int i = 0;
        out_frame[i++] = PUSH_CODE_PATH_DISCOVERY_RESPONSE;
        out_frame[i++] = 0; // reserved
        memcpy(&out_frame[i], contact.id.pub_key, 6);
        i += 6; // pub_key_prefix
        out_frame[i++] = out_path_len;
        i += mesh::Packet::writePath(&out_frame[i], out_path, out_path_len);
        out_frame[i++] = in_path_len;
        i += mesh::Packet::writePath(&out_frame[i], in_path, in_path_len);
        // NOTE: telemetry data in 'extra' is discarded at present

        _serial->writeFrame(out_frame, i);
      }
      return false;  // DON'T send reciprocal path!
    }
  }
  // let base class handle received path and data
  return BaseChatMesh::onContactPathRecv(contact, in_path, in_path_len, out_path, out_path_len, extra_type, extra, extra_len);
}

void MyMesh::onControlDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_CONTROL_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = packet->path_len;
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onControlDataRecv(), data received while app offline");
  }

  if (_listener) _listener->onControlDataRecv(packet);
}

void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), payload_len too long: %d", packet->payload_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_RAW_DATA;
  out_frame[i++] = (int8_t)(_radio->getLastSNR() * 4);
  out_frame[i++] = (int8_t)(_radio->getLastRSSI());
  out_frame[i++] = 0xFF; // reserved (possibly path_len in future)
  memcpy(&out_frame[i], packet->payload, packet->payload_len);
  i += packet->payload_len;

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onRawDataRecv(), data received while app offline");
  }
  if (_listener) {
    _listener->onRawDataRecv(packet);
  }
}

void MyMesh::onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) {
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if (12 + path_len + (path_len >> path_sz) + 1 > sizeof(out_frame)) {
    MESH_DEBUG_PRINTLN("onTraceRecv(), path_len is too long: %d", (uint32_t)path_len);
    return;
  }
  int i = 0;
  out_frame[i++] = PUSH_CODE_TRACE_DATA;
  out_frame[i++] = 0; // reserved
  out_frame[i++] = path_len;
  out_frame[i++] = flags;
  memcpy(&out_frame[i], &tag, 4);
  i += 4;
  memcpy(&out_frame[i], &auth_code, 4);
  i += 4;
  memcpy(&out_frame[i], path_hashes, path_len);
  i += path_len;

  memcpy(&out_frame[i], path_snrs, path_len >> path_sz);
  i += path_len >> path_sz;
  out_frame[i++] = (int8_t)(packet->getSNR() * 4); // extra/final SNR (to this node)

  if (_serial->isConnected()) {
    _serial->writeFrame(out_frame, i);
  } else {
    MESH_DEBUG_PRINTLN("onTraceRecv(), data received while app offline");
  }
  if (_listener) {
    _listener->onTraceRecv(packet, tag, auth_code, flags, path_snrs, path_hashes, path_len);
  }
}

uint32_t MyMesh::calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const {
  return SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * pkt_airtime_millis);
}
uint32_t MyMesh::calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const {
  uint8_t path_hash_count = path_len & 63;
  return SEND_TIMEOUT_BASE_MILLIS +
         ((pkt_airtime_millis * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
          (path_hash_count + 1));
}

void MyMesh::onSendTimeout() {}

MyMesh::MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store)
    : BaseChatMesh(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), tables),
      _serial(NULL), telemetry(MAX_PACKET_PAYLOAD - 4), _store(&store), _listener(NULL), _iter(0) {
  useRadioPrefs(&_prefs);   // the core reads its tuning (delays, duty cycle, CAD, AGC, extra ACKs) from these settings
  _iter_started = false;
  _cli_rescue = false;
  cli_command[0] = 0;
  offline_queue_len = 0;
  app_target_ver = 0;
  clearPendingReqs();
  next_ack_idx = 0;
  sign_data = NULL;
  dirty_contacts_expiry = 0;
  memset(advert_paths, 0, sizeof(advert_paths));
  memset(send_scope.key, 0, sizeof(send_scope.key));
  send_unscoped = false;

  // defaults
  _prefs.airtime_factor = 1.0;
  _prefs.tx_delay_factor = 0.5f;
  _prefs.direct_tx_delay_factor = 0.2f;
  strcpy(_prefs.node_name, "NONAME");
  _prefs.freq = LORA_FREQ;
  _prefs.sf = LORA_SF;
  _prefs.bw = LORA_BW;
  _prefs.cr = LORA_CR;
  _prefs.tx_power_dbm = LORA_TX_POWER;
  _prefs.gps_enabled = 0;       // GPS disabled by default
  _prefs.gps_interval = 0;      // No automatic GPS updates by default
  _prefs.radio_fem_rxgain = 1;
#ifdef RADIO_FEM_TXGAIN
  _prefs.radio_fem_txgain = RADIO_FEM_TXGAIN;   // board-specific default for PA
#else
  _prefs.radio_fem_txgain = 0;
#endif
  //_prefs.rx_delay_base = 10.0f;  enable once new algo fixed
  _prefs.setRepeatEn(false);
#if defined(USE_SX1262) || defined(USE_SX1268)
#ifdef SX126X_RX_BOOSTED_GAIN
  _prefs.rx_boosted_gain = SX126X_RX_BOOSTED_GAIN;
#else
  _prefs.rx_boosted_gain = 1; // enabled by default
#endif
#endif
}

void MyMesh::begin() {
  BaseChatMesh::begin();

  if (!_store->loadMainIdentity(self_id)) {
    self_id = radio_new_identity(); // create new random identity
    int count = 0;
    while (count < 10 && (self_id.pub_key[0] == 0x00 || self_id.pub_key[0] == 0xFF)) { // reserved id hashes
      self_id = radio_new_identity();
      count++;
    }
    _store->saveMainIdentity(self_id);
  }

// if name is provided as a build flag, use that as default node name instead
#ifdef ADVERT_NAME
  strcpy(_prefs.node_name, ADVERT_NAME);
#else
  // use hex of first 4 bytes of identity public key as default node name
  char pub_key_hex[10];
  mesh::Utils::toHex(pub_key_hex, self_id.pub_key, 4);
  strcpy(_prefs.node_name, pub_key_hex);
#endif

  // if build provides default-scope, init with that
#ifdef DEFAULT_FLOOD_SCOPE_NAME
  strcpy(_prefs.default_scope_name, DEFAULT_FLOOD_SCOPE_NAME);
  {
    TransportKeyStore temp;
    TransportKey key;
    temp.getAutoKeyFor(0, "#" DEFAULT_FLOOD_SCOPE_NAME, key);
    memcpy(_prefs.default_scope_key, key.key, sizeof(key.key));
  }
#endif

  // load persisted prefs
  _store->loadPrefs(_prefs);
  sensors.node_lat = _prefs.node_lat;
  sensors.node_lon = _prefs.node_lon;

  // sanitise bad pref values
  _prefs.rx_delay_base = constrain(_prefs.rx_delay_base, 0, 20.0f);
  _prefs.tx_delay_factor = constrain(_prefs.tx_delay_factor, 0, 2.0f);
  _prefs.direct_tx_delay_factor = constrain(_prefs.direct_tx_delay_factor, 0, 2.0f);
  _prefs.airtime_factor = constrain(_prefs.airtime_factor, 0, 9.0f);
  _prefs.freq = constrain(_prefs.freq, 150.0f, 2500.0f);
  _prefs.bw = constrain(_prefs.bw, 7.8f, 500.0f);
  _prefs.sf = constrain(_prefs.sf, 5, 12);
  _prefs.cr = constrain(_prefs.cr, 5, 8);
  _prefs.tx_power_dbm = constrain(_prefs.tx_power_dbm, -9, MAX_LORA_TX_POWER);
  _prefs.gps_enabled = constrain(_prefs.gps_enabled, 0, 1);  // Ensure boolean 0 or 1
  _prefs.gps_interval = constrain(_prefs.gps_interval, 0, 86400);  // Max 24 hours

  _active_ble_pin = _prefs.ble_pin;

  resetContacts();
  _store->loadContacts(this);
  bootstrapRTCfromContacts();
  addChannel("Public", PUBLIC_GROUP_PSK); // pre-configure public channel
  _store->loadChannels(this);

  radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
  radio_driver.setTxPower(_prefs.tx_power_dbm);
  radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain);

  board.attachDynamicPrefs(_prefs.getCustom());

  MESH_DEBUG_PRINTLN("RX Boosted Gain Mode: %s",
                     radio_driver.getRxBoostedGainMode() ? "Enabled" : "Disabled");
}

const char *MyMesh::getNodeName() {
  return _prefs.node_name;
}
NodePrefs *MyMesh::getNodePrefs() {
  return &_prefs;
}
uint32_t MyMesh::getBLEPin() {
  return _active_ble_pin;
}
void MyMesh::setBLEPin(uint32_t active_pin) {
  _active_ble_pin = active_pin;
}

struct FreqRange {
  uint32_t lower_freq, upper_freq;
};

static FreqRange repeat_freq_ranges[] = {
  #ifdef ALLOWED_REPEAT_FREQ_RANGE
  ALLOWED_REPEAT_FREQ_RANGE
  #else
  { 433000, 433000 },
  { 869495, 869495 },
  { 918000, 918000 }
  #endif
};

bool MyMesh::isValidClientRepeatFreq(uint32_t f) const {
  for (int i = 0; i < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]); i++) {
    auto r = &repeat_freq_ranges[i];
    if (f >= r->lower_freq && f <= r->upper_freq) return true;
  }
  return false;
}

void MyMesh::startInterface(BaseSerialInterface &serial) {
  _serial = &serial;
  serial.enable();
}

// The bytes a path length byte announces; Packet::writePath() copies as many.
static size_t pathByteLen(uint8_t path_len) {
  return (path_len & 63) * ((path_len >> 6) + 1);
}

// One row per command: the code, the shortest frame the command accepts (code byte
// included), and its handler. A frame below the minimum, a code without a row, and
// a handler that returns false ("not this command") all get the answer the if chain
// this table replaced gave them: ERR_CODE_UNSUPPORTED_CMD.
const MyMesh::CmdHandler MyMesh::CMD_TABLE[] = {
  { CMD_DEVICE_QUERY,            2,                                                 &MyMesh::cmdDeviceQuery },
  { CMD_APP_START,               8,                                                 &MyMesh::cmdAppStart },
  { CMD_RUN_CLI_COMMAND,         3,                                                 &MyMesh::cmdRunCliCommand },
  { CMD_SEND_TXT_MSG,            14,                                                &MyMesh::cmdSendTxtMsg },
  { CMD_SEND_CHANNEL_TXT_MSG,    7,                                                 &MyMesh::cmdSendChannelTxtMsg },
  { CMD_SEND_CHANNEL_DATA,       1,                                                 &MyMesh::cmdSendChannelData },
  { CMD_GET_CONTACTS,            1,                                                 &MyMesh::cmdGetContacts },
  { CMD_SET_ADVERT_NAME,         2,                                                 &MyMesh::cmdSetAdvertName },
  { CMD_SET_ADVERT_LATLON,       9,                                                 &MyMesh::cmdSetAdvertLatlon },
  { CMD_GET_DEVICE_TIME,         1,                                                 &MyMesh::cmdGetDeviceTime },
  { CMD_SET_DEVICE_TIME,         5,                                                 &MyMesh::cmdSetDeviceTime },
  { CMD_SEND_SELF_ADVERT,        1,                                                 &MyMesh::cmdSendSelfAdvert },
  { CMD_RESET_PATH,              1 + 32,                                            &MyMesh::cmdResetPath },
  { CMD_ADD_UPDATE_CONTACT,      1 + PUB_KEY_SIZE + 3 + MAX_PATH_SIZE + 32 + 4,     &MyMesh::cmdAddUpdateContact },
  { CMD_REMOVE_CONTACT,          1 + PUB_KEY_SIZE,                                  &MyMesh::cmdRemoveContact },
  { CMD_SHARE_CONTACT,           1 + PUB_KEY_SIZE,                                  &MyMesh::cmdShareContact },
  { CMD_GET_CONTACT_BY_KEY,      1 + PUB_KEY_SIZE,                                  &MyMesh::cmdGetContactByKey },
  { CMD_EXPORT_CONTACT,          1,                                                 &MyMesh::cmdExportContact },
  { CMD_IMPORT_CONTACT,          2 + 32 + 64 + 1,                                   &MyMesh::cmdImportContact },
  { CMD_SYNC_NEXT_MESSAGE,       1,                                                 &MyMesh::cmdSyncNextMessage },
  { CMD_SET_RADIO_PARAMS,        11,                                                &MyMesh::cmdSetRadioParams },
  { CMD_SET_RADIO_TX_POWER,      2,                                                 &MyMesh::cmdSetRadioTxPower },
  { CMD_SET_TUNING_PARAMS,       9,                                                 &MyMesh::cmdSetTuningParams },
  { CMD_GET_TUNING_PARAMS,       1,                                                 &MyMesh::cmdGetTuningParams },
  { CMD_SET_OTHER_PARAMS,        2,                                                 &MyMesh::cmdSetOtherParams },
  { CMD_SET_PATH_HASH_MODE,      3,                                                 &MyMesh::cmdSetPathHashMode },
  { CMD_REBOOT,                  7,                                                 &MyMesh::cmdReboot },
  { CMD_GET_BATT_AND_STORAGE,    1,                                                 &MyMesh::cmdGetBattAndStorage },
  { CMD_EXPORT_PRIVATE_KEY,      1,                                                 &MyMesh::cmdExportPrivateKey },
  { CMD_IMPORT_PRIVATE_KEY,      65,                                                &MyMesh::cmdImportPrivateKey },
  { CMD_SEND_RAW_DATA,           6,                                                 &MyMesh::cmdSendRawData },
  { CMD_SEND_LOGIN,              1 + PUB_KEY_SIZE,                                  &MyMesh::cmdSendLogin },
  { CMD_SEND_ANON_REQ,           1 + PUB_KEY_SIZE + 1,                              &MyMesh::cmdSendAnonReq },
  { CMD_SEND_STATUS_REQ,         1 + PUB_KEY_SIZE,                                  &MyMesh::cmdSendStatusReq },
  { CMD_SEND_PATH_DISCOVERY_REQ, 2 + PUB_KEY_SIZE,                                  &MyMesh::cmdSendPathDiscoveryReq },
  { CMD_SEND_TELEMETRY_REQ,      4,                                                 &MyMesh::cmdSendTelemetryReq },
  { CMD_SEND_BINARY_REQ,         2 + PUB_KEY_SIZE,                                  &MyMesh::cmdSendBinaryReq },
  { CMD_HAS_CONNECTION,          1 + PUB_KEY_SIZE,                                  &MyMesh::cmdHasConnection },
  { CMD_LOGOUT,                  1 + PUB_KEY_SIZE,                                  &MyMesh::cmdLogout },
  { CMD_GET_CHANNEL,             2,                                                 &MyMesh::cmdGetChannel },
  { CMD_SET_CHANNEL,             2 + 32 + 16,                                       &MyMesh::cmdSetChannel },
  { CMD_SIGN_START,              1,                                                 &MyMesh::cmdSignStart },
  { CMD_SIGN_DATA,               1 + 1,                                             &MyMesh::cmdSignData },
  { CMD_SIGN_FINISH,             1,                                                 &MyMesh::cmdSignFinish },
  { CMD_SEND_TRACE_PATH,         10 + 1,                                            &MyMesh::cmdSendTracePath },
  { CMD_SET_DEVICE_PIN,          5,                                                 &MyMesh::cmdSetDevicePin },
  { CMD_GET_CUSTOM_VARS,         1,                                                 &MyMesh::cmdGetCustomVars },
  { CMD_SET_CUSTOM_VAR,          4,                                                 &MyMesh::cmdSetCustomVar },
  { CMD_GET_ADVERT_PATH,         PUB_KEY_SIZE+2,                                    &MyMesh::cmdGetAdvertPath },
  { CMD_GET_STATS,               2,                                                 &MyMesh::cmdGetStats },
  { CMD_FACTORY_RESET,           6,                                                 &MyMesh::cmdFactoryReset },
  { CMD_SET_FLOOD_SCOPE_KEY,     2,                                                 &MyMesh::cmdSetFloodScopeKey },
  { CMD_SET_DEFAULT_FLOOD_SCOPE, 1,                                                 &MyMesh::cmdSetDefaultFloodScope },
  { CMD_GET_DEFAULT_FLOOD_SCOPE, 1,                                                 &MyMesh::cmdGetDefaultFloodScope },
  { CMD_SEND_CONTROL_DATA,       2,                                                 &MyMesh::cmdSendControlData },
  { CMD_SET_AUTOADD_CONFIG,      2,                                                 &MyMesh::cmdSetAutoaddConfig },
  { CMD_GET_AUTOADD_CONFIG,      1,                                                 &MyMesh::cmdGetAutoaddConfig },
  { CMD_GET_ALLOWED_REPEAT_FREQ, 1,                                                 &MyMesh::cmdGetAllowedRepeatFreq },
  { CMD_SEND_RAW_PACKET,         4,                                                 &MyMesh::cmdSendRawPacket },
};

void MyMesh::handleCmdFrame(size_t len) {
  for (const CmdHandler& c : CMD_TABLE) {
    if (c.code != cmd_frame[0]) continue;
    if (len >= c.min_len && (this->*c.handle)(len)) return;
    break;
  }
  writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
  MESH_DEBUG_PRINTLN("ERROR: unknown command: %02X", cmd_frame[0]);
}

// sent when app establishes connection
bool MyMesh::cmdDeviceQuery(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  app_target_ver = r.u8();                    // which version of protocol does app understand

  int i = 0;
  out_frame[i++] = RESP_CODE_DEVICE_INFO;
  out_frame[i++] = FIRMWARE_VER_CODE;
  out_frame[i++] = MAX_CONTACTS / 2;   // v3+
  out_frame[i++] = MAX_GROUP_CHANNELS; // v3+
  memcpy(&out_frame[i], &_prefs.ble_pin, 4);
  i += 4;
  memset(&out_frame[i], 0, 12);
  strcpy((char *)&out_frame[i], FIRMWARE_BUILD_DATE);
  i += 12;
  StrHelper::strzcpy((char *)&out_frame[i], board.getManufacturerName(), 40);
  i += 40;
  StrHelper::strzcpy((char *)&out_frame[i], FIRMWARE_VERSION, 20);
  i += 20;
  out_frame[i++] = _prefs.isRepeatEn() ? 1 : 0;   // v9+
  out_frame[i++] = _prefs.path_hash_mode;  // v10+
  _serial->writeFrame(out_frame, i);
  return true;
}

// sent when app establishes connection, respond with node ID
bool MyMesh::cmdAppStart(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  r.skip(7);  // reserved future
  char *app_name = r.restAsCString();
  MESH_DEBUG_PRINTLN("App %s connected", app_name);

  _iter_started = false; // stop any left-over ContactsIterator
  int i = 0;
  out_frame[i++] = RESP_CODE_SELF_INFO;
  out_frame[i++] = ADV_TYPE_CHAT; // what this node Advert identifies as (maybe node's pronouns too?? :-)
  out_frame[i++] = _prefs.tx_power_dbm;
  out_frame[i++] = MAX_LORA_TX_POWER;
  memcpy(&out_frame[i], self_id.pub_key, PUB_KEY_SIZE);
  i += PUB_KEY_SIZE;

  int32_t lat, lon;
  lat = (sensors.node_lat * 1000000.0);
  lon = (sensors.node_lon * 1000000.0);
  memcpy(&out_frame[i], &lat, 4);
  i += 4;
  memcpy(&out_frame[i], &lon, 4);
  i += 4;
  out_frame[i++] = _prefs.multi_acks; // new v7+
  out_frame[i++] = _prefs.advert_loc_policy;
  out_frame[i++] = (_prefs.telemetry_mode_env << 4) | (_prefs.telemetry_mode_loc << 2) |
                   (_prefs.telemetry_mode_base); // v5+
  out_frame[i++] = _prefs.manual_add_contacts;

  uint32_t freq = _prefs.freq * 1000;
  memcpy(&out_frame[i], &freq, 4);
  i += 4;
  uint32_t bw = _prefs.bw * 1000;
  memcpy(&out_frame[i], &bw, 4);
  i += 4;
  out_frame[i++] = _prefs.sf;
  out_frame[i++] = _prefs.cr;

  int tlen = strlen(_prefs.node_name); // revisit: UTF_8 ??
  memcpy(&out_frame[i], _prefs.node_name, tlen);
  i += tlen;
  _serial->writeFrame(out_frame, i);
  return true;
}

// V14+
bool MyMesh::cmdRunCliCommand(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  char *text = r.restAsCString();

  reply_buf[0] = 0;
  if (!handleCommand(text, 0, reply_buf)) {
    strcat(reply_buf, "Unknown command");   // reply_buf may have cmd prefix from 'text'
  }
  out_frame[0] = RESP_CODE_CLI_REPLY;
  int rlen = strlen(reply_buf);
  memcpy(&out_frame[1], reply_buf, rlen);
  _serial->writeFrame(out_frame, 1 + rlen);
  return true;
}

bool MyMesh::cmdSendTxtMsg(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t txt_type = r.u8();
  uint8_t attempt = r.u8();
  uint32_t msg_timestamp = r.u32();
  uint8_t *pub_key_prefix = r.bytes(6);
  ContactInfo *recipient = lookupContactByPubKey(pub_key_prefix, 6);
  if (recipient && (txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND)) {
    char *text = r.restAsCString();
    uint32_t est_timeout;
    int result;
    uint32_t expected_ack;
    if (txt_type == TXT_TYPE_CLI_DATA || txt_type == TXT_TYPE_CLI_COMMAND) {
      msg_timestamp = getRTCClock()->getCurrentTimeUnique(); // Use node's RTC instead of app timestamp to avoid tripping replay protection
      result = sendCommandData(*recipient, msg_timestamp, attempt, txt_type, text, est_timeout);
      expected_ack = 0; // no Ack expected
    } else {
      result = sendMessage(*recipient, msg_timestamp, attempt, text, expected_ack, est_timeout);
    }
    // TODO: add expected ACK to table
    if (result == MSG_SEND_FAILED) {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    } else {
      if (expected_ack) {
        expected_ack_table[next_ack_idx].msg_sent = _ms->getMillis(); // add to circular table
        expected_ack_table[next_ack_idx].ack = expected_ack;
        expected_ack_table[next_ack_idx].contact = recipient;
        next_ack_idx = (next_ack_idx + 1) % EXPECTED_ACK_TABLE_SIZE;
      }

      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
      memcpy(&out_frame[2], &expected_ack, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    }
  } else {
    writeErrFrame(recipient == NULL
                      ? ERR_CODE_NOT_FOUND
                      : ERR_CODE_UNSUPPORTED_CMD); // unknown recipient, or unsupported TXT_TYPE_*
  }
  return true;
}

// send GroupChannel text msg
bool MyMesh::cmdSendChannelTxtMsg(size_t len) {
  // 7 = code, txt_type, channel_idx and the 4 byte timestamp; the text may be empty.
  // Without this the text length below is computed as len - 7 and turns negative.
  FrameReader r(cmd_frame, len, 1);
  uint8_t txt_type = r.u8(); // should be TXT_TYPE_PLAIN
  uint8_t channel_idx = r.u8();
  uint32_t msg_timestamp = r.u32();
  int text_len = r.remaining();
  const char *text = (char *)r.bytes(text_len);

  if (txt_type != TXT_TYPE_PLAIN) {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD);
  } else {
    ChannelDetails channel;
    bool success = getChannel(channel_idx, channel);
    if (success && sendGroupMessage(msg_timestamp, channel.channel, _prefs.node_name, text, text_len)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
  }
  return true;
}

// send GroupChannel datagram
bool MyMesh::cmdSendChannelData(size_t len) {
  if (len < 4) {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    return true;
  }
  FrameReader r(cmd_frame, len, 1);
  uint8_t channel_idx = r.u8();
  uint8_t path_len = r.u8();

  // validate path len, allowing 0xFF for flood
  if (!mesh::Packet::isValidPathLen(path_len) && path_len != OUT_PATH_UNKNOWN) {
    MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA invalid path size: %d", path_len);
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    return true;
  }

  // parse provided path if not flood
  uint8_t path[MAX_PATH_SIZE];
  if (path_len != OUT_PATH_UNKNOWN) {
    const uint8_t *src = r.bytes(pathByteLen(path_len));
    if (src) mesh::Packet::writePath(path, src, path_len);
  }

  if (!r.ok() || !r.has(2)) {   // the path and the 2 byte data_type must be inside the frame
    MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA frame too short for data_type: %d", (uint32_t)len);
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    return true;
  }

  uint16_t data_type = r.u8();
  data_type |= ((uint16_t)r.u8()) << 8;
  int payload_len = r.remaining();
  const uint8_t *payload = r.bytes(payload_len);

  ChannelDetails channel;
  if (!getChannel(channel_idx, channel)) {
    writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
  } else if (data_type == DATA_TYPE_RESERVED) {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else if (payload_len > MAX_GROUP_DATA_LENGTH) {
    MESH_DEBUG_PRINTLN("CMD_SEND_CHANNEL_DATA payload too long: %d > %d", payload_len, MAX_GROUP_DATA_LENGTH);
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else if (sendGroupData(channel.channel, path, path_len, data_type, payload, payload_len)) {
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_TABLE_FULL);
  }
  return true;
}

// get Contact list
bool MyMesh::cmdGetContacts(size_t len) {
  if (_iter_started) {
    writeErrFrame(ERR_CODE_BAD_STATE); // iterator is currently busy
  } else {
    FrameReader r(cmd_frame, len, 1);
    if (r.has(4)) { // has optional 'since' param
      _iter_filter_since = r.u32();
    } else {
      _iter_filter_since = 0;
    }

    uint8_t reply[5];
    reply[0] = RESP_CODE_CONTACTS_START;
    uint32_t count = getNumContacts(); // total, NOT filtered count
    memcpy(&reply[1], &count, 4);
    _serial->writeFrame(reply, 5);

    // start iterator
    _iter = startContactsIterator();
    _iter_started = true;
    _most_recent_lastmod = 0;
  }
  return true;
}

bool MyMesh::cmdSetAdvertName(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  int nlen = r.remaining();
  if (nlen > sizeof(_prefs.node_name) - 1) nlen = sizeof(_prefs.node_name) - 1; // max len
  memcpy(_prefs.node_name, r.bytes(nlen), nlen);
  _prefs.node_name[nlen] = 0; // null terminator
  savePrefs();
  writeOKFrame();
  return true;
}

bool MyMesh::cmdSetAdvertLatlon(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  int32_t lat = r.i32();
  int32_t lon = r.i32();
  int32_t alt = 0;
  if (r.has(4)) {
    alt = r.i32(); // for FUTURE support
  }
  if (lat <= 90 * 1E6 && lat >= -90 * 1E6 && lon <= 180 * 1E6 && lon >= -180 * 1E6) {
    sensors.node_lat = ((double)lat) / 1000000.0;
    sensors.node_lon = ((double)lon) / 1000000.0;
    savePrefs();
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid geo coordinate
  }
  return true;
}

bool MyMesh::cmdGetDeviceTime(size_t) {
  uint8_t reply[5];
  reply[0] = RESP_CODE_CURR_TIME;
  uint32_t now = getRTCClock()->getCurrentTime();
  memcpy(&reply[1], &now, 4);
  _serial->writeFrame(reply, 5);
  return true;
}

bool MyMesh::cmdSetDeviceTime(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint32_t secs = r.u32();
  uint32_t curr = getRTCClock()->getCurrentTime();
  if (secs >= curr) {
    getRTCClock()->setCurrentTime(secs);
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  }
  return true;
}

bool MyMesh::cmdSendSelfAdvert(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    if (r.has(1) && r.u8() == 1) { // optional param (1 = flood, 0 = zero hop)
      unsigned long delay_millis = 0;
      TransportKey default_scope;
      memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));
      sendFloodScoped(default_scope, pkt, delay_millis);
    } else {
      sendZeroHop(pkt);
    }
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_TABLE_FULL);
  }
  return true;
}

bool MyMesh::cmdResetPath(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (recipient) {
    recipient->out_path_len = OUT_PATH_UNKNOWN;
    // recipient->lastmod = ??   shouldn't be needed, app already has this version of contact
    dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // unknown contact
  }
  return true;
}

bool MyMesh::cmdAddUpdateContact(size_t len) {
  // updateContactFromFrame() reads every field up to last_advert_timestamp without
  // testing len; only the trailing lat/lon and lastmod are optional. The reverse
  // direction (writeContactRespFrame) writes the same fields in the same order.
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  uint32_t last_mod = getRTCClock()->getCurrentTime();  // fallback value if not present in cmd_frame
  if (recipient) {
    updateContactFromFrame(*recipient, last_mod, cmd_frame, len);
    recipient->lastmod = last_mod;
    dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    writeOKFrame();
  } else {
    ContactInfo contact;
    updateContactFromFrame(contact, last_mod, cmd_frame, len);
    contact.lastmod = last_mod;
    contact.sync_since = 0;
    if (addContact(contact)) {
      dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  }
  return true;
}

bool MyMesh::cmdRemoveContact(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (recipient && removeContact(*recipient)) {
    _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
    dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // not found, or unable to remove
  }
  return true;
}

bool MyMesh::cmdShareContact(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (recipient) {
    if (shareContactZeroHop(*recipient)) {
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // unable to send
    }
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND);
  }
  return true;
}

bool MyMesh::cmdGetContactByKey(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *contact = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (contact) {
    writeContactRespFrame(RESP_CODE_CONTACT, *contact);
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // not found
  }
  return true;
}

bool MyMesh::cmdExportContact(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (!r.has(PUB_KEY_SIZE)) {
    // export SELF
    mesh::Packet* pkt;
    if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
      pkt = createSelfAdvert(_prefs.node_name);
    } else {
      pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
    }
    if (pkt) {
      pkt->header |= ROUTE_TYPE_FLOOD; // would normally be sent in this mode

      out_frame[0] = RESP_CODE_EXPORT_CONTACT;
      uint8_t out_len = pkt->writeTo(&out_frame[1]);
      releasePacket(pkt); // undo the obtainNewPacket()
      _serial->writeFrame(out_frame, out_len + 1);
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL); // Error
    }
  } else {
    uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    uint8_t out_len;
    if (recipient && (out_len = exportContact(*recipient, &out_frame[1])) > 0) {
      out_frame[0] = RESP_CODE_EXPORT_CONTACT;
      _serial->writeFrame(out_frame, out_len + 1);
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // not found
    }
  }
  return true;
}

bool MyMesh::cmdImportContact(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  size_t n = r.remaining();
  if (importContact(r.bytes(n), n)) {
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  }
  return true;
}

bool MyMesh::cmdSyncNextMessage(size_t) {
  int out_len;
  if ((out_len = getFromOfflineQueue(out_frame)) > 0) {
    _serial->writeFrame(out_frame, out_len);
    if (_listener) _listener->onQueueSizeChanged(offline_queue_len);
  } else {
    out_frame[0] = RESP_CODE_NO_MORE_MESSAGES;
    _serial->writeFrame(out_frame, 1);
  }
  return true;
}

// freq, bw, sf, cr; the repeat flag stays optional
bool MyMesh::cmdSetRadioParams(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint32_t freq = r.u32();
  uint32_t bw = r.u32();
  uint8_t sf = r.u8();
  uint8_t cr = r.u8();
  uint8_t repeat = 0;  // default - false
  if (r.has(1)) {
    repeat = r.u8();   // FIRMWARE_VER_CODE  9+
  }

  if (repeat && !isValidClientRepeatFreq(freq)) {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else if (freq >= 150000 && freq <= 2500000 && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7000 &&
      bw <= 500000) {
    _prefs.sf = sf;
    _prefs.cr = cr;
    _prefs.freq = (float)freq / 1000.0;
    _prefs.bw = (float)bw / 1000.0;
    _prefs.setRepeatEn(repeat != 0);
    savePrefs();

    radio_driver.setParams(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    MESH_DEBUG_PRINTLN("OK: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                       (uint32_t)cr);

    writeOKFrame();
  } else {
    MESH_DEBUG_PRINTLN("Error: CMD_SET_RADIO_PARAMS: f=%d, bw=%d, sf=%d, cr=%d", freq, bw, (uint32_t)sf,
                       (uint32_t)cr);
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  }
  return true;
}

bool MyMesh::cmdSetRadioTxPower(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  int8_t power = (int8_t)r.u8();
  if (power < -9 || power > MAX_LORA_TX_POWER) {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else {
    _prefs.tx_power_dbm = power;
    savePrefs();
    radio_driver.setTxPower(_prefs.tx_power_dbm);
    writeOKFrame();
  }
  return true;
}

// two 32 bit values, neither range checked
bool MyMesh::cmdSetTuningParams(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint32_t rx = r.u32();
  uint32_t af = r.u32();
  _prefs.rx_delay_base = ((float)rx) / 1000.0f;
  _prefs.airtime_factor = ((float)af) / 1000.0f;
  savePrefs();
  writeOKFrame();
  return true;
}

bool MyMesh::cmdGetTuningParams(size_t) {
  uint32_t rx = _prefs.rx_delay_base * 1000, af = _prefs.airtime_factor * 1000;
  int i = 0;
  out_frame[i++] = RESP_CODE_TUNING_PARAMS;
  memcpy(&out_frame[i], &rx, 4); i += 4;
  memcpy(&out_frame[i], &af, 4); i += 4;
  _serial->writeFrame(out_frame, i);
  return true;
}

// the fields after manual_add stay optional
bool MyMesh::cmdSetOtherParams(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  _prefs.manual_add_contacts = r.u8();
  if (r.has(1)) {
    uint8_t modes = r.u8();
    _prefs.telemetry_mode_base = modes & 0x03; // v5+
    _prefs.telemetry_mode_loc = (modes >> 2) & 0x03;
    _prefs.telemetry_mode_env = (modes >> 4) & 0x03;

    if (r.has(1)) {
      _prefs.advert_loc_policy = r.u8();
      if (r.has(1)) {
        _prefs.multi_acks = r.u8();
      }
    }
  }
  savePrefs();
  writeOKFrame();
  return true;
}

bool MyMesh::cmdSetPathHashMode(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (r.u8() != 0) return false;
  uint8_t mode = r.u8();
  if (mode >= 3) {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else {
    _prefs.path_hash_mode = mode;
    savePrefs();
    writeOKFrame();
  }
  return true;
}

bool MyMesh::cmdReboot(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (memcmp(r.bytes(6), "reboot", 6) != 0) return false;
  if (dirty_contacts_expiry) { // is there are pending dirty contacts write needed?
    saveContacts();
  }
  board.reboot();
  return true;
}

bool MyMesh::cmdGetBattAndStorage(size_t) {
  uint8_t reply[11];
  int i = 0;
  reply[i++] = RESP_CODE_BATT_AND_STORAGE;
  uint16_t battery_millivolts = board.getBattMilliVolts();
  uint32_t used = _store->getStorageUsedKb();
  uint32_t total = _store->getStorageTotalKb();
  memcpy(&reply[i], &battery_millivolts, 2); i += 2;
  memcpy(&reply[i], &used, 4); i += 4;
  memcpy(&reply[i], &total, 4); i += 4;
  _serial->writeFrame(reply, i);
  return true;
}

bool MyMesh::cmdExportPrivateKey(size_t) {
#if ENABLE_PRIVATE_KEY_EXPORT
  uint8_t reply[65];
  reply[0] = RESP_CODE_PRIVATE_KEY;
  self_id.writeTo(&reply[1], 64);
  _serial->writeFrame(reply, 65);
#else
  writeDisabledFrame();
#endif
  return true;
}

bool MyMesh::cmdImportPrivateKey(size_t len) {
#if ENABLE_PRIVATE_KEY_IMPORT
  FrameReader r(cmd_frame, len, 1);
  uint8_t *key = r.bytes(64);
  if (!mesh::LocalIdentity::validatePrivateKey(key)) {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid key
  } else {
      mesh::LocalIdentity identity;
      identity.readFrom(key, 64);
      if (_store->saveMainIdentity(identity)) {
        self_id = identity;
        writeOKFrame();
        // re-load contacts, to invalidate ecdh shared_secrets
        resetContacts();
        _store->loadContacts(this);
      } else {
        writeErrFrame(ERR_CODE_FILE_IO_ERROR);
      }
  }
#else
  writeDisabledFrame();
#endif
  return true;
}

bool MyMesh::cmdSendRawData(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t path_len = r.u8();
  if (path_len >= 0 && mesh::Packet::isValidPathLen(path_len)) {
    uint8_t path[MAX_PATH_SIZE];
    const uint8_t *src = r.bytes(pathByteLen(path_len));
    if (src) mesh::Packet::writePath(path, src, path_len);
    if (!r.ok() || !r.has(4)) {  // the path and a payload of at least 4 bytes
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    } else {
      size_t n = r.remaining();
      auto pkt = createRawData(r.bytes(n), n);
      if (pkt) {
        sendDirect(pkt, path, path_len);
        writeOKFrame();
      } else {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      }
    }
  } else {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // flood, not supported (yet)
  }
  return true;
}

bool MyMesh::cmdSendLogin(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  char *password = r.restAsCString();
  if (recipient) {
    uint32_t est_timeout;
    int result = sendLogin(*recipient, password, est_timeout);
    if (result == MSG_SEND_FAILED) {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    } else {
      clearPendingReqs();
      memcpy(&pending_login, recipient->id.pub_key, 4); // match this to onContactResponse()
      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
      memcpy(&out_frame[2], &pending_login, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    }
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
  }
  return true;
}

bool MyMesh::cmdSendAnonReq(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  ContactInfo anon;
  if (recipient == NULL) { // FIRMWARE_VER_CODE 13+,  allow non-contact requests
    memset(&anon, 0, sizeof(anon));
    memcpy(anon.id.pub_key, pub_key, PUB_KEY_SIZE);
    anon.out_path_len = 0;   // default to zero-hop direct
    anon.type = ADV_TYPE_NONE;  // unknown
    anon.lastmod = getRTCClock()->getCurrentTime();

    if (addContact(anon)) recipient = &anon;
  }
  size_t data_len = r.remaining();
  uint8_t *data = r.bytes(data_len);
  if (recipient) {
    uint32_t tag, est_timeout;
    int result = sendAnonReq(*recipient, data, data_len, tag, est_timeout);
    if (result == MSG_SEND_FAILED) {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    } else {
      clearPendingReqs();
      pending_req = tag; // match this to onContactResponse()
      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
      memcpy(&out_frame[2], &tag, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    }
  } else {
    writeErrFrame(ERR_CODE_TABLE_FULL); // contacts full
  }
  return true;
}

bool MyMesh::cmdSendStatusReq(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (recipient) {
    uint32_t tag, est_timeout;
    int result = sendRequest(*recipient, REQ_TYPE_GET_STATUS, tag, est_timeout);
    if (result == MSG_SEND_FAILED) {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    } else {
      clearPendingReqs();
      // FUTURE:  pending_status = tag;  // match this in onContactResponse()
      memcpy(&pending_status, recipient->id.pub_key, 4); // legacy matching scheme
      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
      memcpy(&out_frame[2], &tag, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    }
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
  }
  return true;
}

bool MyMesh::cmdSendPathDiscoveryReq(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (r.u8() != 0) return false;
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (recipient) {
    uint32_t tag, est_timeout;
    // 'Path Discovery' is just a special case of flood + Telemetry req
    uint8_t req_data[9];
    req_data[0] = REQ_TYPE_GET_TELEMETRY_DATA;
    req_data[1] = ~(TELEM_PERM_BASE);  // NEW: inverse permissions mask (ie. we only want BASE telemetry)
    memset(&req_data[2], 0, 3);  // reserved
    getRNG()->random(&req_data[5], 4);   // random blob to help make packet-hash unique
    auto save = recipient->out_path_len;    // temporarily force sendRequest() to flood
    recipient->out_path_len = OUT_PATH_UNKNOWN;
    int result = sendRequest(*recipient, req_data, sizeof(req_data), tag, est_timeout);
    recipient->out_path_len = save;
    if (result == MSG_SEND_FAILED) {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    } else {
      clearPendingReqs();
      pending_discovery = tag; // match this in onContactResponse()
      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
      memcpy(&out_frame[2], &tag, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    }
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
  }
  return true;
}

bool MyMesh::cmdSendTelemetryReq(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  r.skip(3);  // reserved
  if (r.has(PUB_KEY_SIZE)) {  // can deprecate, in favour of CMD_SEND_BINARY_REQ
    uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
    ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (recipient) {
      uint32_t tag, est_timeout;
      int result = sendRequest(*recipient, REQ_TYPE_GET_TELEMETRY_DATA, tag, est_timeout);
      if (result == MSG_SEND_FAILED) {
        writeErrFrame(ERR_CODE_TABLE_FULL);
      } else {
        clearPendingReqs();
        pending_telemetry = tag; // match this in onContactResponse()
        out_frame[0] = RESP_CODE_SENT;
        out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
        memcpy(&out_frame[2], &tag, 4);
        memcpy(&out_frame[6], &est_timeout, 4);
        _serial->writeFrame(out_frame, 10);
      }
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
    }
    return true;
  }
  if (r.remaining() == 0) {  // 'self' telemetry request
    telemetry.reset();
    telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
    float temperature = board.getMCUTemperature();
    if(!isnan(temperature)) { // Supported boards with built-in temperature sensor. ESP32-C3 may return NAN
      telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature); // Built-in MCU Temperature
    }

    // query other sensors -- target specific
    sensors.querySensors(0xFF, telemetry);

    int i = 0;
    out_frame[i++] = PUSH_CODE_TELEMETRY_RESPONSE;
    out_frame[i++] = 0; // reserved
    memcpy(&out_frame[i], self_id.pub_key, 6);
    i += 6; // pub_key_prefix
    uint8_t tlen = telemetry.getSize();
    memcpy(&out_frame[i], telemetry.getBuffer(), tlen);
    i += tlen;
    _serial->writeFrame(out_frame, i);
    return true;
  }
  return false;
}

bool MyMesh::cmdSendBinaryReq(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  ContactInfo *recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (recipient) {
    size_t req_len = r.remaining();
    uint8_t *req_data = r.bytes(req_len);
    uint32_t tag, est_timeout;
    int result = sendRequest(*recipient, req_data, req_len, tag, est_timeout);
    if (result == MSG_SEND_FAILED) {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    } else {
      clearPendingReqs();
      pending_req = tag; // match this in onContactResponse()
      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = (result == MSG_SEND_SENT_FLOOD) ? 1 : 0;
      memcpy(&out_frame[2], &tag, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    }
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND); // contact not found
  }
  return true;
}

bool MyMesh::cmdHasConnection(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  if (hasConnectionTo(pub_key)) {
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND);
  }
  return true;
}

bool MyMesh::cmdLogout(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  stopConnection(pub_key);
  writeOKFrame();
  return true;
}

bool MyMesh::cmdGetChannel(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t channel_idx = r.u8();
  ChannelDetails channel;
  if (getChannel(channel_idx, channel)) {
    int i = 0;
    out_frame[i++] = RESP_CODE_CHANNEL_INFO;
    out_frame[i++] = channel_idx;
    strcpy((char *)&out_frame[i], channel.name);
    i += 32;
    memcpy(&out_frame[i], channel.channel.secret, 16);
    i += 16; // NOTE: only 128-bit supported
    _serial->writeFrame(out_frame, i);
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND);
  }
  return true;
}

bool MyMesh::cmdSetChannel(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (r.has(1 + 32 + 32)) {
    writeErrFrame(ERR_CODE_UNSUPPORTED_CMD); // not supported (yet)
    return true;
  }
  if (r.has(1 + 32 + 16)) {
    uint8_t channel_idx = r.u8();
    ChannelDetails channel;
    StrHelper::strncpy(channel.name, (char *)r.bytes(32), 32);
    memset(channel.channel.secret, 0, sizeof(channel.channel.secret));
    memcpy(channel.channel.secret, r.bytes(16), 16); // NOTE: only 128-bit supported
    if (setChannel(channel_idx, channel)) {
      saveChannels();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_NOT_FOUND); // bad channel_idx
    }
    return true;
  }
  return false;
}

bool MyMesh::cmdSignStart(size_t) {
  out_frame[0] = RESP_CODE_SIGN_START;
  out_frame[1] = 0; // reserved
  uint32_t len = MAX_SIGN_DATA_LEN;
  memcpy(&out_frame[2], &len, 4);
  _serial->writeFrame(out_frame, 6);

  if (sign_data) {
    free(sign_data);
  }
  sign_data = (uint8_t *)malloc(MAX_SIGN_DATA_LEN);
  sign_data_len = 0;
  return true;
}

bool MyMesh::cmdSignData(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  size_t n = r.remaining();
  if (sign_data == NULL || sign_data_len + n > MAX_SIGN_DATA_LEN) {
    writeErrFrame(sign_data == NULL ? ERR_CODE_BAD_STATE : ERR_CODE_TABLE_FULL); // error: too long
  } else {
    memcpy(&sign_data[sign_data_len], r.bytes(n), n);
    sign_data_len += n;
    writeOKFrame();
  }
  return true;
}

bool MyMesh::cmdSignFinish(size_t) {
  if (sign_data) {
    self_id.sign(&out_frame[1], sign_data, sign_data_len);

    free(sign_data); // don't need sign_data now
    sign_data = NULL;

    out_frame[0] = RESP_CODE_SIGNATURE;
    _serial->writeFrame(out_frame, 1 + SIGNATURE_SIZE);
  } else {
    writeErrFrame(ERR_CODE_BAD_STATE);
  }
  return true;
}

bool MyMesh::cmdSendTracePath(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint32_t tag = r.u32();
  uint32_t auth = r.u32();
  uint8_t flags = r.u8();
  if (r.remaining() >= MAX_PACKET_PAYLOAD-5) return false;
  uint8_t path_len = r.remaining();
  uint8_t *path = r.bytes(path_len);
  uint8_t path_sz = flags & 0x03;  // NEW v1.11+
  if ((path_len >> path_sz) > MAX_PATH_SIZE || (path_len % (1 << path_sz)) != 0) { // make sure is multiple of path_sz
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  } else {
    auto pkt = createTrace(tag, auth, flags);
    if (pkt) {
      sendDirect(pkt, path, path_len);

      uint32_t t = _radio->getEstAirtimeFor(pkt->payload_len + pkt->path_len + 2);
      uint32_t est_timeout = calcDirectTimeoutMillisFor(t, path_len >> path_sz);

      out_frame[0] = RESP_CODE_SENT;
      out_frame[1] = 0;
      memcpy(&out_frame[2], &tag, 4);
      memcpy(&out_frame[6], &est_timeout, 4);
      _serial->writeFrame(out_frame, 10);
    } else {
      writeErrFrame(ERR_CODE_TABLE_FULL);
    }
  }
  return true;
}

bool MyMesh::cmdSetDevicePin(size_t len) {

  // get pin from command frame
  FrameReader r(cmd_frame, len, 1);
  uint32_t pin = r.u32();

  // ensure pin is zero, or a valid 6 digit pin
  if (pin == 0 || (pin >= 100000 && pin <= 999999)) {
    _prefs.ble_pin = pin;
    savePrefs();
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  }
  return true;
}

bool MyMesh::cmdGetCustomVars(size_t) {
  out_frame[0] = RESP_CODE_CUSTOM_VARS;
  char *dp = (char *)&out_frame[1];
  for (int i = 0; i < sensors.getNumSettings() && dp - (char *)&out_frame[1] < 140; i++) {
    if (i > 0) {
      *dp++ = ',';
    }
    strcpy(dp, sensors.getSettingName(i));
    dp = strchr(dp, 0);
    *dp++ = ':';
    strcpy(dp, sensors.getSettingValue(i));
    dp = strchr(dp, 0);
  }
  _serial->writeFrame(out_frame, dp - (char *)out_frame);
  return true;
}

bool MyMesh::cmdSetCustomVar(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  char *sp = r.restAsCString();
  char *np = strchr(sp, ':'); // look for separator char
  if (np) {
    *np++ = 0; // modify 'cmd_frame', replace ':' with null
    bool success = sensors.setSettingValue(sp, np);
    if (success) {
      #if ENV_INCLUDE_GPS == 1
      // Update node preferences for GPS settings
      if (strcmp(sp, "gps") == 0) {
        _prefs.gps_enabled = (np[0] == '1') ? 1 : 0;
        savePrefs();
      } else if (strcmp(sp, "gps_interval") == 0) {
        uint32_t interval_seconds = atoi(np);
        _prefs.gps_interval = constrain(interval_seconds, 0, 86400);
        savePrefs();
      }
      #endif
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG);
  }
  return true;
}

bool MyMesh::cmdGetAdvertPath(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  r.skip(1);  // FUTURE use: reserved
  uint8_t *pub_key = r.bytes(PUB_KEY_SIZE);
  AdvertPath* found = NULL;
  for (int i = 0; i < ADVERT_PATH_TABLE_SIZE; i++) {
    auto p = &advert_paths[i];
    if (memcmp(p->pubkey_prefix, pub_key, sizeof(p->pubkey_prefix)) == 0) {
      found = p;
      break;
    }
  }
  if (found) {
    int i = 0;
    out_frame[i++] = RESP_CODE_ADVERT_PATH;
    memcpy(&out_frame[i], &found->recv_timestamp, 4); i += 4;
    out_frame[i++] = found->path_len;
    i += mesh::Packet::writePath(&out_frame[i], found->path, found->path_len);
    _serial->writeFrame(out_frame, i);
  } else {
    writeErrFrame(ERR_CODE_NOT_FOUND);
  }
  return true;
}

bool MyMesh::cmdGetStats(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t stats_type = r.u8();
  if (stats_type == STATS_TYPE_CORE) {
    int i = 0;
    out_frame[i++] = RESP_CODE_STATS;
    out_frame[i++] = STATS_TYPE_CORE;
    uint16_t battery_mv = board.getBattMilliVolts();
    uint32_t uptime_secs = _ms->getMillis() / 1000;
    uint8_t queue_len = (uint8_t)_mgr->getOutboundTotal();
    memcpy(&out_frame[i], &battery_mv, 2); i += 2;
    memcpy(&out_frame[i], &uptime_secs, 4); i += 4;
    memcpy(&out_frame[i], &_err_flags, 2); i += 2;
    out_frame[i++] = queue_len;
    _serial->writeFrame(out_frame, i);
  } else if (stats_type == STATS_TYPE_RADIO) {
    int i = 0;
    out_frame[i++] = RESP_CODE_STATS;
    out_frame[i++] = STATS_TYPE_RADIO;
    int16_t noise_floor = (int16_t)_radio->getNoiseFloor();
    int8_t last_rssi = (int8_t)radio_driver.getLastRSSI();
    int8_t last_snr = (int8_t)(radio_driver.getLastSNR() * 4); // scaled by 4 for 0.25 dB precision
    uint32_t tx_air_secs = getTotalAirTime() / 1000;
    uint32_t rx_air_secs = getReceiveAirTime() / 1000;
    memcpy(&out_frame[i], &noise_floor, 2); i += 2;
    out_frame[i++] = last_rssi;
    out_frame[i++] = last_snr;
    memcpy(&out_frame[i], &tx_air_secs, 4); i += 4;
    memcpy(&out_frame[i], &rx_air_secs, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else if (stats_type == STATS_TYPE_PACKETS) {
    int i = 0;
    out_frame[i++] = RESP_CODE_STATS;
    out_frame[i++] = STATS_TYPE_PACKETS;
    uint32_t recv = radio_driver.getPacketsRecv();
    uint32_t sent = radio_driver.getPacketsSent();
    uint32_t n_sent_flood = getNumSentFlood();
    uint32_t n_sent_direct = getNumSentDirect();
    uint32_t n_recv_flood = getNumRecvFlood();
    uint32_t n_recv_direct = getNumRecvDirect();
    uint32_t n_recv_errors = radio_driver.getPacketsRecvErrors();
    memcpy(&out_frame[i], &recv, 4); i += 4;
    memcpy(&out_frame[i], &sent, 4); i += 4;
    memcpy(&out_frame[i], &n_sent_flood, 4); i += 4;
    memcpy(&out_frame[i], &n_sent_direct, 4); i += 4;
    memcpy(&out_frame[i], &n_recv_flood, 4); i += 4;
    memcpy(&out_frame[i], &n_recv_direct, 4); i += 4;
    memcpy(&out_frame[i], &n_recv_errors, 4); i += 4;
    _serial->writeFrame(out_frame, i);
  } else {
    writeErrFrame(ERR_CODE_ILLEGAL_ARG); // invalid stats sub-type
  }
  return true;
}

bool MyMesh::cmdFactoryReset(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (memcmp(r.bytes(5), "reset", 5) != 0) return false;
  if (_serial) {
    MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
    _serial->disable(); // Phone app disconnects before we can send OK frame so it's safe here
  }
  bool success = _store->formatFileSystem();
  if (success) {
    writeOKFrame();
    delay(1000);
    board.reboot();  // doesn't return
  } else {
    writeErrFrame(ERR_CODE_FILE_IO_ERROR);
  }
  return true;
}

bool MyMesh::cmdSetFloodScopeKey(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  uint8_t mode = r.u8();
  if (mode == 0) {
    if (r.has(16)) {
      memcpy(send_scope.key, r.bytes(sizeof(send_scope.key)), sizeof(send_scope.key));  // set scope override TransportKey
    } else {
      memset(send_scope.key, 0, sizeof(send_scope.key));  // reset scope override
    }
    send_unscoped = false;
    writeOKFrame();
    return true;
  }
  if (mode == 1) {  // ver 12+
    send_unscoped = true;
    writeOKFrame();
    return true;
  }
  return false;
}

bool MyMesh::cmdSetDefaultFloodScope(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  if (r.has(31 + 16)) {
    size_t avail = r.remaining();
    const char *name = (const char *) r.bytes(31);
    const uint8_t *key = r.bytes(16);
    int n = strnlen(name, avail);   // may run on into the key, never past the frame
    if (n > 0 && n < 31) {
      strcpy(_prefs.default_scope_name, name);
      memcpy(_prefs.default_scope_key, key, 16);
      savePrefs();
      writeOKFrame();
    } else {
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else {
    memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));  // set default scope to null
    memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
    savePrefs();
    writeOKFrame();
  }
  return true;
}

bool MyMesh::cmdGetDefaultFloodScope(size_t) {
  out_frame[0] = RESP_CODE_DEFAULT_FLOOD_SCOPE;
  if (strlen(_prefs.default_scope_name) > 0) {
    memcpy(&out_frame[1], _prefs.default_scope_name, 31);
    memcpy(&out_frame[1+31], _prefs.default_scope_key, 16);
    _serial->writeFrame(out_frame, 1+31+16);
  } else {
    _serial->writeFrame(out_frame, 1);   // no name or key means null
  }
  return true;
}

bool MyMesh::cmdSendControlData(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  size_t n = r.remaining();
  uint8_t *data = r.bytes(n);
  if ((data[0] & 0x80) == 0) return false;
  auto resp = createControlData(data, n);
  if (resp) {
    sendZeroHop(resp);
    writeOKFrame();
  } else {
    writeErrFrame(ERR_CODE_TABLE_FULL);
  }
  return true;
}

// autoadd_max_hops stays optional
bool MyMesh::cmdSetAutoaddConfig(size_t len) {
  FrameReader r(cmd_frame, len, 1);
  _prefs.autoadd_config = r.u8();
  if (r.has(1)) {
    uint8_t hops = r.u8();   // read once: min() may be a macro
    _prefs.autoadd_max_hops = min(hops, (uint8_t)64);
  }
  savePrefs();
  writeOKFrame();
  return true;
}

bool MyMesh::cmdGetAutoaddConfig(size_t) {
  int i = 0;
  out_frame[i++] = RESP_CODE_AUTOADD_CONFIG;
  out_frame[i++] = _prefs.autoadd_config;
  out_frame[i++] = _prefs.autoadd_max_hops;
  _serial->writeFrame(out_frame, i);
  return true;
}

bool MyMesh::cmdGetAllowedRepeatFreq(size_t) {
  int i = 0;
  out_frame[i++] = RESP_ALLOWED_REPEAT_FREQ;
  for (int k = 0; k < sizeof(repeat_freq_ranges)/sizeof(repeat_freq_ranges[0]) && i + 8 < sizeof(out_frame); k++) {
    auto r = &repeat_freq_ranges[k];
    memcpy(&out_frame[i], &r->lower_freq, 4); i += 4;
    memcpy(&out_frame[i], &r->upper_freq, 4); i += 4;
  }
  _serial->writeFrame(out_frame, i);
  return true;
}

bool MyMesh::cmdSendRawPacket(size_t len) {
  auto pkt = obtainNewPacket();
  if (pkt) {
    FrameReader r(cmd_frame, len, 1);
    uint8_t priority = r.u8();
    int raw_len = r.remaining();
    if (tryParsePacket(pkt, r.bytes(raw_len), raw_len)) {
      sendPacket(pkt, priority, 0);
      writeOKFrame();
    } else {
      releasePacket(pkt);
      writeErrFrame(ERR_CODE_ILLEGAL_ARG);
    }
  } else {
    writeErrFrame(ERR_CODE_TABLE_FULL);
  }
  return true;
}

static bool save_filter(const ContactInfo& c) {
  return c.type != ADV_TYPE_NONE;   // don't save the transient/anon entries
}

void MyMesh::saveContacts() {
  _store->saveContacts(this, save_filter);
}

void MyMesh::enterCLIRescue() {
  _cli_rescue = true;
  cli_command[0] = 0;
  Serial.println("========= CLI Rescue =========");
}

bool MyMesh::handleCommand(const char* command, uint32_t sender_timestamp, char* reply) {
  while (*command == ' ') command++; // skip leading spaces

  if (strlen(command) > 4 && command[2] == '|') { // optional prefix (for companion radio CLI)
    memcpy(reply, command, 3);                    // reflect the prefix back
    reply += 3;
    *reply = 0;
    command += 3;
  }

  if (_prefs.getRadioPrefs()->handleCommand(command, sender_timestamp, reply)) {  // is radio CLI command?
    if (_prefs.getRadioPrefs()->isDirty()) { savePrefs(); }
    return true;
  }

  // hook for variant-specific CLI processing
  if (board.handleCommand(command, sender_timestamp, reply)) {
    if (_prefs.isDirty()) { savePrefs(); }
    return true;
  }

  if (strcmp(command, "reboot") == 0) {
    board.reboot(); // doesn't return
    return true;
  }

  if (strcmp(command, "poweroff") == 0 || strcmp(command, "shutdown") == 0) {
    board.powerOff(); // doesn't return
    return true;
  }

  if (memcmp(command, "set name ", 9) == 0) {
    if (AdvertDataParser::isValidName(&command[9])) {
      StrHelper::strncpy(_prefs.node_name, &command[9], sizeof(_prefs.node_name));
      savePrefs();
      strcpy(reply, "OK");
    } else {
      strcpy(reply, "Error, bad chars");
    }
    return true;
  }
  if (strcmp(command, "get name") == 0) {
    sprintf(reply, "> %s", _prefs.node_name);
    return true;
  }

  if (memcmp(command, "set pin ", 8) == 0) {
    _prefs.ble_pin = atoi(&command[8]);
    savePrefs();
    sprintf(reply, "> pin is now %06d", _prefs.ble_pin);
    return true;
  }

#ifdef ENABLE_WIFI_INTERFACE
  if (memcmp(command, "set wifi.ssid ", 14) == 0) {
    StrHelper::strncpy(_prefs.wifi_ssid, &command[14], sizeof(_prefs.wifi_ssid));
    savePrefs();
    sprintf(reply, "> wifi.ssid is now %s (set wifi.pwd too, then reboot)", _prefs.wifi_ssid);
    return true;
  }
  if (memcmp(command, "set wifi.pwd ", 13) == 0) {
    StrHelper::strncpy(_prefs.wifi_pwd, &command[13], sizeof(_prefs.wifi_pwd));
    savePrefs();
    strcpy(reply, "> wifi.pwd updated (reboot to apply)");
    return true;
  }
  if (strcmp(command, "get wifi.pwd") == 0) {
    sprintf(reply, "> %s", _prefs.wifi_pwd);
    return true;
  }
  if (strcmp(command, "set wifi.clear") == 0) {
    _prefs.wifi_ssid[0] = 0;
    _prefs.wifi_pwd[0] = 0;
    savePrefs();
    strcpy(reply, "> wifi config cleared (reboot to apply)");
    return true;
  }
  if (strcmp(command, "get wifi.ssid") == 0) {
    sprintf(reply, "> %s", _prefs.getWifiSSID()[0] ? _prefs.getWifiSSID() : "(not set)");
    return true;
  }
  if (memcmp(command, "set wifi.enabled ", 17) == 0) {
    _prefs.wifi_enabled = atoi(&command[17]) ? 1 : 0;
    savePrefs();
    sprintf(reply, "> wifi.enabled is now %d (reboot to apply)", _prefs.wifi_enabled);
    return true;
  }
  if (strcmp(command, "get wifi.enabled") == 0) {
    sprintf(reply, "> %d", _prefs.wifi_enabled);
    return true;
  }
  if (strcmp(command, "get wifi.status") == 0) {
    strcpy(reply, WiFi.status() == WL_CONNECTED ? "> connected" : "> disconnected");
    return true;
  }
  if (strcmp(command, "get wifi.ip") == 0) {
    if (WiFi.status() == WL_CONNECTED) {
      sprintf(reply, "> %s", WiFi.localIP().toString().c_str());
    } else {
      strcpy(reply, "> (not connected)");
    }
    return true;
  }
#endif

  if (strcmp(command, "board") == 0) {
    strcpy(reply, board.getManufacturerName());
    return true;
  }

  if (strcmp(command, "ver") == 0) {
    sprintf(reply, "%s (Build: %s)", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
    return true;
  }

  if (strcmp(command, "get tz.offset") == 0) {
    sprintf(reply, "> %.1f", _prefs.tz_offset);
    return true;
  }
  if (memcmp(command, "set tz.offset ", 14) == 0) {
    float tz = atof(&command[14]);
    if (tz < -12 || tz > 14) {
      strcpy(reply, "Error, must be from -12 to +14");
    } else {
      _prefs.tz_offset = tz;
      savePrefs();
      strcpy(reply, "OK");
    }
    return true;
  }

  return false;  // not handled
}

void MyMesh::checkCLIRescueCmd() {
  int len = strlen(cli_command);
  // `cli_command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(cli_command) and the loop below would
  // then index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(cli_command)) {
    cli_command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(cli_command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      cli_command[len++] = c;
      cli_command[len] = 0;
    }
    Serial.print(c);  // echo
  }
  if (len == sizeof(cli_command)-1) {  // buffer full: treat as a completed line
    cli_command[sizeof(cli_command)-2] = '\r';  // place end-of-line marker inside the buffer
    cli_command[sizeof(cli_command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && cli_command[len - 1] == '\r') {  // received complete line
    cli_command[len - 1] = 0;  // replace newline with C string null terminator

    reply_buf[0] = 0;
    if (handleCommand(cli_command, 0, reply_buf)) {
      // command was handled, print reply output
      Serial.print("  "); Serial.print(reply_buf); Serial.println();
    } else if (strcmp(cli_command, "rebuild") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        _store->saveMainIdentity(self_id);
        savePrefs();
        saveContacts();
        saveChannels();
        Serial.println("  > erase and rebuild done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (strcmp(cli_command, "erase") == 0) {
      bool success = _store->formatFileSystem();
      if (success) {
        Serial.println("  > erase done");
      } else {
        Serial.println("  Error: erase failed");
      }
    } else if (memcmp(cli_command, "ls", 2) == 0) {

      // get path from command e.g: "ls /adafruit"
      const char *path = &cli_command[3];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }
      Serial.printf("Listing files in %s\n", path);

      // log each file and directory
      File root = _store->openRead(path);
      if (is_fs2 == false) {
        if (root) {
          File file = root.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  UserData%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] UserData%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root.openNextFile();
          }
          root.close();
        }
      }

      if (is_fs2 == true || strlen(path) == 0 || strcmp(path, "/") == 0) {
        if (_store->getSecondaryFS() != nullptr) {
          File root2 = _store->openRead(_store->getSecondaryFS(), path);
          File file = root2.openNextFile();
          while (file) {
            if (file.isDirectory()) {
              Serial.printf("[dir]  ExtraFS%s/%s\n", path, file.name());
            } else {
              Serial.printf("[file] ExtraFS%s/%s (%d bytes)\n", path, file.name(), file.size());
            }
            // move to next file
            file = root2.openNextFile();
          }
          root2.close();
        }
      }
    } else if (memcmp(cli_command, "cat", 3) == 0) {

      // get path from command e.g: "cat /contacts3"
      const char *path = &cli_command[4];

      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      } else {
        Serial.println("Invalid path provided, must start with UserData/ or ExtraFS/");
        cli_command[0] = 0;
        return;
      }

      // log file content as hex
      File file = _store->openRead(path);
      if (is_fs2 == true) {
        file = _store->openRead(_store->getSecondaryFS(), path);
      }
      if(file){

        // get file content
        int file_size = file.available();
        uint8_t buffer[file_size];
        file.read(buffer, file_size);

        // print hex
        mesh::Utils::printHex(Serial, buffer, file_size);
        Serial.print("\n");

        file.close();

      }

    } else if (memcmp(cli_command, "rm ", 3) == 0) {
      // get path from command e.g: "rm /adv_blobs"
      const char *path = &cli_command[3];
      MESH_DEBUG_PRINTLN("Removing file: %s", path);
      // ensure path is not empty, or root dir
      if(!path || strlen(path) == 0 || strcmp(path, "/") == 0){
        Serial.println("Invalid path provided");
      } else {
      bool is_fs2 = false;
      if (memcmp(path, "UserData/", 9) == 0) {
        path += 8; // skip "UserData"
      } else if (memcmp(path, "ExtraFS/", 8) == 0) {
        path += 7; // skip "ExtraFS"
        is_fs2 = true;
      }

        // remove file
        bool removed;
        if (is_fs2) {
          MESH_DEBUG_PRINTLN("Removing file from ExtraFS: %s", path);
          removed = _store->removeFile(_store->getSecondaryFS(), path);
        } else {
          MESH_DEBUG_PRINTLN("Removing file from UserData: %s", path);
          removed = _store->removeFile(path);
        }
        if(removed){
          Serial.println("File removed");
        } else {
          Serial.println("Failed to remove file");
        }

      }

    } else if (strcmp(cli_command, "reboot") == 0) {
      board.reboot();  // doesn't return
    } else {
      Serial.println("  Error: unknown command");
    }

    cli_command[0] = 0;  // reset command buffer
  }
}

void MyMesh::checkSerialInterface() {
  size_t len = _serial->checkRecvFrame(cmd_frame);
  if (len > 0) {
    handleCmdFrame(len);
  } else if (_iter_started              // check if our ContactsIterator is 'running'
             && !_serial->isWriteBusy() // don't spam the Serial Interface too quickly!
  ) {
    ContactInfo contact;
    if (_iter.hasNext(this, contact)) {
      if (contact.lastmod > _iter_filter_since) { // apply the 'since' filter
        writeContactRespFrame(RESP_CODE_CONTACT, contact);
        if (contact.lastmod > _most_recent_lastmod) {
          _most_recent_lastmod = contact.lastmod; // save for the RESP_CODE_END_OF_CONTACTS frame
        }
      }
    } else { // EOF
      out_frame[0] = RESP_CODE_END_OF_CONTACTS;
      memcpy(&out_frame[1], &_most_recent_lastmod,
             4); // include the most recent lastmod, so app can update their 'since'
      _serial->writeFrame(out_frame, 5);
      _iter_started = false;
    }
  //} else if (!_serial->isWriteBusy()) {
  //  checkConnections();    // TODO - deprecate the 'Connections' stuff
  }
}

void MyMesh::loop() {
  BaseChatMesh::loop();

  if (_cli_rescue) {
    checkCLIRescueCmd();
  } else {
    checkSerialInterface();
#if defined(ENABLE_WIFI_INTERFACE) && defined(RP2040_PLATFORM) && !defined(ENABLE_USB_INTERFACE)
    // RP2040 WiFi builds are headless and have no way into the rescue CLI (that needs a
    // display + long-press), so serve config commands on the otherwise unused USB serial
    checkCLIRescueCmd();
#endif
  }

  // is there are pending dirty contacts write needed?
  if (dirty_contacts_expiry && millisHasNowPassed(dirty_contacts_expiry)) {
    saveContacts();
    dirty_contacts_expiry = 0;
  }
}

bool MyMesh::advert() {
  mesh::Packet* pkt;
  if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
    pkt = createSelfAdvert(_prefs.node_name);
  } else {
    pkt = createSelfAdvert(_prefs.node_name, sensors.node_lat, sensors.node_lon);
  }
  if (pkt) {
    sendZeroHop(pkt);
    return true;
  } else {
    return false;
  }
}

// To check if there is pending work
bool MyMesh::hasPendingWork() const {
  return _mgr->getOutboundTotal() > 0 || dirty_contacts_expiry != 0;
}
