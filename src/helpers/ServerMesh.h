#pragma once

#include <Mesh.h>

#include <helpers/CommonCLI.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include <helpers/TransportKeyStore.h>

/**
 * \brief  The underpinnings every server role shares: the repeater, the room server
 *         and the sensor.
 *
 * The three roles grew as separate sketches and ended up carrying the same code
 * three times. What lives here is the part that was byte for byte identical in all
 * three: resolving the Region a packet arrived on, choosing the scope of a flooded
 * reply, and loading, saving and applying the Region table. A role keeps whatever is
 * genuinely its own.
 *
 * The members are protected rather than private because the role code reaches them
 * directly, as it did when they were its own fields.
 *
 * See docs/refactoring/0004-shared-server-base.md for the measurement and the order
 * of the steps.
 */
class ServerMesh : public mesh::Mesh, public CommonCLICallbacks {
protected:
  FILESYSTEM* _fs;

  // Declared before the Region maps, which are constructed with it.
  TransportKeyStore key_store;
  RegionMap region_map, temp_map;

  NodePrefs _prefs;               // this node's persisted settings
  unsigned long next_local_advert, next_flood_advert;

  // A 'set radio ...' command parks its values here until the reply has gone out,
  // and schedules the return to the node's own settings.
  unsigned long set_radio_at, revert_radio_at;
  float pending_freq;
  float pending_bw;
  uint8_t pending_sf;
  uint8_t pending_cr;

  RegionEntry* load_stack[8];
  RegionEntry* recv_pkt_region;   // the Region the packet being handled arrived on
  TransportKey default_scope;     // this node's own scope, for replies it cannot scope otherwise
  bool region_load_active;

  ServerMesh(mesh::Radio& radio, mesh::MillisecondClock& ms, mesh::RNG& rng, mesh::RTCClock& rtc,
             mesh::PacketManager& mgr, mesh::MeshTables& tables)
    : mesh::Mesh(radio, ms, rng, rtc, mgr, tables),
      region_map(key_store), temp_map(key_store)
  {
    _fs = NULL;
    next_local_advert = next_flood_advert = 0;
    set_radio_at = revert_radio_at = 0;
    recv_pkt_region = NULL;
    region_load_active = false;
    memset(default_scope.key, 0, sizeof(default_scope.key));
  }

  /**
   * \brief  Build this node's own advertisement.
   *
   * The only part of advertising that differs by role: each one announces its own
   * type (repeater, room server, sensor), so the packet itself stays with the role
   * while the schedule and the send paths live here.
   */
  virtual mesh::Packet* createSelfAdvert() = 0;

  /**
   * \brief  Send a flooded reply to the request being handled, scoped the way the
   *         request was, or with this node's own scope when that is unknowable.
   */
  void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);

  /**
   * \brief  Flood a packet inside a transport scope, or un-scoped if the scope is null.
   */
  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);

  /**
   * \brief  Hand the radio any pending temporary parameters, and take them back
   *         again once they have timed out. Call this from the role's loop().
   */
  void checkPendingRadioParams();

  // Mesh
  mesh::DispatcherAction onRecvPacket(mesh::Packet* pkt) override;

  // CommonCLICallbacks
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

public:
  // Public because each role's main.cpp advertises on its own after boot, and
  // because CommonCLI reaches them through its CommonCLICallbacks pointer. They
  // were public in all three roles before they moved here.
  void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;
  void setTxPower(int8_t power_dbm) override;
  bool formatFileSystem() override;
  void saveIdentity(const mesh::LocalIdentity& new_id) override;
  void clearStats() override;
  void formatStatsReply(char* reply) override;
  void formatRadioStatsReply(char* reply) override;
  void formatPacketStatsReply(char* reply) override;
  void sendSelfAdvertisement(int delay_millis, bool flood) override;
  void updateAdvertTimer() override;
  void updateFloodAdvertTimer() override;
};
