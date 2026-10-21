#pragma once

// Host stand-in for the RadioLib chip classes that src/helpers/radiolib/Custom*.h
// derive from, so the real chip classes can be compiled and their receive
// detection exercised on the host. It is listed before test/mocks for this suite
// only, and replaces the PhysicalLayer-only mock there.
//
// The IRQ bit values are copied from RadioLib 7.7.1, the version the firmware
// builds against, so a test that names a bit means the bit the chip reports.
// Every call the chip classes make that the tests do not care about is accepted
// by a catch-all template and does nothing.

#include "Arduino.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#define RADIOLIB_ERR_NONE             0
#define RADIOLIB_ERR_SPI_CMD_INVALID  (-706)
#define RADIOLIB_ERR_SPI_CMD_FAILED   (-707)
#define RADIOLIB_NC                   (0xFFFFFFFF)
#define RADIOLIB_PREAMBLE_DETECTED    (-14)
#define RADIOLIB_ASSERT(STATEVAR) { if((STATEVAR) != RADIOLIB_ERR_NONE) { return(STATEVAR); } }

// RadioLib's generic IRQ bit numbers (TypeDef.h, enum RadioLibIrqType_t).
enum {
  RADIOLIB_IRQ_TX_DONE = 0x00,
  RADIOLIB_IRQ_RX_DONE,
  RADIOLIB_IRQ_PREAMBLE_DETECTED,
  RADIOLIB_IRQ_SYNC_WORD_VALID,
  RADIOLIB_IRQ_HEADER_VALID,
  RADIOLIB_IRQ_HEADER_ERR,
  RADIOLIB_IRQ_CRC_ERR,
  RADIOLIB_IRQ_CAD_DONE,
  RADIOLIB_IRQ_CAD_DETECTED,
  RADIOLIB_IRQ_TIMEOUT,
};
#define RADIOLIB_IRQ_RX_DEFAULT_FLAGS ((1UL << RADIOLIB_IRQ_RX_DONE) | (1UL << RADIOLIB_IRQ_TIMEOUT) | (1UL << RADIOLIB_IRQ_CRC_ERR) | (1UL << RADIOLIB_IRQ_HEADER_VALID) | (1UL << RADIOLIB_IRQ_HEADER_ERR))
#define RADIOLIB_IRQ_RX_DEFAULT_MASK  ((1UL << RADIOLIB_IRQ_RX_DONE))

// SX126x family (SX126x_commands.h)
#define RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED  0b0000000000000100
#define RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID    0b0000000000001000
#define RADIOLIB_SX126X_IRQ_HEADER_VALID       0b0000000000010000
#define RADIOLIB_SX126X_IRQ_HEADER_ERR         0b0000000000100000
#define RADIOLIB_SX126X_RX_TIMEOUT_INF         0xFFFFFF
#define RADIOLIB_SX126X_SYNC_WORD_PRIVATE      0x12
#define RADIOLIB_SX126X_REG_RX_GAIN            0x08AC
#define RADIOLIB_SX126X_RX_GAIN_BOOSTED        0x96

// LR11x0 family (LR11x0_commands.h)
#define RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED       (0x01UL << 4)
#define RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID  (0x01UL << 5)
#define RADIOLIB_LR11X0_IRQ_HEADER_ERR              (0x01UL << 6)
#define RADIOLIB_LR11X0_RX_TIMEOUT_INF              (0xFFFFFFUL)
#define RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE      (0x12)

// LR2021 (LR2021_commands.h)
#define RADIOLIB_LR2021_IRQ_PREAMBLE_DETECTED   (0x01UL << 5)
#define RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID   (0x01UL << 6)
#define RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR  (0x01UL << 9)
#define RADIOLIB_LR2021_RX_TIMEOUT_INF          (0xFFFFFFUL)
#define RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE  (0x12)

// Formats a call and its arguments for the call logs below, so a test can pin
// which calls a chip class makes, with which arguments, in which order.
inline std::string stubArg(double v) { char b[32]; snprintf(b, sizeof(b), "%g", v); return b; }
inline std::string stubArg(float v) { return stubArg((double)v); }
template <typename T> std::string stubArg(T v) { return std::to_string(v); }
template <typename... A> std::string stubCall(const char* name, A... a) {
  std::string s = name;
  s += "(";
  const char* sep = "";
  ((s += sep, s += stubArg(a), sep = ","), ...);
  return s + ")";
}

class SPIClass {
public:
  std::vector<std::string> calls;
  template <typename... A> void begin(A... a) { calls.push_back(stubCall("begin", a...)); }
  template <typename... A> void setPins(A... a) { calls.push_back(stubCall("setPins", a...)); }
};

class Module {
public:
  Module() { }
};
using STM32WLx_Module = Module;
using SX1262_Module = Module;

// One base for every chip: the IRQ register is a plain field a test sets, and
// every clear the chip class asks for is recorded and applied to it.
class StubChip {
public:
  explicit StubChip(Module*) { }
  virtual ~StubChip() = default;

  uint32_t irq = 0;
  std::vector<uint32_t> clears;

  struct RxCall { uint32_t timeout, flags, mask; size_t len; };
  std::vector<RxCall> rx_calls;

