#include "Display.h"
#include "ArcadeDisplay.h"
#include <U8x8lib.h>
#include <Wire.h>
#include <algorithm>
#include <bitset>
#include <cstdio>
#include <cstring>

namespace principal {
namespace display {
namespace {
struct Panel {
  u8x8_t device{};
  uint8_t address;
  bool online = false;
  uint8_t clearChunk = 32;
  uint8_t error = 0;
  std::uint32_t lastProbe = 0;
};
Panel matchPanel{{}, board::OLED_ADDRESS};
Panel boardPanel{{}, board::BOARD_OLED_ADDRESS};
char shown[4][17]{};
constexpr uint8_t rows[] = {0, 2, 5, 7};

// Wire belongs to the matrix. Use its pins, clock and timeout, and stop at the
// first failed transaction (the stock Arduino callback ignores I2C errors).
uint8_t transfer(u8x8_t* device, uint8_t msg, uint8_t size, void* data) {
  Panel& panel = device == &boardPanel.device ? boardPanel : matchPanel;
  if (!panel.online) return 0;
  switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
      Wire.beginTransmission(panel.address);
      break;
    case U8X8_MSG_BYTE_SEND:
      if (Wire.write(static_cast<uint8_t*>(data), size) != size) {
        Wire.endTransmission();
        panel.error = 1;
        panel.online = false;
      }
      break;
    case U8X8_MSG_BYTE_END_TRANSFER:
      panel.error = Wire.endTransmission();
      panel.online = panel.error == 0;
      break;
  }
  return panel.online;
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
// Four-wire modules have no reset pin. Retries are spaced well beyond the
// power-up delay; the driver's three 100 ms reset-pin waits would stall scans.
uint8_t noResetDelay(u8x8_t*, uint8_t, uint8_t, void*) { return 1; }

void initialise(Panel& panel, bool boot) {
  auto& oled = panel.device;
  Wire.beginTransmission(panel.address);
  panel.error = Wire.endTransmission();
  panel.online = panel.error == 0;
  if (!panel.online) return;
  u8x8_Setup(&oled, u8x8_d_ssd1315_128x64_noname, u8x8_cad_ssd13xx_fast_i2c,
             transfer, boot ? u8x8_gpio_and_delay_arduino : noResetDelay);
  u8x8_SetPin_HW_I2C(&oled, U8X8_PIN_NONE, U8X8_PIN_NONE, U8X8_PIN_NONE);
  u8x8_SetI2CAddress(&oled, panel.address << 1);
  u8x8_InitDisplay(&oled);
  u8x8_SetFont(&oled, u8x8_font_chroma48medium8_r);
  panel.clearChunk = 0;
  if (&panel == &matchPanel) std::memset(shown, 0, sizeof(shown));
  else arcade::reset();
}

bool clearNext(Panel& panel) {
  if (panel.clearChunk >= 32) return false;
  uint8_t blank[32]{};
  const uint8_t chunk = panel.clearChunk++;
  u8x8_DrawTile(&panel.device, 4 * (chunk % 4), chunk / 4, 4, blank);
  if (panel.clearChunk == 32 && panel.online) u8x8_SetPowerSave(&panel.device, 0);
  return true;
}

void coordinate(uint64_t bit, char* name) {
  uint8_t square = 0;
  while (!(bit & (uint64_t{1} << square))) ++square;
  name[0] = 'A' + square % 8;
  name[1] = board::squareRank(square);
  name[2] = 0;
}

bool drawMatch(const BoardApp& app) {
  using namespace chessboard;
  auto& oled = matchPanel.device;
  const auto& game = app.game();
  const bool finished = app.record().finished();
  const auto& san = app.record().lastSan();
  const char* headline = app.game().position().sideToMove() == Color::White ? "VEZ: BRANCAS" : "VEZ: PRETAS";
  const char* detail = stateText(app);
  const char* focus = san.empty() ? "JOGUE" : san.c_str();
  const char* footer = "Sua jogada";
  char count[9];
  char preview[9]{};
  std::string instruction;
  if (finished) {
    switch (game.ending().result) {
      case GameResult::WhiteWin: headline = "BRANCAS VENCEM"; focus = "1-0"; break;
      case GameResult::BlackWin: headline = "PRETAS VENCEM"; focus = "0-1"; break;
      case GameResult::Draw: headline = "EMPATE"; focus = "1/2-1/2"; break;
      default: headline = "FIM DE PARTIDA"; focus = san.empty() ? "*" : san.c_str(); break;
    }
    footer = app.saved() ? "PGN salvo no PC" : "PGN pendente";
  } else if (!app.readingAvailable() || app.fault() != RecordStatus::Ok) {
    focus = "--";
    footer = "Consulte o PC";
  } else if (app.suggestedMove().from != kInvalidSquare && game.state() == GameState::Tracking) {
    instruction = app.suggestedCoordinates();
    headline = "MAQUINA / PRETAS";
    focus = instruction.c_str();
    detail = game.positionStatus().inCheck ? "XEQUE" : "Mova esta peca";
    footer = app.adjustment() == ConfirmationStatus::NoMove ? "Jogue no quadro" : "Ajuste o destino";
  } else if (app.rejecting()) {
    footer = "Reponha as pecas";
  } else if (game.state() == GameState::AwaitingPosition) {
    const auto expected = game.position().occupancy();
    const auto observed = game.attempt().currentOccupancy;
    std::snprintf(count, sizeof(count), "%u/%u", unsigned(std::bitset<64>(observed & expected).count()),
                  unsigned(std::bitset<64>(expected).count()));
    headline = app.record().plyCount() ? "REPONHA AS PECAS" : "MONTE AS PECAS";
    focus = count;
    detail = "Casas corretas";
    footer = (observed & ~expected) ? "Retire as extras" : "Posicione pecas";
  } else if (game.state() == GameState::AwaitingPromotion) {
    footer = "Escolha Q R B N";
  } else {
    const auto expected = game.position().occupancy();
    const auto observed = app.sensorOccupancy();
    const auto removed = expected & ~observed;
    const auto added = observed & ~expected;
    if (removed || added) {
      const bool singleOrigin = removed && !(removed & (removed - 1));
      const bool singleTarget = added && !(added & (added - 1));
      if (singleOrigin && (!added || singleTarget)) {
        coordinate(removed, preview);
        if (added) {
          preview[2] = '>';
          coordinate(added, preview + 3);
        }
        focus = preview;
      } else {
        focus = "REVISAR";
      }
      detail = game.positionStatus().inCheck ? "XEQUE" : "Leitura da mesa";
      footer = "Lendo jogada";
    }
    if (app.adjustment() == ConfirmationStatus::Ambiguous) {
      detail = "Erga e recoloque";
      footer = "a peca no alvo";
    } else if (app.adjustment() != ConfirmationStatus::NoMove) {
      footer = "Ajuste as pecas";
    }
  }
  const char* lines[] = {headline, focus, detail, footer};
  // Limit each pass to four 8x8 tiles, including the enlarged central glyph.
  for (unsigned row = 0; row < 4; ++row) {
    const bool large = row == 1;
    const unsigned width = large ? 8 : 16;
    const unsigned step = large ? 1 : 4;
    char line[17];
    std::memset(line, ' ', width);
    const size_t length = std::min<size_t>(width, std::strlen(lines[row]));
    std::memcpy(line + (width - length) / 2, lines[row], length);
    for (unsigned x = 0; x < width; x += step) {
      if (std::memcmp(shown[row] + x, line + x, step) == 0) continue;
      u8x8_SetInverseFont(&oled, row == 0);
      if (large) {
        u8x8_Draw2x2Glyph(&oled, 2 * x, rows[row], line[x]);
      } else {
        char chunk[5]{};
        std::memcpy(chunk, line + x, step);
        u8x8_DrawString(&oled, x, rows[row], chunk);
      }
      std::memcpy(shown[row] + x, line + x, step);
      return true;
    }
  }
  return false;
}

} // namespace

void begin() {
  initialise(matchPanel, true);
  initialise(boardPanel, true);
}

bool available() { return matchPanel.online; }
bool boardAvailable() { return boardPanel.online; }
uint8_t matchError() { return matchPanel.error; }
uint8_t boardError() { return boardPanel.error; }

void update(const BoardApp& app, std::uint32_t now) {
  static std::uint32_t updated = 0;
  static bool boardTurn = false;
  if (now - updated < 2) return;
  updated = now;
  for (Panel* panel : {&matchPanel, &boardPanel}) {
    if (!panel->online && now - panel->lastProbe >= 3000) {
      panel->lastProbe = now;
      initialise(*panel, false);
      return;
    }
  }
  boardTurn = !boardTurn;
  if (matchPanel.online && (!boardTurn || !boardPanel.online) &&
      (clearNext(matchPanel) || drawMatch(app))) return;
  if (boardPanel.online && !clearNext(boardPanel)) arcade::update(boardPanel.device, app, now);
}
}
}
