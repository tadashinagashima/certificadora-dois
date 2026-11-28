#include "ArcadeDisplay.h"
#include <algorithm>
#include <bitset>
#include <cstdio>
#include <cstring>

namespace principal {
namespace arcade {
using namespace chessboard;

void Frame::dot(int x, int y) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64) pages[y / 8][x] |= uint8_t(1u << (y % 8));
}

void Frame::text(const char* value, int x, int y, int scale) {
  static u8x8_t font{};
  u8x8_SetFont(&font, u8x8_font_chroma48medium8_r);
  for (; *value && x < 128; ++value, x += 8 * scale) {
    if (x + 8 * scale <= 0) continue;
    uint8_t glyph[8];
    u8x8_get_glyph_data(&font, *value, glyph, 0);
    for (int col = 0; col < 8; ++col)
      for (int row = 0; row < 8; ++row)
        if (glyph[col] & (1 << row))
          for (int dx = 0; dx < scale; ++dx)
            for (int dy = 0; dy < scale; ++dy)
              dot(x + scale * col + dx, y + scale * row + dy);
  }
}

void Frame::center(const char* value, int y, int scale) {
  text(value, (128 - int(std::strlen(value)) * 8 * scale) / 2, y, scale);
}

namespace {
constexpr uint16_t pawn[]  = {0x060,0x0f0,0x0f0,0x060,0x1f8,0x0f0,0x060,0x0f0,0x1f8,0x3fc,0,0x7fe};
constexpr uint16_t crown[] = {0,0x402,0x462,0x6f6,0x7fe,0x3fc,0x3fc,0x1f8,0,0x3fc,0x3fc,0};
constexpr uint16_t tower[] = {0x492,0x7fe,0x7fe,0x3fc,0x1f8,0x1f8,0x1f8,0x1f8,0x3fc,0x3fc,0,0x7fe};
constexpr uint16_t bolt[]  = {0x01c,0x038,0x070,0x0e0,0x1fc,0x3f8,0x030,0x060,0x0c0,0x180,0x100,0};

void sprite(Frame& panel, int x, int y, const uint16_t* shape) {
  for (int row = 0; row < 12; ++row)
    for (int col = 0; col < 12; ++col)
      if (shape[row] & (1 << (11 - col)))
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) panel.dot(x + col*2 + dx, y + row*2 + dy);
}

void replay(Frame& left, Frame& right, const BoardApp& app, unsigned age) {
  const auto& san = app.record().lastSan();
  const bool castle = san.compare(0, 3, "O-O") == 0;
  const auto promo = san.find('=');
  const bool ep = app.lastEnPassant();
  const bool check = app.game().positionStatus().inCheck;
  const bool mate = app.game().ending().reason == EndReason::Checkmate;
  const bool capture = san.find('x') != std::string::npos;
  const char* title = castle ? "ROQUE" : ep ? "EN PASSANT" : promo != san.npos ? "PROMOCAO" :
                      mate ? "XEQUE-MATE" : check ? "XEQUE" : capture ? "CAPTURA" : "LANCE ACEITO";
  left.center(title, 2);
  right.center(mate ? "XEQUE-MATE" : check ? "XEQUE" : "REGISTRADO", 2);
  left.center(castle ? "REI + TORRE" : ep ? "CAPTURA AO LADO" : promo != san.npos ? "NOVA PECA" : ">>>", 53);
  right.center(san.c_str(), 47, san.size() <= 8 ? 2 : 1);

  // The piece crosses the bezel. A castle moves a second sprite; the bypassed
  // pawn disappears behind its captor. The promotion changes at the last rank.
  const int phase = std::min(age, 950u) * 170 / 950;
  const int x = 16 + phase, y = 20 - (phase < 85 ? phase : 170 - phase) / 10;
  const auto* piece = castle || mate ? crown : check && !ep && promo == san.npos ? bolt : pawn;
  if (promo != san.npos && x >= 128) {
    // Use the SAN symbol for all four possible promotion choices.
    char promoted[] = {san[promo + 1], 0};
    right.text(promoted, x - 128, y, 3);
  } else {
    sprite(left, x, y, piece);
    sprite(right, x - 128, y, piece);
  }
  if (castle) {
    sprite(left, x - 30, 21, tower);
    sprite(right, x - 158, 21, tower);
  }
  if (ep && x < 112) sprite(left, 101, 28, pawn);
  if ((capture || check || mate) && x > 150) {
    const int radius = 4 + int(age / 45 % 15);
    for (int sign : {-1, 1}) {
      right.dot(x - 116 + sign * radius, 18 - radius / 2);
      right.dot(x - 116 + sign * radius, 34 + radius / 2);
    }
  }
}

std::string firstSquare(Occupancy mask) {
  for (uint8_t i = 0; i < 64; ++i)
    if (mask & (Occupancy{1} << i)) return {char('A' + i % 8), board::squareRank(i)};
  return {};
}

void border(Frame& panel, unsigned phase) {
  for (int x = 0; x < 128; ++x)
    if ((x + phase) % 14 < 3) { panel.dot(x, 13); panel.dot(127-x, 46); }
}
} // namespace

