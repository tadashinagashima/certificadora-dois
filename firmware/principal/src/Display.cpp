#include "Display.h"
#include <U8x8lib.h>
#include <Wire.h>
#include <cstdio>
#include <cstring>

namespace principal {
namespace display {
namespace {
u8x8_t oled;
bool online = false;
char shown[4][17]{};
constexpr uint8_t rows[] = {0, 2, 4, 7};

// Wire belongs to the matrix. Use its pins, clock and timeout, and stop at the
// first failed transaction (the stock Arduino callback ignores I2C errors).
uint8_t transfer(u8x8_t*, uint8_t msg, uint8_t size, void* data) {
  if (!online) return 0;
  switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
      Wire.beginTransmission(board::OLED_ADDRESS);
      break;
    case U8X8_MSG_BYTE_SEND:
      if (Wire.write(static_cast<uint8_t*>(data), size) != size) {
        Wire.endTransmission();
        online = false;
      }
      break;
    case U8X8_MSG_BYTE_END_TRANSFER:
      online = Wire.endTransmission() == 0;
      break;
  }
  return online;
}

const char* stateText(const BoardApp& app) {
  using namespace chessboard;
  if (app.fault() != RecordStatus::Ok) return "Erro de registro";
  if (app.game().state() == GameState::Finished) {
    switch (app.game().ending().reason) {
      case EndReason::Checkmate: return "Xeque-mate";
      case EndReason::Stalemate: return "Afogamento";
      case EndReason::InsufficientMaterial: return "Material insuf.";
      case EndReason::Resignation: return "Abandono";
      default: return "Fim de partida";
    }
  }
  if (!app.readingAvailable()) return "Aguarde sensores";
  if (app.rejecting()) return "Lance rejeitado";
  if (app.game().state() == GameState::AwaitingPosition) return "Reponha as pecas";
  if (app.game().state() == GameState::AwaitingPromotion) return "Promova no PC";
  return app.game().positionStatus().inCheck ? "XEQUE" : "Em jogo";
}
}

void begin() {
  Wire.beginTransmission(board::OLED_ADDRESS);
  online = Wire.endTransmission() == 0;
  if (!online) return;
  u8x8_Setup(&oled, u8x8_d_ssd1315_128x64_noname, u8x8_cad_ssd13xx_fast_i2c,
             transfer, u8x8_gpio_and_delay_arduino);
  u8x8_SetPin_HW_I2C(&oled, U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
  u8x8_SetI2CAddress(&oled, board::OLED_ADDRESS << 1);
  u8x8_InitDisplay(&oled);
  u8x8_ClearDisplay(&oled);
  u8x8_SetPowerSave(&oled, 0);
  u8x8_SetFont(&oled, u8x8_font_chroma48medium8_r);
}

bool available() { return online; }

void update(const BoardApp& app, std::uint32_t now) {
  using namespace chessboard;
  static std::uint32_t updated = 0;
  if (!online || now - updated < 8) return;
  updated = now;
  const bool finished = app.record().finished();
  const char* headline = app.game().position().sideToMove() == Color::White ? "VEZ: BRANCAS" : "VEZ: PRETAS";
  if (finished) {
    switch (app.game().ending().result) {
      case GameResult::WhiteWin: headline = "BRANCAS  1-0"; break;
      case GameResult::BlackWin: headline = "PRETAS   0-1"; break;
      case GameResult::Draw: headline = "EMPATE   1/2-1/2"; break;
      default: headline = "ENCERRADA  *"; break;
    }
  }
  char last[17];
  std::snprintf(last, sizeof(last), "Lance: %.9s", app.record().lastSan().empty() ? "-" : app.record().lastSan().c_str());
  const char* footer = finished ? (app.saved() ? "PGN salvo no PC" : "PGN pendente") : "USB / terminal";
  const char* lines[] = {headline, last, stateText(app), footer};
  // Four glyphs per turn. Pad to erase the tail of longer previous messages.
  for (unsigned row = 0; row < 4; ++row) {
    char line[17];
    std::snprintf(line, sizeof(line), "%-16.16s", lines[row]);
    for (unsigned x = 0; x < 16; x += 4) {
      if (std::memcmp(shown[row] + x, line + x, 4) == 0) continue;
      char chunk[5]{};
      std::memcpy(chunk, line + x, 4);
      u8x8_SetInverseFont(&oled, row == 0);
      u8x8_DrawString(&oled, x, rows[row], chunk);
      std::memcpy(shown[row] + x, chunk, 4);
      return;
    }
  }
}
}
}
