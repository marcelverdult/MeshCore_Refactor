// The board option macros std_init() of the SX126x classes reads, all set, and
// MESH_DEBUG on. This is its own test program (not part of test_radio_chips),
// because the chip classes are header only: the two builds must not be linked
// into one binary with different macro values.
#define ARDUINO 1
#define MESH_DEBUG 1
#define SX126X_USE_REGULATOR_LDO true
#define SX126X_REGISTER_PATCH 1
#define SX126X_DIO3_TCXO_VOLTAGE 1.8f
#define LORA_CR 8
#define SX126X_CURRENT_LIMIT 140
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_RX_BOOSTED_GAIN 1
#define SX126X_RXEN 7
#define SX126X_TXEN 8
#define P_LORA_SCLK 5
#define P_LORA_MISO 3
#define P_LORA_MOSI 6
#define LR1121_FORCE_HP_PA 1

#include <gtest/gtest.h>

#include <helpers/radiolib/CustomSX1262.h>
#include <helpers/radiolib/CustomSX1268.h>
#include <helpers/radiolib/CustomLLCC68.h>
#include <helpers/radiolib/CustomLR1110.h>
#include <helpers/radiolib/CustomLR1121.h>

// What std_init() does with every option set. All three chips share the
// configuration path; SX1262 alone passes the LDO option to begin(), applies the
// register patch for 0x8B5 and prints its three debug lines. SX1268 and LLCC68
// ignore those two macros and print nothing. These differences are pinned per
// chip, so a refactor that merges the classes has to keep them.

