#pragma once

// What a server role's begin() does, in the order it does it. Included by the suite
// of each role (repeater, room server, sensor) after it has named the concrete role
// class as `BootRole`, so the same expectations run against all three. That is the
// point of the boot sequence step in docs/refactoring/0007-boot-sequence.md: the
// three begin() bodies were meant to be one body, and one set of checks says whether
// they still are.
//
// Everything is observed from outside: the file system lookups, the calls on the
// radio, the board and the sensor manager (all recorded in g_events, see EventLog.h),
// and the role's state as the base class holds it. A test builds a node, stores some
// state, then "reboots" by building a second node on the same file system, which is
// what the firmware does between runs.
//
// Compile-time options the sequence depends on are covered where they are compiled
// in: DEFAULT_FLOOD_SCOPE_NAME, ENV_INCLUDE_GPS and, for the repeater, WITH_BRIDGE
// and MESH_DEBUG (the repeater's one log line). The default environments leave them
// out, as most firmware builds do; the "full" repeater environment turns all of them
// on. A suite defines BOOT_ROLE_LOGS_RX_GAIN before including this file if its role
// logs the gain mode it applied.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include <Arduino.h>
#include <target.h>
#include <EventLog.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>

namespace boot_checks {

constexpr uint32_t T0 = 1000;

// The role with the shared base's state opened up. Nothing else in the firmware
// reports any of it.
class BootProbe : public BootRole {
public:
  using BootRole::BootRole;

  FILESYSTEM* fileSystem() const { return _fs; }
  ClientACL& clients() { return acl; }
  RegionMap& regions() { return region_map; }
  NodePrefs& prefs() { return _prefs; }
  const TransportKey& defaultScope() const { return default_scope; }
  unsigned long nextLocalAdvert() const { return next_local_advert; }
  unsigned long nextFloodAdvert() const { return next_flood_advert; }
  const mesh::LocalIdentity& identity() const { return self_id; }

  // Dispatcher::begin() sizes the duty cycle budget from this, so the value it is
  // asked for first is the value the node booted its dispatcher with.
  float getAirtimeBudgetFactor() const override {
    float f = BootRole::getAirtimeBudgetFactor();
    if (!asked) { asked = true; first_airtime_factor = f; }
    return f;
  }
  mutable bool asked = false;
  mutable float first_airtime_factor = -1.0f;
};

class BootSequenceTest : public ::testing::Test {
protected:
  fs::FS fs;
  ArduinoMillis ms;
  StdRNG rng;
  SimpleMeshTables tables;

  void SetUp() override {
    g_mock_millis = T0;
    board.reset();
    radio_driver.reset();
    rtc_clock.now = 1760000000;
    fs.reset();
    rng.begin(1);
    g_events.clear();
  }

  std::unique_ptr<BootProbe> build() {
    return std::unique_ptr<BootProbe>(new BootProbe(board, radio_driver, ms, rng, rtc_clock, tables));
  }

  // Boot a node on the shared file system, let `store` change what it holds, then
  // write all of it to flash the way the CLI and the role do at run time.
  template <class Fn>
  void storeState(Fn store) {
    auto node = build();
    node->begin(&fs);
    store(*node);
    node->savePrefs();
    node->clients().save(&fs);
    node->regions().save(&fs);
  }

  // Boot the node under test and keep only the events begin() itself produced.
  std::unique_ptr<BootProbe> bootFresh() {
    auto node = build();
    g_events.clear();
    node->begin(&fs);
    return node;
  }

  mesh::Identity identityStarting(uint8_t first, uint8_t tag) {
    uint8_t pub[PUB_KEY_SIZE];
    memset(pub, tag, sizeof(pub));
    pub[0] = first;
    return mesh::Identity(pub);
  }

