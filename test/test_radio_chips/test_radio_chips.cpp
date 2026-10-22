#include <gtest/gtest.h>

#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1268.h>
#include <helpers/radiolib/CustomLLCC68.h>
#include <helpers/radiolib/CustomSTM32WLx.h>
#include <helpers/radiolib/CustomLR1110.h>
#include <helpers/radiolib/CustomLR1121.h>
#include <helpers/radiolib/CustomLR2021.h>

// Characterization of how the chip classes tell the wrapper that a packet is on
// the air (isReceiving(), which RadioLibWrapper::isReceivingPacket() forwards to).
// The dispatcher holds a transmission back while this reports true, so the
// timing below decides when a node talks over a packet it is hearing.
//
// The same expectations run against every chip class. Each chip names its IRQ
// bits differently and clears a slightly different set, which the traits below
// spell out; everything else is meant to be the same state machine.

namespace {

struct SX126xBits {
  static constexpr uint32_t preamble = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED;
  static constexpr uint32_t header = RADIOLIB_SX126X_IRQ_HEADER_VALID;
  static constexpr uint32_t hdrErr = RADIOLIB_SX126X_IRQ_HEADER_ERR;
  // the SX126x copies also clear the sync word bit
  static constexpr uint32_t clearAll = preamble | header | hdrErr | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID;
  static constexpr uint32_t rxTimeout = RADIOLIB_SX126X_RX_TIMEOUT_INF;
};
struct LR11x0Bits {
  static constexpr uint32_t preamble = RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED;
  static constexpr uint32_t header = RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID;
  static constexpr uint32_t hdrErr = RADIOLIB_LR11X0_IRQ_HEADER_ERR;
  static constexpr uint32_t clearAll = preamble | header | hdrErr;
  static constexpr uint32_t rxTimeout = RADIOLIB_LR11X0_RX_TIMEOUT_INF;
};
struct LR2021Bits {
  static constexpr uint32_t preamble = RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED;
  static constexpr uint32_t header = RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID;
  static constexpr uint32_t hdrErr = RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR;
  static constexpr uint32_t clearAll = preamble | header | hdrErr;
  static constexpr uint32_t rxTimeout = RADIOLIB_LR2021_RX_TIMEOUT_INF;
};

template <typename C, typename B> struct Chip { using Radio = C; using Bits = B; };

using Chips = ::testing::Types<
  Chip<CustomSX1262, SX126xBits>,
  Chip<CustomSX1268, SX126xBits>,
  Chip<CustomLLCC68, SX126xBits>,
  Chip<CustomSTM32WLx, SX126xBits>,
  Chip<CustomLR1110, LR11x0Bits>,
  Chip<CustomLR1121, LR11x0Bits>,
  Chip<CustomLR2021, LR2021Bits>>;

// The defaults the chip classes start with, before setParams() replaces them.
constexpr uint32_t DEFAULT_PREAMBLE_MS = 66;
constexpr uint32_t DEFAULT_PAYLOAD_MS = 3934;

// Start the clock away from zero: a timer value of 0 means "not started" in the
// state machine, which a separate test pins.
constexpr uint32_t T0 = 1000;

template <typename T>
struct RadioChipTest : public ::testing::Test {
  using Radio = typename T::Radio;
  using Bits = typename T::Bits;
  Module mod;
  Radio radio{&mod};

  void SetUp() override { g_mock_millis = T0; }

