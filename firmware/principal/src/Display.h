#pragma once
#include "BoardApp.h"

namespace principal {
namespace display {
void begin();
bool available();
void update(const BoardApp& app, std::uint32_t now);
}
}
