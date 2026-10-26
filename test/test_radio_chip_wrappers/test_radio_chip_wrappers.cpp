// The boosted gain macros the two reset helpers read, so the value a wrapper
// passes to them shows up in the call log. Its own test program: the wrappers
// and reset helpers are header only, and test_radio_chips builds the chip
// classes without these macros.
#define SX126X_RX_BOOSTED_GAIN 1
#define RX_BOOSTED_GAIN 1

#include <gtest/gtest.h>

#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/radiolib/CustomSX1268Wrapper.h>
#include <helpers/radiolib/CustomLLCC68Wrapper.h>
#include <helpers/radiolib/CustomSTM32WLxWrapper.h>
#include <helpers/radiolib/CustomLR1110Wrapper.h>
#include <helpers/radiolib/CustomLR1121Wrapper.h>
#include <helpers/radiolib/CustomLR2021Wrapper.h>
#include <helpers/radiolib/CustomSX1276Wrapper.h>

#include <string>
#include <vector>

// Characterization of the eight chip wrappers (Custom<chip>Wrapper.h): what each
// one asks of its chip class when the mesh sets the radio parameters, reads the
// signal, resets the receiver, finishes a transmission or powers off. The same
// expectations run against every wrapper; where the families differ, the traits
// below say how, so a refactor that shares code between them has to keep each
// difference.

namespace {

class StubBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 4200; }
  const char* getManufacturerName() const override { return "stub"; }
  void reboot() override { }
  uint8_t getStartupReason() const override { return 0; }
};

enum Family { SX126X, LR11X0, LR2021_, SX127X };

// boost: which boosted gain override the wrapper has (NONE inherits the base
// class, which reports false and touches nothing).
enum Boost { NONE, SX126X_REG, LR11X0_FLAG, LR2021_LEVEL };

template <typename W, typename R, Family F, Boost B, bool SleepFalse>
struct Wrap {
  using Wrapper = W;
  using Radio = R;
  static constexpr Family family = F;
  static constexpr Boost boost = B;
  static constexpr bool powerOffSleepFalse = SleepFalse;
};

using Wrappers = ::testing::Types<
  Wrap<CustomSX1262Wrapper, CustomSX1262, SX126X, SX126X_REG, true>,
  Wrap<CustomSX1268Wrapper, CustomSX1268, SX126X, SX126X_REG, false>,
  Wrap<CustomLLCC68Wrapper, CustomLLCC68, SX126X, SX126X_REG, false>,
  Wrap<CustomSTM32WLxWrapper, CustomSTM32WLx, SX126X, NONE, false>,
  Wrap<CustomLR1110Wrapper, CustomLR1110, LR11X0, LR11X0_FLAG, false>,
  Wrap<CustomLR1121Wrapper, CustomLR1121, LR11X0, LR11X0_FLAG, false>,
  Wrap<CustomLR2021Wrapper, CustomLR2021, LR2021_, LR2021_LEVEL, false>,
  Wrap<CustomSX1276Wrapper, CustomSX1276, SX127X, NONE, false>>;

// The IRQ bits each family reports a preamble and a header with.
template <typename R> struct Bits;
template <> struct Bits<CustomSX1262> { static constexpr uint32_t preamble = RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED, header = RADIOLIB_SX126X_IRQ_HEADER_VALID; };
template <> struct Bits<CustomSX1268> : Bits<CustomSX1262> { };
template <> struct Bits<CustomLLCC68> : Bits<CustomSX1262> { };
template <> struct Bits<CustomSTM32WLx> : Bits<CustomSX1262> { };
template <> struct Bits<CustomLR1110> { static constexpr uint32_t preamble = RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED, header = RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID; };
template <> struct Bits<CustomLR1121> : Bits<CustomLR1110> { };
template <> struct Bits<CustomLR2021> { static constexpr uint32_t preamble = RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED, header = RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID; };
template <> struct Bits<CustomSX1276> { static constexpr uint32_t preamble = 0, header = 0; };  // no IRQ flags, see below