  // The first time the named file was looked at.
  static int firstLookupOf(const char* file) {
    return eventIndex(std::string("exists:") + file);
  }
};

// ---- the order -------------------------------------------------------------

TEST_F(BootSequenceTest, TheDispatcherStartsBeforeAnythingIsLoaded) {
  bootFresh();

  ASSERT_EQ(eventIndex("radio.begin"), 0) << "Mesh::begin() is the first thing begin() does";
  EXPECT_EQ(radio_driver.begin_calls, 1);
}

TEST_F(BootSequenceTest, TheDispatcherSizesItsBudgetFromTheConstructorDefaultsNotTheStoredPrefs) {
  // Recorded as observed behaviour, not endorsed: Mesh::begin() runs before the
  // stored prefs are loaded, so the duty cycle budget is computed from the airtime
  // factor the constructor set and the stored one only counts from then on. It is the
  // reason the order of the first two lines of begin() cannot be changed in a move.
  storeState([](BootProbe& n) { n.prefs().airtime_factor = 3.0f; });

  auto node = bootFresh();

  ASSERT_TRUE(node->asked);
  EXPECT_FLOAT_EQ(node->first_airtime_factor, 1.0f) << "the constructor default";
  EXPECT_FLOAT_EQ(node->prefs().airtime_factor, 3.0f) << "the stored value, loaded afterwards";
}

TEST_F(BootSequenceTest, PrefsAreLoadedBeforeTheAclAndTheAclBeforeTheRegions) {
  storeState([&](BootProbe& n) {
    n.clients().putClient(identityStarting(0xAA, 1), 1);
    n.regions().putRegion("#alpha", 0);
  });

  bootFresh();

  const int prefs_at = firstLookupOf("/prefs.json");
  const int acl_at = firstLookupOf("/s_contacts");
  const int regions_at = firstLookupOf("/regions2");
  ASSERT_GE(prefs_at, 0);
  ASSERT_GE(acl_at, 0);
  ASSERT_GE(regions_at, 0);
  EXPECT_LT(prefs_at, acl_at);
  EXPECT_LT(acl_at, regions_at);
  EXPECT_LT(eventIndex("radio.begin"), prefs_at);
}

TEST_F(BootSequenceTest, TheRadioIsConfiguredOnlyAfterEverythingIsLoaded) {
  bootFresh();

  const int regions_at = firstLookupOf("/regions2");
  const int params_at = eventIndex("radio.setParams");
  ASSERT_GE(regions_at, 0);
  ASSERT_GE(params_at, 0);
  EXPECT_LT(regions_at, params_at);
}

TEST_F(BootSequenceTest, TheRadioIsSetUpInTheOrderParamsPowerGainThenTheBoard) {
  bootFresh();

  const int params = eventIndex("radio.setParams");
  const int power = eventIndex("radio.setTxPower");
  const int gain = eventIndex("radio.setRxBoostedGainMode");
  const int attach = eventIndex("board.attachDynamicPrefs");
  const int adc = eventIndex("board.setAdcMultiplier");
  ASSERT_GE(params, 0);
  ASSERT_GE(power, 0);
  ASSERT_GE(gain, 0);
  ASSERT_GE(attach, 0);
  ASSERT_GE(adc, 0);
  EXPECT_LT(params, power);
  EXPECT_LT(power, gain);
  EXPECT_LT(gain, attach);
  EXPECT_LT(attach, adc);
}

TEST_F(BootSequenceTest, EachStepOfTheSetUpHappensExactlyOnce) {
  bootFresh();

  EXPECT_EQ(eventCount("radio.begin"), 1);
  EXPECT_EQ(eventCount("radio.setParams"), 1);
  EXPECT_EQ(eventCount("radio.setTxPower"), 1);
  EXPECT_EQ(eventCount("radio.setRxBoostedGainMode"), 1);
  EXPECT_EQ(eventCount("board.attachDynamicPrefs"), 1);
  EXPECT_EQ(eventCount("board.setAdcMultiplier"), 1);
}

// ---- what is loaded and what it is used for --------------------------------

TEST_F(BootSequenceTest, TheFileSystemIsTheOneBeginWasGiven) {
  auto node = bootFresh();

  EXPECT_EQ(node->fileSystem(), &fs);
}

TEST_F(BootSequenceTest, TheRoleCanSaveItsPrefsRightAfterBegin) {
  // Saving goes through the file system pointer begin() stored, so this fails if
  // that assignment is lost or comes too late.
  auto node = bootFresh();
  ASSERT_FALSE(fs.exists("/prefs.json"));

  node->savePrefs();

  EXPECT_TRUE(fs.exists("/prefs.json"));
}

TEST_F(BootSequenceTest, BeginWritesNothingOnAnEmptyFileSystem) {
  bootFresh();

  EXPECT_TRUE(fs.files.empty()) << "booting is read only, nothing is created";
}

TEST_F(BootSequenceTest, StoredRadioSettingsReachTheRadio) {
  storeState([](BootProbe& n) {
    n.prefs().freq = 869.525f;
    n.prefs().bw = 250.0f;
    n.prefs().sf = 9;
    n.prefs().cr = 6;
    n.prefs().tx_power_dbm = 11;
    n.prefs().rx_boosted_gain = 0;   // the opposite of the default
  });
  radio_driver.reset();

  bootFresh();

  EXPECT_FLOAT_EQ(radio_driver.freq, 869.525f);
  EXPECT_FLOAT_EQ(radio_driver.bw, 250.0f);
  EXPECT_EQ(radio_driver.sf, 9);
  EXPECT_EQ(radio_driver.cr, 6);
  EXPECT_EQ(radio_driver.tx_power, 11);
  EXPECT_FALSE(radio_driver.rx_boosted);
}

TEST_F(BootSequenceTest, StoredBoostedGainIsAppliedWhenItIsOn) {
  storeState([](BootProbe& n) { n.prefs().rx_boosted_gain = 1; });
  radio_driver.reset();

  bootFresh();

  EXPECT_TRUE(radio_driver.rx_boosted);
  EXPECT_EQ(radio_driver.rx_boost_calls, 1);
}

TEST_F(BootSequenceTest, TheStoredAdcMultiplierReachesTheBoard) {
  storeState([](BootProbe& n) { n.prefs().adc_multiplier = 1.5f; });
  board.reset();

  bootFresh();

  EXPECT_FLOAT_EQ(board.adc_multiplier, 1.5f);
}

TEST_F(BootSequenceTest, AdvertTimersCountFromTheStoredIntervals) {
  storeState([](BootProbe& n) {
    n.prefs().advert_interval = 7;          // units of two minutes
    n.prefs().flood_advert_interval = 5;    // hours
  });

  auto node = bootFresh();

  EXPECT_EQ(node->nextLocalAdvert(), T0 + 7UL * 2 * 60 * 1000);
  EXPECT_EQ(node->nextFloodAdvert(), T0 + 5UL * 60 * 60 * 1000);
}

TEST_F(BootSequenceTest, AdvertTimersOfAFreshNodeFollowWhateverTheConstructorSet) {
  // The roles do not share their advert defaults (the sensor starts without a flood
  // advert), so this does not name them: it checks that begin() schedules from the
  // prefs the node actually holds, which on a fresh file system are the constructor's.
  auto node = bootFresh();

  const NodePrefs& p = node->prefs();
  const unsigned long local =
      p.advert_interval > 0 ? T0 + (unsigned long)p.advert_interval * 2 * 60 * 1000 : 0;
  const unsigned long flood =
      p.flood_advert_interval > 0 ? T0 + (unsigned long)p.flood_advert_interval * 60 * 60 * 1000 : 0;
  EXPECT_EQ(node->nextLocalAdvert(), local);
  EXPECT_EQ(node->nextFloodAdvert(), flood);
}

TEST_F(BootSequenceTest, ZeroIntervalsLeaveBothTimersOff) {
  storeState([](BootProbe& n) {
    n.prefs().advert_interval = 0;
    n.prefs().flood_advert_interval = 0;
  });

  auto node = bootFresh();

  EXPECT_EQ(node->nextLocalAdvert(), 0UL);
  EXPECT_EQ(node->nextFloodAdvert(), 0UL);
}

TEST_F(BootSequenceTest, TheStoredAclIsLoaded) {
  storeState([&](BootProbe& n) {
    ClientInfo* c = n.clients().putClient(identityStarting(0xAA, 1), 3);
    ASSERT_NE(c, nullptr);
    memset(c->shared_secret, 7, PUB_KEY_SIZE);
  });

  auto node = bootFresh();

  ASSERT_EQ(node->clients().getNumClients(), 1);
  const ClientInfo* c = node->clients().getClientByIdx(0);
  EXPECT_EQ(c->id.pub_key[0], 0xAA);
  EXPECT_EQ(c->permissions, 3);

  // The secret is recalculated from this node's own identity, not read back.
  uint8_t expected[PUB_KEY_SIZE];
  node->identity().calcSharedSecret(expected, c->id.pub_key);
  EXPECT_EQ(memcmp(c->shared_secret, expected, PUB_KEY_SIZE), 0);
}

TEST_F(BootSequenceTest, TheStoredRegionsAreLoaded) {
  storeState([](BootProbe& n) {
    n.regions().putRegion("#alpha", 0);
    n.regions().putRegion("#beta", 0);
  });

  auto node = bootFresh();

  EXPECT_NE(node->regions().findByName("#alpha"), nullptr);
  EXPECT_NE(node->regions().findByName("#beta"), nullptr);
}

// ---- the default scope -----------------------------------------------------

TEST_F(BootSequenceTest, AStoredDefaultRegionBecomesTheDefaultScope) {
  storeState([](BootProbe& n) {
    RegionEntry* r = n.regions().putRegion("#probe", 0);
    ASSERT_NE(r, nullptr);
    n.regions().setDefaultRegion(r);
  });

  auto node = bootFresh();

  RegionEntry* r = node->regions().findByName("#probe");
  ASSERT_NE(r, nullptr);
  ASSERT_EQ(node->regions().getDefaultRegion(), r);
  TransportKey expected;
  ASSERT_EQ(node->regions().getTransportKeysFor(*r, &expected, 1), 1);
  EXPECT_FALSE(node->defaultScope().isNull());
  EXPECT_EQ(memcmp(node->defaultScope().key, expected.key, sizeof(expected.key)), 0);
}

#ifndef DEFAULT_FLOOD_SCOPE_NAME

TEST_F(BootSequenceTest, WithoutAStoredDefaultRegionThereIsNoDefaultScope) {
  auto node = bootFresh();

  EXPECT_TRUE(node->defaultScope().isNull());
  EXPECT_EQ(node->regions().getDefaultRegion(), nullptr);
  EXPECT_EQ(node->regions().getCount(), 0);
}

#else   // a build that names a default scope

TEST_F(BootSequenceTest, TheConfiguredDefaultScopeIsCreatedWhenNoneIsStored) {
  auto node = bootFresh();

  RegionEntry* r = node->regions().findByName(DEFAULT_FLOOD_SCOPE_NAME);
  ASSERT_NE(r, nullptr) << "created on first boot";
  EXPECT_EQ(r->flags, 0) << "allow flood";
  EXPECT_EQ(r->parent, 0);
  EXPECT_EQ(node->regions().getDefaultRegion(), r);

  TransportKey expected;
  ASSERT_EQ(node->regions().getTransportKeysFor(*r, &expected, 1), 1);
  EXPECT_FALSE(node->defaultScope().isNull());
  EXPECT_EQ(memcmp(node->defaultScope().key, expected.key, sizeof(expected.key)), 0);
  EXPECT_EQ(node->regions().getCount(), 1);
}

TEST_F(BootSequenceTest, TheConfiguredDefaultScopeIsNotWrittenToFlashByBooting) {
  bootFresh();

  EXPECT_FALSE(fs.exists("/regions2"));
}

TEST_F(BootSequenceTest, AStoredDefaultRegionWinsOverTheConfiguredName) {
  storeState([](BootProbe& n) {
    // The first boot on an empty file system already created the configured Region,
    // so take it out again: what is stored is #probe, and #probe is the default.
    RegionEntry* configured = n.regions().findByName(DEFAULT_FLOOD_SCOPE_NAME);
    ASSERT_NE(configured, nullptr);
    ASSERT_TRUE(n.regions().removeRegion(*configured));
    RegionEntry* r = n.regions().putRegion("#probe", 0);
    ASSERT_NE(r, nullptr);
    n.regions().setDefaultRegion(r);
  });

  auto node = bootFresh();

  EXPECT_EQ(node->regions().findByName(DEFAULT_FLOOD_SCOPE_NAME), nullptr) << "not created";
  EXPECT_EQ(node->regions().getDefaultRegion(), node->regions().findByName("#probe"));
  EXPECT_EQ(node->regions().getCount(), 1);
}

TEST_F(BootSequenceTest, AStoredRegionOfTheConfiguredNameIsReusedWithItsFlags) {
  storeState([](BootProbe& n) {
    // Stored as a Region that denies flood and is not the default.
    RegionEntry* r = n.regions().findByName(DEFAULT_FLOOD_SCOPE_NAME);
    ASSERT_NE(r, nullptr);
    r->flags = REGION_DENY_FLOOD;
    n.regions().setDefaultRegion(nullptr);
  });

  auto node = bootFresh();

  RegionEntry* r = node->regions().findByName(DEFAULT_FLOOD_SCOPE_NAME);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(node->regions().getCount(), 1) << "reused, not duplicated";
  EXPECT_EQ(node->regions().getDefaultRegion(), r) << "and made the default again";
  EXPECT_TRUE(r->flags & REGION_DENY_FLOOD) << "only a region created at boot is opened up";
}

#endif  // DEFAULT_FLOOD_SCOPE_NAME

// ---- optional blocks -------------------------------------------------------

#if ENV_INCLUDE_GPS == 1

TEST_F(BootSequenceTest, TheStoredGpsSettingIsPushedToTheSensorManagerLast) {
  storeState([](BootProbe& n) { n.prefs().gps_enabled = 1; });

  bootFresh();

  ASSERT_EQ(eventCount("sensors.setSettingValue gps=1"), 1);
  EXPECT_EQ(eventCount("sensors.setSettingValue gps=0"), 0);
  EXPECT_EQ(eventIndex("sensors.setSettingValue gps=1"), (int)g_events.size() - 1)
      << "the last thing begin() does";
  EXPECT_GT(eventIndex("sensors.setSettingValue gps=1"), eventIndex("board.setAdcMultiplier"));
}

TEST_F(BootSequenceTest, AStoredGpsOffIsPushedAsOff) {
  storeState([](BootProbe& n) { n.prefs().gps_enabled = 0; });

  bootFresh();

  EXPECT_EQ(eventCount("sensors.setSettingValue gps=0"), 1);
  EXPECT_EQ(eventCount("sensors.setSettingValue gps=1"), 0);
}

#else

TEST_F(BootSequenceTest, WithoutGpsSupportNothingIsPushedToTheSensorManager) {
  bootFresh();

  for (const std::string& e : g_events) {
    EXPECT_NE(e.rfind("sensors.", 0), 0u) << e;
  }
}

#endif  // ENV_INCLUDE_GPS

#if defined(BOOT_ROLE_LOGS_RX_GAIN) && MESH_DEBUG

// Only the repeater says what it did to the radio, and only in a debug build. The
// line reports the mode the radio is in at that moment, so it also shows that the
// message comes after the gain was applied and not before.
TEST_F(BootSequenceTest, TheRepeaterLogsTheGainModeAfterApplyingIt) {
  storeState([](BootProbe& n) { n.prefs().rx_boosted_gain = 1; });
  radio_driver.reset();   // a radio that starts with the boost off
  Serial.reset();

  bootFresh();

  EXPECT_EQ(Serial.out.find("DEBUG: RX Boosted Gain Mode: Enabled\n"), 0u)
      << "the first thing the debug build prints, and it reports the applied mode";
  size_t lines = 0;
  for (size_t at = Serial.out.find("RX Boosted Gain Mode"); at != std::string::npos;
       at = Serial.out.find("RX Boosted Gain Mode", at + 1)) {
    lines++;
  }
  EXPECT_EQ(lines, 1u) << "and only once";
}

TEST_F(BootSequenceTest, TheRepeaterLogsADisabledGainModeAsDisabled) {
  storeState([](BootProbe& n) { n.prefs().rx_boosted_gain = 0; });
  radio_driver.reset();
  Serial.reset();

  bootFresh();

  EXPECT_NE(Serial.out.find("DEBUG: RX Boosted Gain Mode: Disabled\n"), std::string::npos);
}

#endif  // BOOT_ROLE_LOGS_RX_GAIN

#ifdef WITH_BRIDGE

TEST_F(BootSequenceTest, TheBridgeStartsAfterTheLoadingAndBeforeTheRadioIsConfigured) {
  bootFresh();

  const int started = eventIndex("bridge.begin");
  ASSERT_GE(started, 0);
  EXPECT_EQ(eventCount("bridge.begin"), 1);
  EXPECT_GT(started, firstLookupOf("/regions2"));
  EXPECT_LT(started, eventIndex("radio.setParams"));
}

TEST_F(BootSequenceTest, ADisabledBridgeIsNotStarted) {
  storeState([](BootProbe& n) { n.prefs().bridge_enabled = 0; });

  bootFresh();

  EXPECT_EQ(eventCount("bridge.begin"), 0);
  EXPECT_EQ(eventCount("radio.setParams"), 1) << "the rest of the sequence still runs";
}

#endif  // WITH_BRIDGE

}  // namespace boot_checks
