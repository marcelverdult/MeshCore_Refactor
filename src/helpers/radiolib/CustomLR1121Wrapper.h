#pragma once

#include "CustomLR1121.h"
#include "CustomLR11x0Wrapper.h"

class CustomLR1121Wrapper : public CustomLR11x0Wrapper<CustomLR1121> {
public:
  CustomLR1121Wrapper(CustomLR1121& radio, mesh::MainBoard& board) : CustomLR11x0Wrapper(radio, board) { }
};
