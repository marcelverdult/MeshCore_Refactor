#pragma once

#include "CustomSX126x.h"

// SX1268 has never had the SX1262 only parts of std_init(): it ignores
// SX126X_USE_REGULATOR_LDO and SX126X_REGISTER_PATCH and prints no init debug
// lines. Giving it any of them is a behaviour change of its own.
struct CustomSX1268Traits {
  static constexpr bool regulatorOption = false;
  static constexpr bool registerPatch = false;
  static constexpr bool initDebug = false;
  static const char* name() { return "SX1268"; }
};

class CustomSX1268 : public CustomSX126x<SX1268, CustomSX1268Traits> {
  public:
    CustomSX1268(Module *mod) : CustomSX126x<SX1268, CustomSX1268Traits>(mod) { }
};