namespace {

template <typename T>
struct InitTest : public ::testing::Test {
  Module mod;
  T radio{&mod};
  SPIClass spi;
  void SetUp() override {
    Serial.reset();
    radio.regs[0x8B5] = 0x40;
  }
};

class SX1262Init : public InitTest<CustomSX1262> { };
class SX1268Init : public InitTest<CustomSX1268> { };
class LLCC68Init : public InitTest<CustomLLCC68> { };

// The calls between begin() and the register patch, the same for all three.
const std::vector<std::string> CONFIG = {
  "setCRC(1)", "setCurrentLimit(140)", "setDio2AsRfSwitch(1)",
  "setRxBoostedGainMode(1)", "setRfSwitchPins(7,8)",
};
const std::vector<std::string> SPI_BEGIN = {"begin(5,3,6)"};

std::vector<std::string> join(std::vector<std::string> a, const std::vector<std::string>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

TEST_F(SX1262Init, AllOptions) {
  EXPECT_TRUE(radio.std_init(&spi));
  EXPECT_EQ(spi.calls, SPI_BEGIN);
  auto want = join({"begin(869.525,250,11,8,18,22,16,1.8,1)"}, CONFIG);
  want = join(want, {"readRegister(2229,1)", "writeRegister(2229,1):65"});
  EXPECT_EQ(radio.calls, want);
  EXPECT_EQ(radio.regs[0x8B5], 0x41);
  EXPECT_EQ(Serial.out,
            "DEBUG: SX1262 regulator requested: LDO\n"
            "DEBUG: SX1262 status=0xA5 device_errors=0x0120\n");
}

TEST_F(SX1262Init, RetryKeepsLdoAndPrintsTheError) {
  radio.begin_results = {RADIOLIB_ERR_SPI_CMD_FAILED};
  EXPECT_TRUE(radio.std_init(&spi));
  auto want = join({"begin(869.525,250,11,8,18,22,16,1.8,1)", "begin(869.525,250,11,8,18,22,16,0,1)"}, CONFIG);
  want = join(want, {"readRegister(2229,1)", "writeRegister(2229,1):65"});
  EXPECT_EQ(radio.calls, want);
  EXPECT_EQ(Serial.out,
            "DEBUG: SX1262 regulator requested: LDO\n"
            "DEBUG: SX1262 init failed with error -707, retrying with TCXO at 0.0V\n"
            "DEBUG: SX1262 status=0xA5 device_errors=0x0120\n");
}

TEST_F(SX1262Init, FailureSkipsPatchAndStatusLine) {
  radio.begin_results = {-2};
  EXPECT_FALSE(radio.std_init(&spi));
  std::vector<std::string> want = {"begin(869.525,250,11,8,18,22,16,1.8,1)"};
  EXPECT_EQ(radio.calls, want);
  EXPECT_EQ(Serial.out,
            "DEBUG: SX1262 regulator requested: LDO\n"
            "ERROR: radio init failed: -2\n");
}

// SX1268 and LLCC68: begin() without the LDO argument (RadioLib's default false),
// no register patch, no debug lines.
template <typename F> void expectWithoutSX1262Extras(F& t) {
  EXPECT_TRUE(t.radio.std_init(&t.spi));
  EXPECT_EQ(t.spi.calls, SPI_BEGIN);
  EXPECT_EQ(t.radio.calls, join({"begin(869.525,250,11,8,18,22,16,1.8,0)"}, CONFIG));
  EXPECT_EQ(t.radio.regs[0x8B5], 0x40);
  EXPECT_EQ(Serial.out, "");
}

template <typename F> void expectRetryWithoutSX1262Extras(F& t) {
  t.radio.begin_results = {RADIOLIB_ERR_SPI_CMD_FAILED};
  EXPECT_TRUE(t.radio.std_init(&t.spi));
  EXPECT_EQ(t.radio.calls, join({"begin(869.525,250,11,8,18,22,16,1.8,0)", "begin(869.525,250,11,8,18,22,16,0,0)"}, CONFIG));
  EXPECT_EQ(Serial.out, "");
}

TEST_F(SX1268Init, AllOptions) { expectWithoutSX1262Extras(*this); }
TEST_F(SX1268Init, RetryIsSilent) { expectRetryWithoutSX1262Extras(*this); }
TEST_F(LLCC68Init, AllOptions) { expectWithoutSX1262Extras(*this); }
TEST_F(LLCC68Init, RetryIsSilent) { expectRetryWithoutSX1262Extras(*this); }

// The LR11x0 chip classes with MESH_DEBUG: the line after a header error names
// the chip. With LR1121_FORCE_HP_PA, LR1121 asks for the high power PA.
template <typename T>
struct LR11x0Debug : public ::testing::Test {
  Module mod;
  T radio{&mod};
  void SetUp() override { Serial.reset(); }
  void headerError() {
    radio.rx_length = 0;
    radio.irq = RADIOLIB_LR11X0_IRQ_HEADER_ERR;
    EXPECT_EQ(radio.getPacketLength(true), 0u);
    std::vector<std::string> want = {"standby()"};
    EXPECT_EQ(radio.calls, want);
  }
};
class LR1110Debug : public LR11x0Debug<CustomLR1110> { };
class LR1121Debug : public LR11x0Debug<CustomLR1121> { };

TEST_F(LR1110Debug, HeaderErrorLineNamesTheChip) {
  headerError();
  EXPECT_EQ(Serial.out, "DEBUG: LR1110: got header err, calling standby()\n");
}

TEST_F(LR1121Debug, HeaderErrorLineNamesTheChip) {
  headerError();
  EXPECT_EQ(Serial.out, "DEBUG: LR1121: got header err, calling standby()\n");
}

TEST_F(LR1110Debug, BeginPrintsNothing) {
  EXPECT_EQ(radio.begin(), RADIOLIB_ERR_NONE);
  EXPECT_EQ(Serial.out, "");
}

TEST_F(LR1121Debug, ForcedHighPowerPa) {
  radio.clip_to = 13;
  EXPECT_EQ(radio.setOutputPower(22), RADIOLIB_ERR_NONE);
  std::vector<std::string> want = {"checkOutputPower(22,1)", "setOutputPower(13,1)"};
  EXPECT_EQ(radio.calls, want);
}

TEST_F(LR1110Debug, PowerIgnoresTheLr1121Option) {
  EXPECT_EQ(radio.setOutputPower(22), RADIOLIB_ERR_NONE);
  std::vector<std::string> want = {"setOutputPower(22)"};
  EXPECT_EQ(radio.calls, want);
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