void render(Frame& left, Frame& right, const BoardApp& app, std::uint32_t now) {
  left = {};
  right = {};
  const auto& game = app.game();
  const auto& record = app.record();
  const auto* lesson = app.tutorial();
  const auto& san = record.lastSan();
  const auto ply = record.plyCount();
  static std::size_t lastPly = 0;
  static std::uint32_t sceneAt = 0;
  if (lastPly != ply) { lastPly = ply; sceneAt = now; }

  const bool finished = record.finished();
  if (app.fault() != RecordStatus::Ok || (!app.readingAvailable() && !finished)) {
    left.center(app.fault() != RecordStatus::Ok ? "REGISTRO" : "SENSORES", 24, 2);
    right.center("Consulte o PC", 24);
    return;
  }
  const auto expected = game.position().occupancy();
  const auto observed = app.sensorOccupancy();
  if (game.state() == GameState::AwaitingPosition) {
    const auto reason = app.restoreReason();
    const bool restore = reason != ConfirmationStatus::NoMove;
    const char* title = lesson ? lesson->title : "PREPARACAO";
    if (restore) {
      switch (reason) {
        case ConfirmationStatus::Ambiguous: title = "LANCE AMBIGUO"; break;
        case ConfirmationStatus::NoLegalMatch: title = "LANCE ILEGAL"; break;
        case ConfirmationStatus::UnexpectedMove: title = "LANCE DIFERENTE"; break;
        case ConfirmationStatus::UnreliableInput: title = "LEITURA PERDIDA"; break;
        default: title = "LANCE BLOQUEADO"; break;
      }
      border(left, now / 50);
    }
    left.center(title, 2);
    left.center(restore ? "REPONHA" : "MONTE", 21, 2);
    left.center(restore ? "Posicao anterior" : "Veja mapa no PC", 53);
    char count[16];
    std::snprintf(count, sizeof(count), "%u / %u", unsigned(std::bitset<64>(observed & expected).count()),
                  unsigned(std::bitset<64>(expected).count()));
    right.center("CASAS OCUPADAS", 2);
    right.center(count, 21, 2);
    const auto extra = observed & ~expected;
    const auto hint = extra ? "Retire de " + firstSquare(extra) : "Ocupe " + firstSquare(expected & ~observed);
    right.center(hint.c_str(), 53);
    return;
  }
  if (ply && now - sceneAt < 1300 && game.state() != GameState::AwaitingPromotion) {
    replay(left, right, app, now - sceneAt);
    return;
  }
  if (finished) {
    const bool complete = lesson && ply == lesson->count;
    const auto end = game.ending();
    const char* reason = complete ? "LICAO CONCLUIDA" : end.reason == EndReason::Checkmate ? "XEQUE-MATE" :
      end.reason == EndReason::Stalemate ? "AFOGAMENTO" : end.reason == EndReason::InsufficientMaterial ?
      "MATERIAL INSUF." : end.reason == EndReason::Resignation ? "ABANDONO" : "FIM DE PARTIDA";
    left.center(reason, 4);
    sprite(left, 52, 23, crown);
    right.center(app.saved() ? "PGN SALVO" : "PGN PENDENTE", 4);
    const char* result = end.result == GameResult::WhiteWin ? "1-0" : end.result == GameResult::BlackWin ?
                         "0-1" : end.result == GameResult::Draw ? "1/2-1/2" : "*";
    right.center(complete ? san.c_str() : result, 23, 2);
    right.center(lesson ? "Repetir no PC" : "Nova mesa no PC", 53);
    return;
  }
  if (game.state() == GameState::AwaitingPromotion) {
    left.center("ULTIMA FILEIRA", 2);
    sprite(left, 30, 22, pawn); sprite(left, 74, 22, crown);
    right.center("PROMOCAO", 2);
    right.center(game.selectedPromotion() == Promotion::None ? "Q R B N" : "ENTER", 22, 2);
    right.center(lesson ? "Use promote R" : "Escolha no PC", 53);
    return;
  }
  const auto side = game.position().sideToMove();
  const char* color = side == Color::White ? "BRANCAS" : "PRETAS";
  left.center(lesson ? lesson->title : game.positionStatus().inCheck ? "XEQUE" : "VEZ DE", 2);
  border(left, now / 85);
  const auto& name = record.playerName(side);
  const std::string player = app.computerMode() && side == Color::Black ? "MAQUINA" :
                             name == "?" ? color : name;
  if (player.size() <= 8) {
    const int center = (128 - int(player.size()) * 16) / 2;
    const unsigned t = (now - sceneAt) % 3600;
    const int entrance = t < 380 ? (128 - center) * int(380 - t) / 380 : 0;
    const int exit = t > 3220 ? (128 - center) * int(t - 3220) / 380 : 0;
    left.text(player.c_str(), center + entrance - exit, 23, 2);
  } else {
    const int travel = int(player.size()) * 16 + 128;
    left.text(player.c_str(), 128 - int((now / 15) % travel), 23, 2);
  }
  left.center(game.positionStatus().inCheck ? "XEQUE" : lesson ? "AULA GUIADA  >>>" : "JOGADA       >>>", 53);
  std::string focus = app.suggestedCoordinates();
  const bool guided = !focus.empty();
  if (!guided) {
    const auto removed = expected & ~observed, added = observed & ~expected;
    if (removed && !(removed & (removed - 1)) && !(added & (added - 1))) {
      focus = firstSquare(removed);
      if (added) focus += ">" + firstSquare(added);
    } else focus = observed != expected ? "EM CURSO" : san.empty() ? "JOGUE" : san;
  }
  right.center(lesson ? app.step()->cue : guided ? "MOVA NO QUADRO" : observed != expected ? "LEITURA DA MESA" : "TABULEIRO", 2);
  right.center(focus.c_str(), 22, focus.size() <= 8 ? 2 : 1);
  right.center("Enter confirma", 53);
}

} // namespace arcade
} // namespace principal
