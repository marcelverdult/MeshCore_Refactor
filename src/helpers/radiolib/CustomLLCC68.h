#pragma once

#include "CustomSX126x.h"

// LLCC68 has never had the SX1262 only parts of std_init(): it ignores
// SX126X_USE_REGULATOR_LDO and SX126X_REGISTER_PATCH and prints no init debug
// lines. Giving it any of them is a behaviour change of its own.
struct CustomLLCC68Traits {
  static constexpr bool regulatorOption = false;
  static constexpr bool registerPatch = false;
  static constexpr bool initDebug = false;
  static const char* name() { return "LLCC68"; }
};

class CustomLLCC68 : public CustomSX126x<LLCC68, CustomLLCC68Traits> {
  public:
    CustomLLCC68(Module *mod) : CustomSX126x<LLCC68, CustomLLCC68Traits>(mod) { }
};
