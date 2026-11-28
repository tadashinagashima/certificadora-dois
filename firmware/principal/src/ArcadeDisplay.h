#pragma once

#include "BoardApp.h"
#include <U8x8lib.h>

namespace principal {
namespace arcade {
void reset();
void update(u8x8_t& oled, const BoardApp& app, std::uint32_t now);
}
}
