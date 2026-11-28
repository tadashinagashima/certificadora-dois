#pragma once

#include "BoardApp.h"
#include <U8x8lib.h>

namespace principal {
namespace arcade {

struct Frame {
  uint8_t pages[8][128]{};
  void dot(int x, int y);
  void text(const char* value, int x, int y, int scale = 1);
  void center(const char* value, int y, int scale = 1);
};

// Both views are composed from the same game state and animation clock.
void render(Frame& left, Frame& right, const BoardApp& app, std::uint32_t now);

} // namespace arcade
} // namespace principal
