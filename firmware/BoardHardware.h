#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "board_config.h"

namespace board {

// Common electrical interface for the game and the standalone diagnostic.
class Hardware {
 public:
  void setup() {
    digitalWrite(MCP_RESET_PIN, LOW);
    pinMode(MCP_RESET_PIN, OUTPUT_OPEN_DRAIN);
    for (auto pin : {LED_GREEN, LED_RED, BUZZER}) {
      digitalWrite(pin, LOW);
      pinMode(pin, OUTPUT);
    }
    initialiseMatrix();
  }

  bool initialiseMatrix() {
    if (!wireReady_) {
      wireReady_ = Wire.begin(SDA_PIN, SCL_PIN, I2C_HZ);
      if (!wireReady_) return fail("controlador I2C indisponivel");
      Wire.setTimeOut(25);
    }
    digitalWrite(MCP_RESET_PIN, LOW);
    delay(2);
    digitalWrite(MCP_RESET_PIN, HIGH); // Release to the external 10k pull-up.
    delay(2);

    // Reset selects BANK=0 and sequential addressing. Preload LOW with all pins high-Z.
    if (!write16(IODIR, 0xFFFF) || !write16(OLAT, 0) || !write16(IPOL, 0) ||
        !write16(GPINTEN, 0) || !write16(GPPU, 0))
      return fail("configuracao I2C sem confirmacao");
    uint16_t directions, latches;
    if (!read16(IODIR, directions) || directions != 0xFFFF ||
        !read16(OLAT, latches) || latches != 0)
      return fail("verificacao de IODIR/OLAT falhou");
    error_ = nullptr;
    return true;
  }

  // Also used by the diagnostic to hold column a LOW for a voltmeter.
  bool selectColumn(uint8_t file) {
    if (!ready()) return false;
    if (file >= 8 || !write16(IODIR, 0xFFFF) || !write16(IODIR, columnDirection(file)))
      return fail("selecao da coluna falhou");
    return true;
  }

  bool scan(uint64_t& occupancy) {
    uint64_t next = 0;
    for (uint8_t file = 0; file < 8; ++file) {
      if (!selectColumn(file)) return false;
      delayMicroseconds(COLUMN_SETTLE_US);
      uint16_t levels;
      if (!read16(GPIO, levels)) return fail("leitura I2C falhou");
      for (uint8_t rank = 0; rank < 8; ++rank) {
        if (!(levels & (uint16_t{1} << ROW_BITS[rank])))
          next |= uint64_t{1} << squareIndex(file, rank);
      }
    }
    if (!write16(IODIR, 0xFFFF)) return fail("liberacao das colunas falhou");
    occupancy = next; // Publish only a complete scan.
    return true;
  }

  void indicate(Indicators lights) const {
    digitalWrite(LED_GREEN, lights.green ? HIGH : LOW);
    digitalWrite(LED_RED, lights.red ? HIGH : LOW);
    digitalWrite(BUZZER, lights.buzzer ? HIGH : LOW);
  }

  bool ready() const { return error_ == nullptr; }
  const char* error() const { return error_; }

 private:
  enum Register : uint8_t { IODIR = 0x00, IPOL = 0x02, GPINTEN = 0x04,
                            GPPU = 0x0C, GPIO = 0x12, OLAT = 0x14 };

  static bool write16(Register reg, uint16_t value) {
    Wire.beginTransmission(MCP_ADDRESS);
    Wire.write(static_cast<uint8_t>(reg));
    Wire.write(static_cast<uint8_t>(value));
    Wire.write(static_cast<uint8_t>(value >> 8));
    return Wire.endTransmission() == 0;
  }

  static bool read16(Register reg, uint16_t& value) {
    Wire.beginTransmission(MCP_ADDRESS);
    Wire.write(static_cast<uint8_t>(reg));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(MCP_ADDRESS, static_cast<uint8_t>(2)) != 2) {
      while (Wire.available()) Wire.read();
      return false;
    }
    const uint8_t a = Wire.read(), b = Wire.read();
    value = a | (static_cast<uint16_t>(b) << 8);
    return true;
  }

  bool fail(const char* reason) {
    digitalWrite(MCP_RESET_PIN, LOW); // Disable column drivers even if I2C is down.
    error_ = reason;
    return false;
  }

  bool wireReady_ = false;
  const char* error_ = "nao inicializado";
};

} // namespace board