  float freqMHz = 869.525f;
  uint8_t spreadingFactor = 9;

  // SX126x and LR2021 names
  uint32_t getIrqFlags() { return irq; }
  int16_t clearIrqFlags(uint32_t mask) { clears.push_back(mask); irq &= ~mask; return RADIOLIB_ERR_NONE; }
  // LR11x0 names
  uint32_t getIrqStatus() { return irq; }
  int16_t clearIrqState(uint32_t mask) { clears.push_back(mask); irq &= ~mask; return RADIOLIB_ERR_NONE; }

  virtual int16_t startReceive() { return RADIOLIB_ERR_NONE; }
  int16_t startReceive(uint32_t timeout, uint32_t flags, uint32_t mask, size_t len) {
    rx_calls.push_back({timeout, flags, mask, len});
    return RADIOLIB_ERR_NONE;
  }

  virtual size_t getPacketLength(bool) { return 0; }
  virtual int16_t setOutputPower(int8_t) { return RADIOLIB_ERR_NONE; }
  int16_t setOutputPower(int8_t, bool) { return RADIOLIB_ERR_NONE; }
  virtual int16_t setFrequency(float) { return RADIOLIB_ERR_NONE; }

  template <typename... A> int16_t begin(A...) { return RADIOLIB_ERR_NONE; }
  template <typename... A> int16_t standby(A...) { return RADIOLIB_ERR_NONE; }
  // The configuration calls std_init() makes, in order, with their arguments.
  std::vector<std::string> calls;
  // Register contents readRegister() returns and writeRegister() changes.
  std::map<uint16_t, uint8_t> regs;

  template <typename... A> int16_t setCRC(A... a) { calls.push_back(stubCall("setCRC", a...)); return RADIOLIB_ERR_NONE; }
  template <typename... A> int16_t setCurrentLimit(A... a) { calls.push_back(stubCall("setCurrentLimit", a...)); return RADIOLIB_ERR_NONE; }
  template <typename... A> int16_t setDio2AsRfSwitch(A... a) { calls.push_back(stubCall("setDio2AsRfSwitch", a...)); return RADIOLIB_ERR_NONE; }
  template <typename... A> int16_t setRxBoostedGainMode(A... a) { calls.push_back(stubCall("setRxBoostedGainMode", a...)); return RADIOLIB_ERR_NONE; }
  template <typename... A> void setRfSwitchPins(A... a) { calls.push_back(stubCall("setRfSwitchPins", a...)); }
  int16_t readRegister(uint16_t addr, uint8_t* data, size_t len) {
    calls.push_back(stubCall("readRegister", addr, len));
    for (size_t i = 0; i < len; i++) data[i] = regs[addr + i];
    return RADIOLIB_ERR_NONE;
  }
  int16_t writeRegister(uint16_t addr, const uint8_t* data, size_t len) {
    std::string c = stubCall("writeRegister", addr, len);
    for (size_t i = 0; i < len; i++) { regs[addr + i] = data[i]; c += ":" + std::to_string(data[i]); }
    calls.push_back(c);
    return RADIOLIB_ERR_NONE;
  }
  template <typename... A> int16_t setRegulatorDCDC(A...) { return RADIOLIB_ERR_NONE; }
  template <typename... A> int16_t checkOutputPower(A...) { return RADIOLIB_ERR_NONE; }
  template <typename... A> int16_t explicitHeader(A...) { return RADIOLIB_ERR_NONE; }
  // Distinct values, so a debug line that prints them is pinned with them.
  uint8_t getStatus() { return 0xA5; }
  uint16_t getDeviceErrors() { return 0x0120; }
};

// The SX126x begin() with RadioLib's parameters and defaults (SX1262.h, SX1268.h
// and LLCC68.h at the pinned commit all default useRegulatorLDO to false). Each
// call is logged; the results a test queues are returned in order, then
// RADIOLIB_ERR_NONE.
class SX126x : public StubChip {
public:
  using StubChip::StubChip;
  std::vector<int16_t> begin_results;
  int16_t begin(float freq = 434.0, float bw = 125.0, uint8_t sf = 9, uint8_t cr = 7, uint8_t syncWord = RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                int8_t power = 10, uint16_t preambleLength = 8, float tcxoVoltage = 1.6, bool useRegulatorLDO = false) {
    calls.push_back(stubCall("begin", freq, bw, sf, cr, syncWord, power, preambleLength, tcxoVoltage, useRegulatorLDO));
    if (begin_results.empty()) return RADIOLIB_ERR_NONE;
    int16_t r = begin_results.front();
    begin_results.erase(begin_results.begin());
    return r;
  }
};
class SX1262 : public SX126x { public: using SX126x::SX126x; };
class SX1268 : public SX126x { public: using SX126x::SX126x; };
class LLCC68 : public SX1262 { public: using SX1262::SX1262; };
class STM32WLx : public SX1262 { public: using SX1262::SX1262; };
class LR11x0 : public StubChip { public: using StubChip::StubChip; };
class LR1110 : public LR11x0 { public: using LR11x0::LR11x0; };
class LR1121 : public LR11x0 { public: using LR11x0::LR11x0; };
class LR2021 : public StubChip {
public:
  using StubChip::StubChip;
  uint8_t irqDioNum = 0;
};
