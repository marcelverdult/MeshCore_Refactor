#pragma once

#include "CustomLR11x0.h"

struct CustomLR1110Traits {
  static const char* name() { return "LR1110"; }
};

class CustomLR1110 : public CustomLR11x0<LR1110, CustomLR1110Traits> {
  public:
    CustomLR1110(Module *mod) : CustomLR11x0<LR1110, CustomLR1110Traits>(mod) { }
};
