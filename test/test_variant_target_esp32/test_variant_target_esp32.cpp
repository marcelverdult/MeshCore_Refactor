#include <gtest/gtest.h>

#include <cstring>

#include <helpers/radiolib/RadioLibWrappers.h>
#include "target.h"

// The radio, wrapper and RTC globals and radio_init() of the 22 ESP32 variants
// of cut D2, pinned on the host: which RadioLib Module constructor the radio is
// built with (pins, own SPI bus and its host, or none), what radio_driver
// wraps, which clock rtc_clock falls back to, and how radio_init() starts them.
// Built in four environments, one per board description that occurs (see
// platformio.ini); each compiles the variant blocks of its description,
// verbatim. See docs/refactoring/0011-variant-target.md, "Cut D".

mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }
mesh::LocalIdentity::LocalIdentity(mesh::RNG* rng) { rng->random(pub_key, SEED_SIZE); }
void AutoDiscoverRTCClock::begin(TwoWire& wire) {
  target_calls.push_back(&wire == &Wire ? "rtc_clock.begin(Wire)" : "rtc_clock.begin(other)");
}
uint32_t AutoDiscoverRTCClock::getCurrentTime() { return _fallback->getCurrentTime(); }
void AutoDiscoverRTCClock::setCurrentTime(uint32_t time) { _fallback->setCurrentTime(time); }

TestBoard board;

// No target.h declares the fallback clock; the test needs it to see where
// rtc_clock falls back to.
extern ESP32RTCClock fallback_clock;

// The variant's block and radio_init(), verbatim from the variants with this
// environment's description: SX1276 from heltec_v2 and lilygo_tbeam_SX1276,
// an own SPI host from heltec_e213, heltec_e290, heltec_rc32 and heltec_t190,
// otherwise from the 10 with the plain block (heltec_v3 and others).
#if defined(USE_SX1276)
#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1);
#endif
#elif defined(P_LORA_SPI_HOST)
#if defined(P_LORA_SCLK)
  static SPIClass spi(FSPI);
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif
#else
#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

// The same block and body once more, as the reference the cases are checked
// against on every commit, whatever defines the globals above.
namespace copy {
#if defined(USE_SX1276)
#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1);
#endif
#elif defined(P_LORA_SPI_HOST)
#if defined(P_LORA_SCLK)
  static SPIClass spi(FSPI);
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif
#else
#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}
}  // namespace copy

// The blocks of the variants that have their own bus in every environment and
// so write it without the #if (lilygo_t3s3, meshadventurer and the two nibble
// boards; lilygo_tlora_v2_1 with the SX1276), the bus of lilygo_tlora_c6 (own
// host 0, the branch its #if takes), and the radio_init() body four of them
// write without the #if (lilygo_t3s3, lilygo_tbeam_SX1262 and the two nibble
// boards). Only where their description is this environment's.
#if defined(P_LORA_SCLK)
namespace unconditional {
#if defined(USE_SX1276)
static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_0, P_LORA_RESET, P_LORA_DIO_1, spi);
#elif defined(P_LORA_SPI_HOST)
  static SPIClass spi(0);
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  return radio.std_init(&spi);
}
}  // namespace unconditional
#endif

