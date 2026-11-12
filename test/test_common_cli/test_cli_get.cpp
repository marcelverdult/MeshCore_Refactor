// CommonCLI::handleGetCmd(): every "get <name>" of the chain. The radio settings
// ("get radio", "get tx", "get rxdelay" and the rest) are answered earlier, by
// CommonRadioPrefs; only their place in the order is checked here.

#include <gtest/gtest.h>

#include "CliHarness.h"

static std::string upperHex(const uint8_t* b, size_t n) {
  static const char* digits = "0123456789ABCDEF";
  std::string s;
  for (size_t i = 0; i < n; i++) { s += digits[b[i] >> 4]; s += digits[b[i] & 15]; }
  return s;
}

TEST_F(CliTest, GetReadsWhatSetWrote) {
  EXPECT_EQ(run("get allow.read.only"), "> off");
  prefs.allow_read_only = 1;
  EXPECT_EQ(run("get allow.read.only"), "> on");
  EXPECT_EQ(run("get flood.advert.interval"), "> 12");
  EXPECT_EQ(run("get advert.interval"), "> 180");          // stored in units of 2 minutes
  EXPECT_EQ(run("get guest.password"), "> guestpw");
  EXPECT_EQ(run("get name"), "> Alpha");
  EXPECT_EQ(run("get repeat"), "> on");
  prefs.disable_fwd = 1;
  EXPECT_EQ(run("get repeat"), "> off");
  EXPECT_EQ(run("get flood.max"), "> 20");
  EXPECT_EQ(run("get flood.max.unscoped"), "> 21");
  EXPECT_EQ(run("get flood.max.advert"), "> 22");
  EXPECT_EQ(run("get role"), "> repeater");
  EXPECT_TRUE(takeCalls().empty());                        // nothing is saved by a read
}

TEST_F(CliTest, GetAdminPasswordDoesNotExist) {
  EXPECT_EQ(run("get password"), "??: password");
  EXPECT_EQ(run("get pass"), "??: pass");
}

TEST_F(CliTest, GetLatAndLonPrintTheFloatFormatOfTheFirmware) {
  EXPECT_EQ(run("get lat"), "> " + ftoa(51.5f));
  EXPECT_EQ(run("get lon"), "> " + ftoa(-0.25f));
  EXPECT_EQ(run("get lat"), "> 51.5");
  EXPECT_EQ(run("get lon"), "> -0.25");
  prefs.node_lat = 0;
  EXPECT_EQ(run("get lat"), "> 0.0");
}

TEST_F(CliTest, GetNamesMatchOnTheirPrefixPinnedAsIs) {
  // The names are compared with memcmp() over their own length only, so a longer word
  // that starts with one is the same command.
  EXPECT_EQ(run("get names"), "> Alpha");
  EXPECT_EQ(run("get latitude"), "> 51.5");
  EXPECT_EQ(run("get longitude"), "> -0.25");
  EXPECT_EQ(run("get lonely"), "> -0.25");
  EXPECT_EQ(run("get roles"), "> repeater");
  EXPECT_EQ(run("get repeating"), "> on");
  EXPECT_EQ(run("get guest.passwords"), "> guestpw");
  EXPECT_EQ(run("get allow.read.only please"), "> off");
}

TEST_F(CliTest, GetFloodMaxLongerNamesWinOverTheShorterOne) {
  EXPECT_EQ(run("get flood.max.advert"), "> 22");
  EXPECT_EQ(run("get flood.max.unscoped"), "> 21");
  EXPECT_EQ(run("get flood.max.other"), "> 20");     // matches the short name, pinned as is
  EXPECT_EQ(run("get flood.advert.interval"), "> 12");
}

TEST_F(CliTest, GetOwnerInfoShowsNewlinesAsBars) {
  EXPECT_EQ(run("get owner.info"), "> ");
  strcpy(prefs.owner_info, "Jo\nBerlin\njo@example.org");
  EXPECT_EQ(run("get owner.info"), "> Jo|Berlin|jo@example.org");
  memset(prefs.owner_info, 'o', 119);
  prefs.owner_info[119] = 0;
  EXPECT_EQ(run("get owner.info"), "> " + std::string(119, 'o'));
}

TEST_F(CliTest, GetLoopDetectNamesTheMode) {
  const char* names[] = {"> off", "> minimal", "> moderate", "> strict"};
  for (int m = 0; m < 4; m++) {
    prefs.loop_detect = m;
    EXPECT_EQ(run("get loop.detect"), names[m]) << m;
  }
  prefs.loop_detect = 9;                       // not a mode: reads as strict, pinned as is
  EXPECT_EQ(run("get loop.detect"), "> strict");
}