  bool poll(uint32_t irq) { radio.irq = irq; return radio.isReceiving(); }
  void at(uint32_t ms) { g_mock_millis = T0 + ms; }
};
TYPED_TEST_SUITE(RadioChipTest, Chips);

TYPED_TEST(RadioChipTest, QuietChannelIsNotReceiving) {
  EXPECT_FALSE(this->poll(0));
  EXPECT_TRUE(this->radio.clears.empty());
}

TYPED_TEST(RadioChipTest, OtherIrqBitsAreIgnored) {
  using B = typename TestFixture::Bits;
  uint32_t other = ~(B::preamble | B::header | B::hdrErr);
  EXPECT_FALSE(this->poll(other));
  EXPECT_TRUE(this->radio.clears.empty());
}

TYPED_TEST(RadioChipTest, PreambleCountsUntilItsDeadlineThenIsCleared) {
  using B = typename TestFixture::Bits;
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(DEFAULT_PREAMBLE_MS);          // deadline itself still counts
  EXPECT_TRUE(this->poll(B::preamble));
  EXPECT_TRUE(this->radio.clears.empty());
  this->at(DEFAULT_PREAMBLE_MS + 1);      // one past: give up on it
  EXPECT_FALSE(this->poll(B::preamble));
  ASSERT_EQ(this->radio.clears.size(), 1u);
  EXPECT_EQ(this->radio.clears[0], B::preamble);  // only the preamble bit
}

TYPED_TEST(RadioChipTest, PreambleTimerRestartsAfterAClear) {
  using B = typename TestFixture::Bits;
  this->poll(B::preamble);
  this->at(DEFAULT_PREAMBLE_MS + 1);
  EXPECT_FALSE(this->poll(B::preamble));
  // the chip reports a new preamble: a fresh deadline from now
  this->at(DEFAULT_PREAMBLE_MS + 2);
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(2 * DEFAULT_PREAMBLE_MS + 2);
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(2 * DEFAULT_PREAMBLE_MS + 3);
  EXPECT_FALSE(this->poll(B::preamble));
  EXPECT_EQ(this->radio.clears.size(), 2u);
}

TYPED_TEST(RadioChipTest, QuietPollResetsThePreambleTimer) {
  using B = typename TestFixture::Bits;
  this->poll(B::preamble);
  this->at(50);
  EXPECT_FALSE(this->poll(0));
  this->at(100);                           // 100 ms after the first preamble
  EXPECT_TRUE(this->poll(B::preamble));    // but a new timer started at 100
  this->at(100 + DEFAULT_PREAMBLE_MS);
  EXPECT_TRUE(this->poll(B::preamble));
  EXPECT_TRUE(this->radio.clears.empty());
}

TYPED_TEST(RadioChipTest, HeaderCountsUntilThePayloadDeadlineThenAllIsCleared) {
  using B = typename TestFixture::Bits;
  EXPECT_TRUE(this->poll(B::preamble | B::header));
  this->at(DEFAULT_PAYLOAD_MS);
  EXPECT_TRUE(this->poll(B::preamble | B::header));
  EXPECT_TRUE(this->radio.clears.empty());
  this->at(DEFAULT_PAYLOAD_MS + 1);
  EXPECT_FALSE(this->poll(B::preamble | B::header));
  ASSERT_EQ(this->radio.clears.size(), 1u);
  EXPECT_EQ(this->radio.clears[0], B::clearAll);
}

TYPED_TEST(RadioChipTest, PayloadDeadlineStartsAtTheHeaderNotThePreamble) {
  using B = typename TestFixture::Bits;
  this->poll(B::preamble);
  this->at(50);
  EXPECT_TRUE(this->poll(B::preamble | B::header));
  this->at(50 + DEFAULT_PAYLOAD_MS);
  EXPECT_TRUE(this->poll(B::header));
  this->at(51 + DEFAULT_PAYLOAD_MS);
  EXPECT_FALSE(this->poll(B::header));
}

TYPED_TEST(RadioChipTest, HeaderAloneOutlivesThePreambleDeadline) {
  using B = typename TestFixture::Bits;
  EXPECT_TRUE(this->poll(B::header));
  this->at(DEFAULT_PREAMBLE_MS + 500);
  EXPECT_TRUE(this->poll(B::header | B::preamble));
  EXPECT_TRUE(this->radio.clears.empty());
}

TYPED_TEST(RadioChipTest, HeaderErrorWinsAndClearsEverything) {
  using B = typename TestFixture::Bits;
  this->poll(B::preamble | B::header);
  this->at(10);
  EXPECT_FALSE(this->poll(B::preamble | B::header | B::hdrErr));
  ASSERT_EQ(this->radio.clears.size(), 1u);
  EXPECT_EQ(this->radio.clears[0], B::clearAll);
}

TYPED_TEST(RadioChipTest, HeaderErrorWithoutHeaderAlsoClears) {
  using B = typename TestFixture::Bits;
  EXPECT_FALSE(this->poll(B::hdrErr));
  ASSERT_EQ(this->radio.clears.size(), 1u);
  EXPECT_EQ(this->radio.clears[0], B::clearAll);
}

TYPED_TEST(RadioChipTest, HeaderErrorResetsTheTimers) {
  using B = typename TestFixture::Bits;
  this->poll(B::preamble);
  this->at(60);
  EXPECT_FALSE(this->poll(B::hdrErr));
  this->at(61);                           // a new preamble right after
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(61 + DEFAULT_PREAMBLE_MS);
  EXPECT_TRUE(this->poll(B::preamble));
}

TYPED_TEST(RadioChipTest, HeaderFlagClearedElsewhereEndsTheReception) {
  using B = typename TestFixture::Bits;
  this->poll(B::header);
  this->at(10);
  // the header bit is gone (the packet was read), a preamble bit is still up
  EXPECT_FALSE(this->poll(B::preamble));
  EXPECT_TRUE(this->radio.clears.empty());
  // that poll reset the state, so the next one starts a preamble timer
  this->at(20);
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(20 + DEFAULT_PREAMBLE_MS);
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(21 + DEFAULT_PREAMBLE_MS);
  EXPECT_FALSE(this->poll(B::preamble));
}

TYPED_TEST(RadioChipTest, DeadlinesFollowTheSetters) {
  using B = typename TestFixture::Bits;
  this->radio.setPreambleMillis(20);
  this->radio.setMaxPayloadMillis(300);
  this->poll(B::preamble);
  this->at(20);
  EXPECT_TRUE(this->poll(B::preamble));
  this->at(21);
  EXPECT_FALSE(this->poll(B::preamble));

  this->at(100);
  this->poll(B::header);
  this->at(400);
  EXPECT_TRUE(this->poll(B::header));
  this->at(401);
  EXPECT_FALSE(this->poll(B::header));
}

// A timer value of 0 doubles as "not started". A preamble first seen at
// millis() == 0 therefore starts its timer on the next poll instead. Pinned
// because a rewrite with a separate flag would change it.
TYPED_TEST(RadioChipTest, PreambleSeenAtMillisZeroStartsOnTheNextPoll) {
  using B = typename TestFixture::Bits;
  g_mock_millis = 0;
  EXPECT_TRUE(this->poll(B::preamble));
  g_mock_millis = 50;
  EXPECT_TRUE(this->poll(B::preamble));
  g_mock_millis = 50 + DEFAULT_PREAMBLE_MS;  // 116 ms after the first sighting
  EXPECT_TRUE(this->poll(B::preamble));
  g_mock_millis = 51 + DEFAULT_PREAMBLE_MS;
  EXPECT_FALSE(this->poll(B::preamble));
}

TYPED_TEST(RadioChipTest, DeadlinesSurviveTheMillisWrap) {
  using B = typename TestFixture::Bits;
  g_mock_millis = 0xFFFFFFF0u;
  this->poll(B::header);
  g_mock_millis = 0xFFFFFFF0u + DEFAULT_PAYLOAD_MS;  // wraps past zero
  EXPECT_TRUE(this->poll(B::header));
  g_mock_millis = 0xFFFFFFF0u + DEFAULT_PAYLOAD_MS + 1;
  EXPECT_FALSE(this->poll(B::header));
}

TYPED_TEST(RadioChipTest, StartReceiveAsksForThePreambleIrq) {
  using B = typename TestFixture::Bits;
  this->radio.startReceive();
  ASSERT_EQ(this->radio.rx_calls.size(), 1u);
  const auto& c = this->radio.rx_calls[0];
  EXPECT_EQ(c.timeout, B::rxTimeout);
  EXPECT_EQ(c.flags, RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED));
  EXPECT_EQ(c.mask, RADIOLIB_IRQ_RX_DEFAULT_MASK);
  EXPECT_EQ(c.len, 0u);
}

