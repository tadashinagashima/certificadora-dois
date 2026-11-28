#pragma once
#include "BoardApp.h"

namespace principal {
namespace display {
void begin();
bool available();
bool boardAvailable();
uint8_t matchError();
uint8_t boardError();
void update(const BoardApp& app, std::uint32_t now);
}
}