TEST_F(CliTest, GetPublicKeyShowsTheKeyInUpperCaseHex) {
  uint8_t prv[PRV_KEY_SIZE];
  for (int i = 0; i < PRV_KEY_SIZE; i++) prv[i] = (uint8_t)(0x20 + i);
  cb.self_id.readFrom(prv, PRV_KEY_SIZE);
  EXPECT_EQ(run("get public.key"), "> " + upperHex(cb.self_id.pub_key, PUB_KEY_SIZE));
  EXPECT_EQ(run("get public.key").size(), 2u + 64u);
}

TEST_F(CliTest, GetPrvKeyIsForTheSerialConsoleOnly) {
  uint8_t prv[PRV_KEY_SIZE];
  for (int i = 0; i < PRV_KEY_SIZE; i++) prv[i] = (uint8_t)(0x20 + i);
  cb.self_id.readFrom(prv, PRV_KEY_SIZE);
  std::string reply = run("get prv.key");
  EXPECT_EQ(reply, "> " + upperHex(prv, PRV_KEY_SIZE));
  EXPECT_EQ(reply.size(), 2u + 128u);
  EXPECT_EQ(run("get prv.key", 1760000001), "??: prv.key");   // over the air: not a known name
}

TEST_F(CliTest, GetBridgeTypeNamesTheBridgeOfTheBuild) {
#if defined(WITH_RS232_BRIDGE)
  EXPECT_EQ(run("get bridge.type"), "> rs232");
#elif defined(WITH_ESPNOW_BRIDGE)
  EXPECT_EQ(run("get bridge.type"), "> espnow");
#else
  EXPECT_EQ(run("get bridge.type"), "> none");
#endif
}

// ---- build dependent: bridge settings ---------------------------------------------------------------

#if defined(WITH_BRIDGE)

TEST_F(CliTest, GetBridgeSettings) {
  EXPECT_EQ(run("get bridge.enabled"), "> off");
  prefs.bridge_enabled = 1;
  EXPECT_EQ(run("get bridge.enabled"), "> on");
  prefs.bridge_delay = 750;
  EXPECT_EQ(run("get bridge.delay"), "> 750");
  prefs.bridge_pkt_src = 0;
  EXPECT_EQ(run("get bridge.source"), "> logTx");
  prefs.bridge_pkt_src = 1;
  EXPECT_EQ(run("get bridge.source"), "> logRx");
}

#else

TEST_F(CliTest, GetBridgeSettingsDoNotExistWithoutABridge) {
  EXPECT_EQ(run("get bridge.enabled"), "??: bridge.enabled");
  EXPECT_EQ(run("get bridge.delay"), "??: bridge.delay");
  EXPECT_EQ(run("get bridge.source"), "??: bridge.source");
  EXPECT_EQ(run("get bridge.baud"), "??: bridge.baud");
  EXPECT_EQ(run("get bridge.channel"), "??: bridge.channel");
  EXPECT_EQ(run("get bridge.secret"), "??: bridge.secret");
}

#endif

#if defined(WITH_RS232_BRIDGE)
TEST_F(CliTest, GetBridgeBaud) {
  prefs.bridge_baud = 57600;
  EXPECT_EQ(run("get bridge.baud"), "> 57600");
}
#elif defined(WITH_BRIDGE)
TEST_F(CliTest, GetBridgeBaudDoesNotExistWithoutAnRs232Bridge) {
  EXPECT_EQ(run("get bridge.baud"), "??: bridge.baud");
}
#endif

#if defined(WITH_ESPNOW_BRIDGE)
TEST_F(CliTest, GetBridgeChannelAndSecret) {
  prefs.bridge_channel = 6;
  strcpy(prefs.bridge_secret, "hunter2");
  EXPECT_EQ(run("get bridge.channel"), "> 6");
  EXPECT_EQ(run("get bridge.secret"), "> hunter2");      // the secret is readable, pinned as is
}
#elif defined(WITH_BRIDGE)
TEST_F(CliTest, GetBridgeChannelAndSecretDoNotExistWithoutAnEspNowBridge) {
  EXPECT_EQ(run("get bridge.channel"), "??: bridge.channel");
  EXPECT_EQ(run("get bridge.secret"), "??: bridge.secret");
}
#endif

// ---- board dependent ---------------------------------------------------------------------------------