// std_init() and getRxBoostedGainMode() of the three SX126x classes that have a
// std_init(), built here without any of the board option macros
// (SX126X_USE_REGULATOR_LDO, SX126X_REGISTER_PATCH, SX126X_CURRENT_LIMIT, ...),
// without P_LORA_SCLK and without MESH_DEBUG. test_radio_chips_init builds the
// same classes with all of them set.

template <typename T>
struct SX126xInitTest : public ::testing::Test {
  Module mod;
  T radio{&mod};
  void SetUp() override { Serial.reset(); }
};
using SX126xChips = ::testing::Types<CustomSX1262, CustomSX1268, CustomLLCC68>;
TYPED_TEST_SUITE(SX126xInitTest, SX126xChips);

// LORA_FREQ, LORA_BW, LORA_SF and LORA_TX_POWER come from the environment's
// build flags; coding rate 5, the private sync word (0x12 = 18), preamble 16,
// TCXO 1.6 V and the DC-DC regulator are the defaults std_init() supplies.
const std::string BEGIN_DEFAULT = "begin(869.525,250,11,5,18,22,16,1.6,0)";
const std::string BEGIN_TCXO_OFF = "begin(869.525,250,11,5,18,22,16,0,0)";

TYPED_TEST(SX126xInitTest, StdInitWithoutOptions) {
  EXPECT_TRUE(this->radio.std_init());
  std::vector<std::string> want = {BEGIN_DEFAULT, "setCRC(1)"};
  EXPECT_EQ(this->radio.calls, want);
  EXPECT_EQ(Serial.out, "");
}

