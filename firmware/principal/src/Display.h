#pragma once
#include "BoardApp.h"

namespace principal {
namespace display {
void begin();
bool setFrequency(std::uint32_t hz);
std::uint32_t busFrequency();
bool rightOnline();
bool leftOnline();
uint8_t rightError();
uint8_t leftError();
void update(const BoardApp& app, std::uint32_t now);
}
}
