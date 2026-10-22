#pragma once

#include "CustomSTM32WLx.h"
#include "CustomSX126xWrapper.h"
#include <math.h>

// No boosted gain override: the reset passes the base class's false, as before.
class CustomSTM32WLxWrapper : public CustomSX126xWrapper<CustomSTM32WLx> {
public:
  CustomSTM32WLxWrapper(CustomSTM32WLx& radio, mesh::MainBoard& board) : CustomSX126xWrapper(radio, board) { }
};