// SX1276 reports reception through its modem status register.
inline void setModemStatus(CustomSX1276& r, uint8_t status) { r.modem_status = status; }
template <typename R> void setModemStatus(R&, uint8_t) { }

constexpr uint32_t T0 = 1000;

template <typename T>
struct ChipWrapperTest : public ::testing::Test {
  using Wrapper = typename T::Wrapper;
  using Radio = typename T::Radio;
  Module mod;
  Radio radio{&mod};
  StubBoard board;
  Wrapper w{radio, board};

  void SetUp() override { g_mock_millis = T0; }

  std::vector<std::string> take() {
    std::vector<std::string> c = radio.calls;
    radio.calls.clear();
    return c;
  }
};
TYPED_TEST_SUITE(ChipWrapperTest, Wrappers);

using Log = std::vector<std::string>;

// setParams(): the four modem setters in this order, then the preamble length
// for the new spreading factor; LR2021 then reapplies its side detectors and
// returns to standby, so the next poll restarts reception.
TYPED_TEST(ChipWrapperTest, SetParamsCallsTheChipInOrder) {
  this->w.setParams(869.618f, 62.5f, 8, 8);
  Log expect = {"setFrequency(869.618)", "setSpreadingFactor(8)", "setBandwidth(62.5)",
                "setCodingRate(8)", "setPreambleLength(32)"};
  if (TypeParam::family == LR2021_) {
    expect.push_back("setSideDetector(0)");
    expect.push_back("standby()");
  }
  EXPECT_EQ(this->take(), expect);

  this->w.setParams(869.525f, 250.0f, 11, 5);
  expect = {"setFrequency(869.525)", "setSpreadingFactor(11)", "setBandwidth(250)",
            "setCodingRate(5)", "setPreambleLength(16)"};
  if (TypeParam::family == LR2021_) {
    expect.push_back("setSideDetector(0)");
    expect.push_back("standby()");
  }
  EXPECT_EQ(this->take(), expect);
}

// The receive detection: through isReceivingPacket() the wrapper asks the chip
// class. SX1276 reads its modem status, the others their IRQ flags.
TYPED_TEST(ChipWrapperTest, IsReceivingPacketAsksTheChip) {
  if (TypeParam::family == SX127X) {
    setModemStatus(this->radio, 0);
    EXPECT_FALSE(this->w.isReceivingPacket());
    setModemStatus(this->radio, RH_RF95_MODEM_STATUS_SIGNAL_DETECTED);
    EXPECT_TRUE(this->w.isReceivingPacket());
    setModemStatus(this->radio, RH_RF95_MODEM_STATUS_CLEAR);
    EXPECT_FALSE(this->w.isReceivingPacket());
    return;
  }
  this->radio.irq = 0;
  EXPECT_FALSE(this->w.isReceivingPacket());
  this->radio.irq = Bits<typename TypeParam::Radio>::preamble;
  EXPECT_TRUE(this->w.isReceivingPacket());
}