TYPED_TEST(SX126xInitTest, StdInitWithSpiButNoPinsLeavesSpiAlone) {
  SPIClass spi;
  EXPECT_TRUE(this->radio.std_init(&spi));
  EXPECT_TRUE(spi.calls.empty());
}

TYPED_TEST(SX126xInitTest, SpiCmdFailedRetriesWithTcxoOff) {
  this->radio.begin_results = {RADIOLIB_ERR_SPI_CMD_FAILED};
  EXPECT_TRUE(this->radio.std_init());
  std::vector<std::string> want = {BEGIN_DEFAULT, BEGIN_TCXO_OFF, "setCRC(1)"};
  EXPECT_EQ(this->radio.calls, want);
  EXPECT_EQ(Serial.out, "");
}

TYPED_TEST(SX126xInitTest, SpiCmdInvalidRetriesWithTcxoOff) {
  this->radio.begin_results = {RADIOLIB_ERR_SPI_CMD_INVALID};
  EXPECT_TRUE(this->radio.std_init());
  std::vector<std::string> want = {BEGIN_DEFAULT, BEGIN_TCXO_OFF, "setCRC(1)"};
  EXPECT_EQ(this->radio.calls, want);
}

TYPED_TEST(SX126xInitTest, FailedRetryReportsTheSecondError) {
  this->radio.begin_results = {RADIOLIB_ERR_SPI_CMD_FAILED, RADIOLIB_ERR_SPI_CMD_INVALID};
  EXPECT_FALSE(this->radio.std_init());
  std::vector<std::string> want = {BEGIN_DEFAULT, BEGIN_TCXO_OFF};
  EXPECT_EQ(this->radio.calls, want);
  EXPECT_EQ(Serial.out, "ERROR: radio init failed: -706\n");
}

TYPED_TEST(SX126xInitTest, OtherErrorIsNotRetried) {
  this->radio.begin_results = {-2};
  EXPECT_FALSE(this->radio.std_init());
  std::vector<std::string> want = {BEGIN_DEFAULT};
  EXPECT_EQ(this->radio.calls, want);
  EXPECT_EQ(Serial.out, "ERROR: radio init failed: -2\n");
}

TYPED_TEST(SX126xInitTest, RxBoostedGainModeReadsTheGainRegister) {
  this->radio.regs[RADIOLIB_SX126X_REG_RX_GAIN] = RADIOLIB_SX126X_RX_GAIN_BOOSTED;
  EXPECT_TRUE(this->radio.getRxBoostedGainMode());
  this->radio.regs[RADIOLIB_SX126X_REG_RX_GAIN] = 0x94;  // power saving gain
  EXPECT_FALSE(this->radio.getRxBoostedGainMode());
  std::vector<std::string> want = {"readRegister(2220,1)", "readRegister(2220,1)"};
  EXPECT_EQ(this->radio.calls, want);
}