TEST_F(CliTest, GetBootloaderVersionIsNrf52Only) {
#if defined(NRF52_PLATFORM)
  brd.bootloader_ok = true;
  brd.bootloader_ver = "0.9.2";
  EXPECT_EQ(run("get bootloader.ver"), "> 0.9.2");
  brd.bootloader_ok = false;
  EXPECT_EQ(run("get bootloader.ver"), "> unknown");
#else
  brd.bootloader_ok = true;                         // the board is not asked
  brd.bootloader_ver = "0.9.2";
  EXPECT_EQ(run("get bootloader.ver"), "Error: unsupported");
#endif
}

TEST_F(CliTest, GetAdcMultiplierReadsTheBoardNotThePrefs) {
  prefs.adc_multiplier = 3.0f;
  brd.adc = 0.0f;
  EXPECT_EQ(run("get adc.multiplier"), "Error: unsupported");
  brd.adc = 2.5f;
  EXPECT_EQ(run("get adc.multiplier"), "> 2.500");
  brd.adc = 1.0f / 3.0f;
  EXPECT_EQ(run("get adc.multiplier"), "> 0.333");
}

TEST_F(CliTest, GetPwrmgtSupportDependsOnTheBuild) {
#if defined(NRF52_POWER_MANAGEMENT)
  EXPECT_EQ(run("get pwrmgt.support"), "> supported");
#else
  EXPECT_EQ(run("get pwrmgt.support"), "> unsupported");
#endif
}

TEST_F(CliTest, GetPwrmgtSourceAndBootVoltageDependOnTheBuild) {
#if defined(NRF52_POWER_MANAGEMENT)
  brd.ext_powered = false;
  EXPECT_EQ(run("get pwrmgt.source"), "> battery");
  brd.ext_powered = true;
  EXPECT_EQ(run("get pwrmgt.source"), "> external");
  EXPECT_EQ(run("get pwrmgt.bootmv"), "> 3500 mV");
#else
  EXPECT_EQ(run("get pwrmgt.source"), "ERROR: Power management not supported");
  EXPECT_EQ(run("get pwrmgt.bootmv"), "ERROR: Power management not supported");
#endif
}

TEST_F(CliTest, GetPwrmgtBootreasonNamesBothReasonsInEveryBuild) {
  EXPECT_EQ(run("get pwrmgt.bootreason"), "> Reset: reset-3; Shutdown: shutdown-4");
  brd.reset_reason = 8;
  brd.shutdown_reason = 2;
  EXPECT_EQ(run("get pwrmgt.bootreason"), "> Reset: reset-8; Shutdown: shutdown-2");
}

TEST_F(CliTest, GetExtraSfListsUpToThreeWithoutThePromptPinnedAsIs) {
  EXPECT_EQ(run("get extra.sf"), "No extra SF configured");
  prefs.extra_sf[0] = 7;
  EXPECT_EQ(run("get extra.sf"), "7");
  prefs.extra_sf[1] = 9;
  prefs.extra_sf[2] = 11;
  EXPECT_EQ(run("get extra.sf"), "7,9,11");
  prefs.extra_sf[3] = 12;                           // a fourth is never shown
  EXPECT_EQ(run("get extra.sf"), "7,9,11");
}

TEST_F(CliTest, GetOfAnUnknownSettingEchoesItsText) {
  EXPECT_EQ(run("get nonsense"), "??: nonsense");
  EXPECT_EQ(run("get "), "??: ");
  EXPECT_EQ(run("get x y z"), "??: x y z");
}

TEST_F(CliTest, RadioSettingsKeepTheirOwnAnswersInTheGetChain) {
  // ftoa() prints the float as stored, so 869.618 shows its binary noise (pinned as is)
  EXPECT_EQ(run("get radio"), "> 869.6179809,62.5,8,6");
  EXPECT_EQ(run("get tx"), "> 14");
  EXPECT_EQ(run("get rxdelay"), "> 1.5");
  EXPECT_EQ(run("get af"), "> 1.0");
  EXPECT_EQ(run("get freq"), "> 869.6179809");
  EXPECT_TRUE(takeCalls().empty());
}

TEST_F(CliTest, GetAdvertLocShowsThePolicy) {
  prefs.advert_loc_policy = ADVERT_LOC_NONE;  EXPECT_EQ(run("get advert.loc"), "> none");
  prefs.advert_loc_policy = ADVERT_LOC_PREFS; EXPECT_EQ(run("get advert.loc"), "> prefs");
  prefs.advert_loc_policy = ADVERT_LOC_SHARE; EXPECT_EQ(run("get advert.loc"), "> share");
  prefs.advert_loc_policy = 9;                EXPECT_EQ(run("get advert.loc"), "Error, invalid policy");
  EXPECT_TRUE(takeCalls().empty());
}