// setParams() also hands the chip class the two deadlines of the receive
// detection, computed from the new parameters; SX1276 has none.
TYPED_TEST(ChipWrapperTest, SetParamsGivesTheChipItsDeadlines) {
  if (TypeParam::family == SX127X) GTEST_SKIP() << "SX1276 has no receive deadlines";
  using B = Bits<typename TypeParam::Radio>;
  const uint8_t sf = 7, cr = 5;
  const float bw = 500.0f;
  this->w.setParams(869.525f, bw, sf, cr);
  PacketMillis pm = this->w.calcMaxPacketMillis(sf, bw, cr, RadioLibWrapper::preambleLengthForSF(sf));
  ASSERT_NE(pm.preambleMillis, 66u);    // not the chip's default
  ASSERT_NE(pm.payloadMillis, 3934u);

  this->radio.irq = B::preamble;
  g_mock_millis = T0;
  EXPECT_TRUE(this->w.isReceivingPacket());
  g_mock_millis = T0 + pm.preambleMillis;
  EXPECT_TRUE(this->w.isReceivingPacket());
  g_mock_millis = T0 + pm.preambleMillis + 1;
  EXPECT_FALSE(this->w.isReceivingPacket());

  this->radio.irq = B::preamble | B::header;
  g_mock_millis = T0 + 10000;
  EXPECT_TRUE(this->w.isReceivingPacket());
  g_mock_millis = T0 + 10000 + pm.payloadMillis;
  EXPECT_TRUE(this->w.isReceivingPacket());
  g_mock_millis = T0 + 10000 + pm.payloadMillis + 1;
  EXPECT_FALSE(this->w.isReceivingPacket());
}

// The last packet's RSSI and SNR come from the chip; the current RSSI is the
// instant reading: getRSSI(false) on SX126x and SX127x, getRssiInst() on the LR
// chips, which report -110 when the chip writes nothing.
TYPED_TEST(ChipWrapperTest, SignalReadings) {
  this->radio.rssi = -80.25f;
  this->radio.snr = 7.5f;
  EXPECT_FLOAT_EQ(this->w.getLastRSSI(), -80.25f);
  EXPECT_FLOAT_EQ(this->w.getLastSNR(), 7.5f);

  if (TypeParam::family == SX126X || TypeParam::family == SX127X) {
    EXPECT_FLOAT_EQ(this->w.getCurrentRSSI(), this->radio.rssi_now);
  } else {
    EXPECT_FLOAT_EQ(this->w.getCurrentRSSI(), this->radio.rssi_inst);
    this->radio.rssi_inst_written = false;
    EXPECT_FLOAT_EQ(this->w.getCurrentRSSI(), -110.0f);
  }
  EXPECT_TRUE(this->take().empty());
}

TYPED_TEST(ChipWrapperTest, SpreadingFactorIsTheChips) {
  for (uint8_t sf : {7, 11, 12}) {
    this->radio.spreadingFactor = sf;
    EXPECT_EQ(this->w.getSpreadingFactor(), sf);
  }
}

// Boosted gain: SX1262, SX1268, LLCC68 set it on the chip and read register
// 0x8AC back; LR1110 and LR1121 set it and remember it; LR2021 sets a level
// from standby; STM32WLx and SX1276 have no override and report false.
TYPED_TEST(ChipWrapperTest, BoostedGain) {
  this->radio.regs[RADIOLIB_SX126X_REG_RX_GAIN] = RADIOLIB_SX126X_RX_GAIN_BOOSTED;
  switch (TypeParam::boost) {
    case NONE:
      EXPECT_FALSE(this->w.setRxBoostedGainMode(true));
      EXPECT_FALSE(this->w.getRxBoostedGainMode());
      EXPECT_EQ(this->take(), Log{});
      break;
    case SX126X_REG:
      EXPECT_TRUE(this->w.setRxBoostedGainMode(true));
      EXPECT_EQ(this->take(), Log{"setRxBoostedGainMode(1)"});
      EXPECT_TRUE(this->w.getRxBoostedGainMode());
      EXPECT_EQ(this->take(), Log{"readRegister(2220,1)"});
      this->radio.regs[RADIOLIB_SX126X_REG_RX_GAIN] = 0x94;
      EXPECT_FALSE(this->w.getRxBoostedGainMode());
      break;
    case LR11X0_FLAG:
      EXPECT_FALSE(this->w.getRxBoostedGainMode());
      EXPECT_TRUE(this->w.setRxBoostedGainMode(true));
      EXPECT_EQ(this->take(), Log{"setRxBoostedGainMode(1)"});
      EXPECT_TRUE(this->w.getRxBoostedGainMode());
      EXPECT_TRUE(this->w.setRxBoostedGainMode(false));
      EXPECT_FALSE(this->w.getRxBoostedGainMode());
      break;
    case LR2021_LEVEL:
      EXPECT_TRUE(this->w.setRxBoostedGainMode(true));
      EXPECT_EQ(this->take(), (Log{"standby()", "setRxBoostedGainMode(7)", "standby()"}));
      EXPECT_TRUE(this->w.setRxBoostedGainMode(false));
      EXPECT_EQ(this->take(), (Log{"standby()", "setRxBoostedGainMode(0)", "standby()"}));
      EXPECT_FALSE(this->w.getRxBoostedGainMode());
      break;
  }
}