// The rest of the LR11x0 chip classes: begin() on the DC-DC regulator, the
// standby after a header error, the getters the wrapper reads and the boosted
// gain flag. LR1110 and LR1121 carry the same code for these.
template <typename T>
struct LR11x0ChipTest : public ::testing::Test {
  Module mod;
  T radio{&mod};
  void SetUp() override { Serial.reset(); }
};
using LR11x0Chips = ::testing::Types<CustomLR1110, CustomLR1121>;
TYPED_TEST_SUITE(LR11x0ChipTest, LR11x0Chips);

using Log = std::vector<std::string>;

TYPED_TEST(LR11x0ChipTest, BeginPassesItsArgumentsThenSwitchesToDcdc) {
  EXPECT_EQ(this->radio.begin(869.525f, 250.0f, 11, 5, 0x12, 22, 16, 1.8f), RADIOLIB_ERR_NONE);
  Log want = {"begin(869.525,250,11,5,18,22,16,1.8)", "setRegulatorDCDC()"};
  EXPECT_EQ(this->radio.calls, want);
}

TYPED_TEST(LR11x0ChipTest, BeginKeepsRadioLibsDefaults) {
  EXPECT_EQ(this->radio.begin(), RADIOLIB_ERR_NONE);
  Log want = {"begin(434,125,9,7,18,10,8,1.6)", "setRegulatorDCDC()"};
  EXPECT_EQ(this->radio.calls, want);
}

TYPED_TEST(LR11x0ChipTest, FailedBeginSkipsTheRegulator) {
  this->radio.begin_results = {-2};
  EXPECT_EQ(this->radio.begin(), -2);
  Log want = {"begin(434,125,9,7,18,10,8,1.6)"};
  EXPECT_EQ(this->radio.calls, want);
}

TYPED_TEST(LR11x0ChipTest, RegulatorErrorIsReturned) {
  this->radio.dcdc_results = {-5};
  EXPECT_EQ(this->radio.begin(), -5);
}

TYPED_TEST(LR11x0ChipTest, EmptyPacketWithHeaderErrorPutsTheChipInStandby) {
  this->radio.rx_length = 0;
  this->radio.irq = RADIOLIB_LR11X0_IRQ_HEADER_ERR;
  EXPECT_EQ(this->radio.getPacketLength(true), 0u);
  Log want = {"standby()"};
  EXPECT_EQ(this->radio.calls, want);
  EXPECT_TRUE(this->radio.clears.empty());  // the IRQ bits are left alone
  EXPECT_EQ(Serial.out, "");                // the debug line is off here
}

TYPED_TEST(LR11x0ChipTest, EmptyPacketWithoutHeaderErrorIsLeftAlone) {
  this->radio.rx_length = 0;
  this->radio.irq = ~RADIOLIB_LR11X0_IRQ_HEADER_ERR;
  EXPECT_EQ(this->radio.getPacketLength(false), 0u);
  EXPECT_TRUE(this->radio.calls.empty());
}

TYPED_TEST(LR11x0ChipTest, PacketWithHeaderErrorBitIsLeftAlone) {
  this->radio.rx_length = 42;
  this->radio.irq = RADIOLIB_LR11X0_IRQ_HEADER_ERR;
  EXPECT_EQ(this->radio.getPacketLength(true), 42u);
  EXPECT_TRUE(this->radio.calls.empty());
}

TYPED_TEST(LR11x0ChipTest, FrequencyAndSpreadingFactorAreTheChipsMembers) {
  this->radio.freqMHz = 433.875f;
  this->radio.spreadingFactor = 7;
  EXPECT_EQ(this->radio.getFreqMHz(), 433.875f);
  EXPECT_EQ(this->radio.getSpreadingFactor(), 7);
}

