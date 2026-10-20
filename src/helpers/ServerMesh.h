#pragma once

#include <Mesh.h>

#include <helpers/ClientACL.h>
#include <helpers/CommonCLI.h>
#include <helpers/RegionMap.h>
#include <helpers/RoutingPolicy.h>
#include <helpers/TransportKeyStore.h>

/**
 * \brief  The underpinnings every server role shares: the repeater, the room server
 *         and the sensor.
 *
 * The three roles grew as separate sketches and ended up carrying the same code
 * three times. What lives here was identical in all three, or identical apart from
 * formatting:
 *
 *   - the Region a packet arrived on, and the scope a flooded reply goes out with
 *   - loading, saving and applying the Region table
 *   - the advertisement schedule, with the advert itself left to the role
 *   - the temporary radio parameters a 'set radio ...' command installs
 *   - the identity file, the file system erase, the TX power and the stats replies
 *   - the client ACL and the peer lookup a packet's sender hash goes through
 *   - the boot sequence: begin() loads what the node stored, then gives the radio and
 *     the board the settings it holds, always in the same order
 *
 * A role keeps whatever is genuinely its own: what it announces, how it answers a
 * request, and in the repeater's case its bridge and loop detection.
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

  ClientACL acl;                  // the clients this node knows and what they may do
  NodePrefs _prefs;               // this node's persisted settings

  // Filled by searchPeersByHash(), read by getPeerSharedSecret() right after. Sized
  // for the whole ACL, which is what ClientACL can hold.
  int matching_peer_indexes[MAX_CLIENTS];
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
   * \brief  The hooks begin() calls into the role. A role owns its CommonCLI, so it
   *         says where the prefs are loaded from; the other two are for the one role
   *         that has more to do at those points, and do nothing by default.
   */
  virtual void loadPrefs() = 0;

  /**
   * \brief  Bring prefs that an older firmware stored up to NODE_PREFS_VERSION.
   *         Called by begin() right after loadPrefs(), and only when the stored
   *         version is older; begin() then records the current version.
   * \param  from  the stored version, 0 for a file written before versions existed
   * \returns true if a value was changed, so begin() saves the result at once and
   *          the upgrade does not run again on the next boot
   */
  virtual bool upgradePrefs(uint8_t from) { return false; }

  /**
   * \brief  Start the role's bridge, if it has one and it is enabled. Called after
   *         everything is loaded and before the radio is configured.
   */
  virtual void beginBridge() { }

  /**
   * \brief  Called once the radio has been given the node's parameters, TX power
   *         and boosted gain mode.
   */
  virtual void onRadioConfigured() { }

#if ENV_INCLUDE_GPS == 1
  /**
   * \brief  Push the stored GPS on/off setting to the sensor manager.
   */
  void applyGpsPrefs();
#endif

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
  /**
   * \brief  The checks every server role makes before passing a packet on:
   *         forwarding switched off, and the flood hop limits (flood.max,
   *         flood.max.unscoped, flood.max.advert). A role with checks of its own
   *         overrides this and calls it first.
   */
  bool allowPacketForward(const mesh::Packet* packet) override;
  int searchPeersByHash(const uint8_t* hash) override;
  void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;

  // CommonCLICallbacks
  void startRegionsLoad() override;
  bool saveRegions() override;
  void onDefaultRegionChanged(const RegionEntry* r) override;

public:
  /**
   * \brief  Bring the node up: start the dispatcher, load the stored prefs, ACL and
   *         Regions, establish the default scope, then configure the radio and the
   *         board from the prefs and start the advert timers.
   *
   * The order is part of the behaviour and is pinned by test/mocks/BootSequenceChecks.h.
   * In particular the dispatcher starts first, so it sizes its duty cycle budget from
   * the constructor's airtime factor and not the stored one.
   */
  void begin(FILESYSTEM* fs);

  // These were public on all three roles and stay public, so nothing outside the
  // roles changes. begin() and sendSelfAdvertisement() need it: each role's main.cpp
  // calls them once after boot. The rest are reached through the
  // CommonCLICallbacks pointer, where public access comes from that interface.
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
