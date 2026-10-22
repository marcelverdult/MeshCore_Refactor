#pragma once

#include "CustomLR1110.h"
#include "CustomLR11x0Wrapper.h"

class CustomLR1110Wrapper : public CustomLR11x0Wrapper<CustomLR1110> {
public:
  CustomLR1110Wrapper(CustomLR1110& radio, mesh::MainBoard& board) : CustomLR11x0Wrapper(radio, board) { }
};
