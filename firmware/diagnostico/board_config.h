#pragma once

#include <stdint.h>

namespace board {
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t MCP_RESET_PIN = 14;
constexpr uint8_t MCP_ADDRESS = 0x20;
constexpr uint8_t SD_CS = 13;
constexpr uint8_t SD_SCK = 18;
constexpr uint8_t SD_MISO = 19;
constexpr uint8_t SD_MOSI = 23;
constexpr uint8_t BUTTON_CONFIRM = 32;
constexpr uint8_t BUTTON_PROMOTE = 33;
constexpr uint8_t BUTTON_END = 34;
constexpr uint8_t LED_GREEN = 25;
constexpr uint8_t LED_RED = 26;
constexpr uint8_t BUZZER = 27;

// Direct MCP row inputs with external 47k pull-ups to the DevKit 3V3 rail.
// Initial delay; verify row levels and settling on the assembled matrix.
constexpr uint16_t COLUMN_SETTLE_US = 300;

// MCP bit 0..7 = GPA0..7; bit 8..15 = GPB0..7.
// Datasheet revision D: GPA7/GPB7 may not be used to sense inputs.
// IODIR still disables their output drivers when a column is not selected.
constexpr uint8_t COL_BITS[8] = {0, 1, 2, 3, 4, 5, 7, 15};  // a..h
constexpr uint8_t ROW_BITS[8] = {8, 9, 10, 11, 12, 13, 14, 6};  // 1..8
constexpr uint16_t ROW_MASK = 0x7F40;

constexpr uint8_t squareIndex(uint8_t file, uint8_t rank) {
  return rank * 8 + file;  // a1=0, h1=7, a8=56, h8=63
}

constexpr char squareFile(uint8_t index) { return 'a' + index % 8; }
constexpr char squareRank(uint8_t index) { return '1' + index / 8; }

constexpr uint16_t columnDirection(uint8_t file) {
  return static_cast<uint16_t>(~(uint16_t{1} << COL_BITS[file]));
}

constexpr bool wiringIsConsistent() {
  uint16_t used = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    const uint16_t column = uint16_t{1} << COL_BITS[i];
    const uint16_t row = uint16_t{1} << ROW_BITS[i];
    if ((used & (column | row)) || (column & row)) return false;
    if (ROW_BITS[i] == 7 || ROW_BITS[i] == 15) return false;
    if ((columnDirection(i) & ROW_MASK) != ROW_MASK) return false;
    const uint16_t driven = static_cast<uint16_t>(~columnDirection(i));
    if (!driven || (driven & (driven - 1))) return false;
    used |= column | row;
  }
  return used == 0xFFFF;
}

constexpr bool allSquaresRoundTrip() {
  for (uint8_t file = 0; file < 8; ++file) {
    for (uint8_t rank = 0; rank < 8; ++rank) {
      const uint8_t index = squareIndex(file, rank);
      if (squareFile(index) != 'a' + file || squareRank(index) != '1' + rank) return false;
    }
  }
  return true;
}

static_assert(wiringIsConsistent(), "MCP: duplicate pin, unsupported row input or multiple driven columns");
static_assert(allSquaresRoundTrip(), "Square mapping must round trip for all 64 squares");
static_assert(squareIndex(0, 0) == 0 && squareIndex(7, 7) == 63, "a1/h8 orientation mismatch");
}  // namespace board