// The receiver reset resetAGC() runs when no packet is on the air: the SX126x
// family runs sx126xResetAGC() with its boosted gain state (STM32WLx passes the
// base class's false), LR1110 and LR1121 run lr11x0ResetAGC() around their
// frequency, LR2021 and SX1276 only the base class's warm sleep.
TYPED_TEST(ChipWrapperTest, ResetAGC) {
  this->radio.regs[RADIOLIB_SX126X_REG_RX_GAIN] = RADIOLIB_SX126X_RX_GAIN_BOOSTED;
  this->radio.freqMHz = 869.5f;
  if (TypeParam::boost == LR11X0_FLAG) {
    this->w.setRxBoostedGainMode(true);
    this->take();
  }
  this->w.resetAGC();
  Log c = this->take();
  switch (TypeParam::family) {
    case SX126X: {
      Log expect;
      if (TypeParam::boost == SX126X_REG) expect.push_back("readRegister(2220,1)");
      expect.push_back("sleep(1)");
      expect.push_back("standby(0,1)");
      expect.push_back("calibrateImage(869.5)");
      expect.push_back(TypeParam::boost == SX126X_REG ? "setRxBoostedGainMode(1)" : "setRxBoostedGainMode(0)");
      EXPECT_EQ(c, expect);
      EXPECT_EQ(this->mod.halStub.calls, (Log{"SPIwriteStream(137,1,1,0):127", "delay(5)"}));
      break;
    }
    case LR11X0:
      EXPECT_EQ(c, (Log{"sleep(1,0)", "standby(0,1)", "calibrate(63)",
                        "calibrateImageRejection(865.5,873.5)", "setRxBoostedGainMode(1)"}));
      break;
    case LR2021_:
    case SX127X:
      EXPECT_EQ(c, Log{"sleep()"});
      break;
  }
}

// After a transmission the LR chips set the preamble length again for the
// current spreading factor; the others only finish the transmission.
TYPED_TEST(ChipWrapperTest, OnSendFinished) {
  for (uint8_t sf : {8, 9}) {
    this->radio.spreadingFactor = sf;
    this->w.onSendFinished();
    Log expect = {"finishTransmit()"};
    if (TypeParam::family == LR11X0 || TypeParam::family == LR2021_) {
      expect.push_back(sf <= 8 ? "setPreambleLength(32)" : "setPreambleLength(16)");
    }
    EXPECT_EQ(this->take(), expect);
  }
}

// LR1110 and LR1121 estimate at least 200 ms of air time.
TYPED_TEST(ChipWrapperTest, EstimatedAirtime) {
  this->radio.time_on_air = 150000;
  EXPECT_EQ(this->w.getEstAirtimeFor(20), TypeParam::family == LR11X0 ? 200u : 150u);
  this->radio.time_on_air = 300000;
  EXPECT_EQ(this->w.getEstAirtimeFor(20), 300u);
}

// SX1262 powers off with sleep(false), the others with the base class's sleep().
TYPED_TEST(ChipWrapperTest, PowerOff) {
  this->w.powerOff();
  EXPECT_EQ(this->take(), TypeParam::powerOffSleepFalse ? Log{"sleep(0)"} : Log{"sleep()"});
}

}  // namespace

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
