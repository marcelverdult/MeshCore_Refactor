#pragma once

#include "CustomSX126x.h"

// SX1262 is the chip std_init() was written for: it honours
// SX126X_USE_REGULATOR_LDO and SX126X_REGISTER_PATCH and prints its init debug
// lines.
struct CustomSX1262Traits {
  static constexpr bool regulatorOption = true;
  static constexpr bool registerPatch = true;
  static constexpr bool initDebug = true;
  static const char* name() { return "SX1262"; }
};

class CustomSX1262 : public CustomSX126x<SX1262, CustomSX1262Traits> {
  public:
    CustomSX1262(Module *mod) : CustomSX126x<SX1262, CustomSX1262Traits>(mod) { }
};
