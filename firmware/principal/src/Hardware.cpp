#include "Hardware.h"
#include <Arduino.h>
#include <Wire.h>

namespace principal {
namespace {
constexpr std::uint8_t IODIR = 0x00, GPIO = 0x12, OLAT = 0x14;

bool write16(std::uint8_t reg, std::uint16_t value) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(reg);
  Wire.write(static_cast<std::uint8_t>(value));
  Wire.write(static_cast<std::uint8_t>(value >> 8));
  return Wire.endTransmission() == 0;
}
bool read16(std::uint8_t reg, std::uint16_t& value) {
  Wire.beginTransmission(board::MCP_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(board::MCP_ADDRESS, static_cast<std::uint8_t>(2)) != 2) {
    while (Wire.available()) Wire.read();
    return false;
  }
  const std::uint8_t a = Wire.read(), b = Wire.read();
  value = a | (static_cast<std::uint16_t>(b) << 8);
  return true;
}
}

void Hardware::setup() {
  digitalWrite(board::MCP_RESET_PIN, LOW);
  pinMode(board::MCP_RESET_PIN, OUTPUT_OPEN_DRAIN);
  for (auto pin : {board::LED_GREEN, board::LED_RED, board::BUZZER}) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  wireReady_ = Wire.begin(board::SDA_PIN, board::SCL_PIN, board::I2C_HZ);
  Wire.setTimeOut(25);
  initialiseMatrix();
}

bool Hardware::fail() {
  // A hardware reset disables the column drivers even when I2C has failed.
  digitalWrite(board::MCP_RESET_PIN, LOW);
  ready_ = false;
  return false;
}

bool Hardware::initialiseMatrix() {
  if (!wireReady_) return fail();
  digitalWrite(board::MCP_RESET_PIN, LOW);
  delay(2);
  digitalWrite(board::MCP_RESET_PIN, HIGH);
  delay(2);
  // Reset establishes BANK=0 and SEQOP=0. Preload LOW with every pin high-Z.
  if (!write16(IODIR, 0xFFFF) || !write16(OLAT, 0) ||
      !write16(0x02, 0) || !write16(0x04, 0) || !write16(0x0C, 0)) return fail();
  std::uint16_t directions = 0, latches = 0;
  if (!read16(IODIR, directions) || directions != 0xFFFF ||
      !read16(OLAT, latches) || latches != 0) return fail();
  ready_ = true;
  return true;
}

bool Hardware::scan(chessboard::Occupancy& value) {
  if (!ready_) return false;
  chessboard::Occupancy next = 0;
  for (std::uint8_t file = 0; file < 8; ++file) {
    if (!write16(IODIR, 0xFFFF) || !write16(IODIR, board::columnDirection(file))) return fail();
    delayMicroseconds(board::COLUMN_SETTLE_US);
    std::uint16_t levels;
    if (!read16(GPIO, levels)) return fail();
    for (std::uint8_t rank = 0; rank < 8; ++rank)
      if (!(levels & (std::uint16_t{1} << board::ROW_BITS[rank])))
        next |= chessboard::Occupancy{1} << board::squareIndex(file, rank);
  }
  if (!write16(IODIR, 0xFFFF)) return fail();
  value = next; // Only publish a complete scan.
  return true;
}

void Hardware::indicate(Indicators lights) {
  digitalWrite(board::LED_GREEN, lights.green ? HIGH : LOW);
  digitalWrite(board::LED_RED, lights.red ? HIGH : LOW);
  digitalWrite(board::BUZZER, lights.buzzer ? HIGH : LOW);
}

} // namespace principal