TYPED_TEST(LR11x0ChipTest, BoostedGainFlagIsStoredBeforeTheChipAnswers) {
  EXPECT_FALSE(this->radio.getRxBoostedGainMode());
  EXPECT_EQ(this->radio.setRxBoostedGainMode(true), RADIOLIB_ERR_NONE);
  EXPECT_TRUE(this->radio.getRxBoostedGainMode());
  this->radio.gain_results = {-1};
  EXPECT_EQ(this->radio.setRxBoostedGainMode(false), -1);
  EXPECT_FALSE(this->radio.getRxBoostedGainMode());  // stored although the chip failed
  this->radio.gain_results = {-1};
  EXPECT_EQ(this->radio.setRxBoostedGainMode(true), -1);
  EXPECT_TRUE(this->radio.getRxBoostedGainMode());
  Log want = {"setRxBoostedGainMode(1)", "setRxBoostedGainMode(0)", "setRxBoostedGainMode(1)"};
  EXPECT_EQ(this->radio.calls, want);
}

// The output power: LR1110 leaves it to RadioLib; LR1121 clips it to the range of
// the PA the band selects and applies it again after a frequency change.
struct LR1110Power : public ::testing::Test {
  Module mod;
  CustomLR1110 radio{&mod};
};
struct LR1121Power : public ::testing::Test {
  Module mod;
  CustomLR1121 radio{&mod};
};

TEST_F(LR1110Power, PowerAndFrequencyGoStraightToRadioLib) {
  EXPECT_EQ(radio.setOutputPower(22), RADIOLIB_ERR_NONE);
  EXPECT_EQ(radio.setFrequency(2400.0f), RADIOLIB_ERR_NONE);
  Log want = {"setOutputPower(22)", "setFrequency(2400)"};
  EXPECT_EQ(radio.calls, want);
  radio.power_results = {-13};
  EXPECT_EQ(radio.setOutputPower(30), -13);
}

TEST_F(LR1121Power, FrequencyBeforeAnyPowerIsPlain) {
  EXPECT_EQ(radio.setFrequency(915.0f), RADIOLIB_ERR_NONE);
  Log want = {"setFrequency(915)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1121Power, PowerIsClippedToThePaRange) {
  radio.clip_to = 13;
  EXPECT_EQ(radio.setOutputPower(22), RADIOLIB_ERR_NONE);
  Log want = {"checkOutputPower(22,0)", "setOutputPower(13,0)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1121Power, FrequencyChangeAppliesTheRequestedPowerAgain) {
  radio.clip_to = 13;
  radio.setOutputPower(22);
  radio.calls.clear();
  radio.clip_to = 22;
  EXPECT_EQ(radio.setFrequency(868.0f), RADIOLIB_ERR_NONE);
  Log want = {"setFrequency(868)", "checkOutputPower(22,0)", "setOutputPower(22,0)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1121Power, FailedPowerIsNotRemembered) {
  radio.setOutputPower(10);
  radio.power_results = {-13};
  EXPECT_EQ(radio.setOutputPower(22), -13);
  radio.calls.clear();
  EXPECT_EQ(radio.setFrequency(868.0f), RADIOLIB_ERR_NONE);
  Log want = {"setFrequency(868)", "checkOutputPower(10,0)", "setOutputPower(10,0)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1121Power, FailedFirstPowerLeavesFrequencyPlain) {
  radio.power_results = {-13};
  EXPECT_EQ(radio.setOutputPower(22), -13);
  radio.calls.clear();
  EXPECT_EQ(radio.setFrequency(868.0f), RADIOLIB_ERR_NONE);
  Log want = {"setFrequency(868)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1121Power, FailedFrequencySkipsThePower) {
  radio.setOutputPower(22);
  radio.calls.clear();
  radio.freq_results = {-12};
  EXPECT_EQ(radio.setFrequency(2400.0f), -12);
  Log want = {"setFrequency(2400)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1121Power, PowerErrorAfterAFrequencyChangeIsReturned) {
  radio.setOutputPower(22);
  radio.power_results = {-13};
  EXPECT_EQ(radio.setFrequency(2400.0f), -13);
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