namespace {

struct Globals {
  const char* name;
  TestRadioWrapper* driver;
  AutoDiscoverRTCClock* rtc;
  ESP32RTCClock* fallback;
  bool (*init)();
};

std::string name_of(const ::testing::TestParamInfo<Globals>& info) { return info.param.name; }

class RadioGlobals : public ::testing::TestWithParam<Globals> {
protected:
  TestRadio* radio() { return static_cast<TestRadio*>(GetParam().driver->wrapped_radio()); }
  void SetUp() override {
    target_calls.clear();
    if (radio()) {
      radio()->std_init_calls = 0;
      radio()->std_init_result = true;
    }
  }
};

TEST_P(RadioGlobals, RadioIsBuiltFromAModule) {
  ASSERT_NE(nullptr, radio());
  EXPECT_NE(nullptr, radio()->module);
}

TEST_P(RadioGlobals, ModuleHasTheBoardPinsInTheChipsOrder) {
  ASSERT_NE(nullptr, radio()->module);
  EXPECT_EQ((uint32_t)P_LORA_NSS, radio()->module->cs);
#if defined(USE_SX1276)
  EXPECT_EQ((uint32_t)P_LORA_DIO_0, radio()->module->irq);
  EXPECT_EQ((uint32_t)P_LORA_RESET, radio()->module->rst);
  EXPECT_EQ((uint32_t)P_LORA_DIO_1, radio()->module->gpio);
#else
  EXPECT_EQ((uint32_t)P_LORA_DIO_1, radio()->module->irq);
  EXPECT_EQ((uint32_t)P_LORA_RESET, radio()->module->rst);
  EXPECT_EQ((uint32_t)P_LORA_BUSY, radio()->module->gpio);
#endif
}

TEST_P(RadioGlobals, ModuleBus) {
  ASSERT_NE(nullptr, radio()->module);
#if defined(P_LORA_SCLK)
  // An own bus, passed to RadioLib, on the variant's host or the default one.
  ASSERT_NE(nullptr, radio()->module->spi);
  EXPECT_NE(&SPI, radio()->module->spi);
#if defined(P_LORA_SPI_HOST)
  EXPECT_EQ(FSPI, radio()->module->spi->bus);
#else
  EXPECT_EQ(HSPI, radio()->module->spi->bus);
#endif
#else
  // No bus: RadioLib starts its default bus itself.
  EXPECT_EQ(nullptr, radio()->module->spi);
#endif
}

TEST_P(RadioGlobals, DriverWrapsTheBoard) {
  EXPECT_EQ(&board, GetParam().driver->wrapped_board());
}

TEST_P(RadioGlobals, RtcClockFallsBackToTheFallbackClock) {
  GetParam().fallback->setCurrentTime(1234567);
  EXPECT_EQ(1234567u, GetParam().rtc->getCurrentTime());
  GetParam().rtc->setCurrentTime(7654321);
  EXPECT_EQ(7654321u, GetParam().fallback->getCurrentTime());
}

TEST_P(RadioGlobals, InitStartsTheFallbackClockThenTheRtcThenTheRadio) {
  EXPECT_TRUE(GetParam().init());
#if defined(P_LORA_SCLK)
  std::vector<std::string> expected = {"fallback_clock.begin()", "rtc_clock.begin(Wire)", "std_init(module bus)"};
#else
  std::vector<std::string> expected = {"fallback_clock.begin()", "rtc_clock.begin(Wire)", "std_init()"};
#endif
  EXPECT_EQ(expected, target_calls);
  EXPECT_EQ(1, radio()->std_init_calls);
}

TEST_P(RadioGlobals, InitReturnsFalseWhenTheRadioFails) {
  radio()->std_init_result = false;
  EXPECT_FALSE(GetParam().init());
  EXPECT_EQ(3u, target_calls.size());
}

INSTANTIATE_TEST_SUITE_P(Copy, RadioGlobals,
  ::testing::Values(Globals{"Copy", &copy::radio_driver, &copy::rtc_clock, &copy::fallback_clock, &copy::radio_init}), name_of);

#if defined(P_LORA_SCLK)
INSTANTIATE_TEST_SUITE_P(Unconditional, RadioGlobals,
  ::testing::Values(Globals{"Unconditional", &unconditional::radio_driver, &unconditional::rtc_clock,
                            &unconditional::fallback_clock, &unconditional::radio_init}), name_of);
#endif

// The globals and radio_init() the variant gets, whoever defines them.
INSTANTIATE_TEST_SUITE_P(Variant, RadioGlobals,
  ::testing::Values(Globals{"Variant", &radio_driver, &rtc_clock, &fallback_clock, &radio_init}), name_of);

}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
