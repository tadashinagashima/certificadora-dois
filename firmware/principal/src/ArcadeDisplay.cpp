#include "ArcadeDisplay.h"
#include <algorithm>
#include <bitset>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace principal {
namespace arcade {
using namespace chessboard;

void Frame::dot(int x, int y) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64) pages[y / 8][x] |= uint8_t(1u << (y % 8));
}

void Frame::text(const char* value, int x, int y, int scale, bool marquee) {
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
              dot(x + scale * col + dx, y + (scale + (marquee ? 1 : 0)) * row + dy);
  }
}

void Frame::center(const char* value, int y, int scale) {
  text(value, (128 - int(std::strlen(value)) * 8 * scale) / 2, y, scale);
}

namespace {
// 12 x 12 silhouettes. The notches remain legible on the two-colour panel.
using Glyph = uint16_t[12];
constexpr Glyph pawn   = {0x060,0x0f0,0x0f0,0x060,0x1f8,0x0f0,0x060,0x0f0,0x1f8,0x3fc,0,0x7fe};
constexpr Glyph knight = {0x110,0x3b0,0x7f8,0xed8,0xffc,0xc7c,0x0f8,0x1f8,0x3f8,0x3fc,0,0x7fe};
constexpr Glyph bishop = {0x060,0x0f0,0x1e8,0x3dc,0x3bc,0x1f8,0x0f0,0x1f8,0x0f0,0x3fc,0,0x7fe};
constexpr Glyph tower  = {0x492,0x7fe,0x7fe,0x3fc,0x1f8,0x198,0x198,0x1f8,0x3fc,0x3fc,0,0x7fe};
constexpr Glyph queen  = {0x462,0xe67,0x666,0x7fe,0x3fc,0x1f8,0x1f8,0x0f0,0x1f8,0x3fc,0,0x7fe};
constexpr Glyph king   = {0x060,0x1f8,0x060,0x3fc,0x7fe,0x666,0x3fc,0x1f8,0x0f0,0x3fc,0,0x7fe};

const uint16_t* silhouette(Piece piece) {
  static const uint16_t* const shapes[] = {pawn, knight, bishop, tower, queen, king};
  return piece == Piece::Empty ? pawn : shapes[(unsigned(piece) - 1) % 6];
}

void sprite(Frame& panel, int x, int y, const uint16_t* shape, int scale = 2) {
  for (int row = 0; row < 12; ++row)
    for (int col = 0; col < 12; ++col)
      if (shape[row] & (1 << (11 - col)))
        for (int dy = 0; dy < scale; ++dy)
          for (int dx = 0; dx < scale; ++dx) panel.dot(x + col*scale + dx, y + row*scale + dy);
}

void header(Frame& f, const char* title) {
  f.center(title, 0);
  for (unsigned x = 0; x < 128; ++x) f.pages[0][x] ^= 0xff;
  for (int x = 4; x < 124; ++x) f.dot(x, 10);
}

// The open strip between the header and footer spans both physical displays.
struct Stage {
  Frame& left;
  Frame& right;
  void pixel(int x, int y) {
    if (y < 12 || y > 48) return;
    if (x < 128) left.dot(x, y); else right.dot(x - 128, y);
  }
  void piece(int x, int y, const uint16_t* shape) {
    sprite(left, x, y, shape);
    sprite(right, x - 128, y, shape);
  }
  void burst(int x, int y, unsigned t) {
    constexpr int rays[][2] = {{-3,0},{-2,-2},{0,-3},{2,-2},{3,0},{2,2},{0,3},{-2,2}};
    if (t >= 440) return;
    for (const auto& ray : rays) {
      const int r = 1 + int(t) / 35;
      pixel(x + ray[0]*r, y + ray[1]*r);
      pixel(x + ray[0]*(r+1), y + ray[1]*(r+1));
    }
  }
};

int slide(unsigned age, unsigned begin, unsigned duration, int from, int to) {
  if (age <= begin) return from;
  const int t = int(std::min(age - begin, duration));
  return from + (to-from)*t/int(duration);
}

void replay(Frame& left, Frame& right, const BoardApp& app, unsigned age) {
  const auto& san = app.record().lastSan();
  const Move move = app.lastMove();
  const bool castle = san.compare(0, 3, "O-O") == 0;
  const bool promo = move.promotion != Promotion::None;
  const bool ep = app.lastEnPassant();
  const bool check = app.game().positionStatus().inCheck;
  const bool mate = app.game().ending().reason == EndReason::Checkmate;
  const bool capture = san.find('x') != std::string::npos;
  const auto* shape = silhouette(app.game().position().pieceAt(move.to));
  header(left, castle ? "ROQUE" : ep ? "EN PASSANT" : promo ? "PROMOCAO" : capture ? "CAPTURA" : "LANCE ACEITO");
  header(right, mate ? "XEQUE-MATE" : check ? "XEQUE" : "REGISTRADO");
  Stage stage{left,right};
  for (int x = 0; x < 256; ++x) {
    if ((x + age/25) % 16 < 5) stage.pixel(x, 48);
    if ((x + age/45) % 52 == 0) stage.pixel(x, 18 + x % 15);
  }
  if (castle) {
    // The king crosses the rook, which slides back to the king's inside.
    const bool longSide = move.to % 8 == 2;
    auto lane = [longSide](int x) { return longSide ? 232-x : x; };
    const int x = slide(age, 100, 740, 24, 184);
    const int jump = std::max(0, 16 - std::abs(x - 118) / 4);
    stage.piece(lane(x), 24 - jump/2, king);
    stage.piece(lane(slide(age, 670, 420, 216, 144)), 24, tower);
    left.center(longSide ? "ROQUE LONGO" : "ROQUE CURTO", 53);
  } else if (promo) {
    const int x = slide(age, 60, 760, 20, 180);
    const int gate = 145;
    for (int y = 14; y < 46; ++y) {
      if ((y + age/35) % 9 < 5) { stage.pixel(gate, y); stage.pixel(gate+3, y); }
    }
    stage.piece(x, 23, x + 12 < gate ? pawn : shape);
    if (age >= 590) stage.burst(gate, 30, age - 590);
    left.center("ULTIMA FILEIRA", 53);
  } else {
    const int x = slide(age, 70, 820, 16, 186);
    const int jump = std::max(0, 18 - std::abs(112 - x) / 4);
    stage.piece(x, 23 - jump/2, shape);
    for (int i = 1; i < 5 && x > 24 && x < 184; ++i)
      stage.pixel(x - 4*i, 30 + (i*5)%9);
    if (capture) {
      const int victim = ep ? 111 : 174;
      if (x + 12 < victim) stage.piece(victim, 24, silhouette(app.lastVictim()));
      else stage.burst(victim + 12, 32, (x + 12 - victim)*6 + (age > 890 ? age - 890 : 0));
    }
    left.center(ep ? "CAPTURA AO LADO" : capture ? "PECA CAPTURADA" : "JOGADA ACEITA", 53);
  }
  if (check && age > 900) {
    for (int i = 0; i < 15; ++i) {
      stage.pixel(217 - i/3 + (i > 6 ? 5 : 0), 14 + i);
      stage.pixel(218 - i/3 + (i > 6 ? 5 : 0), 14 + i);
    }
  }
  right.center(san.c_str(), 49, san.size() <= 8 ? 2 : 1);
}

std::string firstSquare(Occupancy mask) {
  for (uint8_t i = 0; i < 64; ++i)
    if (mask & (Occupancy{1} << i)) return {char('A' + i % 8), board::squareRank(i)};
  return {};
}

void boardMap(Frame& f, Occupancy occupied, Occupancy focus, unsigned now) {
  for (unsigned i = 0; i < 64; ++i) {
    const int x = 4 + int(i % 8)*6, y = 13 + (7-int(i / 8))*6;
    const Occupancy bit = Occupancy{1} << i;
    f.dot(x+2, y+2);
    if (occupied & bit)
      for (int dx=1; dx<=3; ++dx) for (int dy=1; dy<=3; ++dy) f.dot(x+dx,y+dy);
    if ((focus & bit) && now % 600 < 430) {
      for (int n=0; n<5; ++n) { f.dot(x+n,y); f.dot(x+n,y+4); f.dot(x,y+n); f.dot(x+4,y+n); }
    }
  }
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
  const auto age = now - app.lastMoveAt();
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
    header(left, title);
    left.center(restore ? "REPONHA" : "MONTE", 21, 2);
    left.center(restore ? "Posicao anterior" : "Veja mapa no PC", 53);
    char count[16];
    std::snprintf(count, sizeof(count), "%u / %u", unsigned(std::bitset<64>(observed & expected).count()),
                  unsigned(std::bitset<64>(expected).count()));
    header(right, "MAPA DA MESA");
    boardMap(right, observed & expected, observed ^ expected, now);
    right.text(count, 55, 17);
    const auto extra = observed & ~expected;
    const auto square = firstSquare(extra ? extra : expected & ~observed);
    right.text(extra ? "RETIRE" : "OCUPE", 60, 31);
    right.text(square.c_str(), 65, 43, 2);
    return;
  }
  if (game.state() == GameState::AwaitingCapture) {
    header(left, "CONFIRA CAPTURA");
    sprite(left, 52, 23 - int(now/80 % 8), pawn);
    left.center("Erga e recoloque", 54);
    const auto targets = game.captureTargets();
    header(right, "PECA NO DESTINO");
    boardMap(right, observed, targets, now);
    const auto others = targets & (targets - 1);
    std::string hint = firstSquare(targets);
    if (others) hint += (others & (others - 1)) ? " ..." : " " + firstSquare(others);
    right.text(hint.c_str(), 61, 23, others ? 1 : 2);
    right.text("ENTER", 60, 48);
    return;
  }
  if (ply && age < 1500 && observed == expected && game.state() != GameState::AwaitingPromotion) {
    replay(left, right, app, age);
    return;
  }
  if (finished) {
    const bool complete = lesson && ply == lesson->count;
    const auto end = game.ending();
    const char* reason = complete ? "LICAO CONCLUIDA" : end.reason == EndReason::Checkmate ? "XEQUE-MATE" :
      end.reason == EndReason::Stalemate ? "AFOGAMENTO" : end.reason == EndReason::InsufficientMaterial ?
      "MATERIAL INSUF." : end.reason == EndReason::Resignation ? "ABANDONO" : "FIM DE PARTIDA";
    header(left, reason);
    sprite(left, 52, 23, queen);
    Stage stage{left,right};
    stage.burst(27, 30, now % 900);
    stage.burst(103, 35, (now + 400) % 900);
    header(right, app.saved() ? "PGN SALVO" : "PGN PENDENTE");
    const char* result = end.result == GameResult::WhiteWin ? "1-0" : end.result == GameResult::BlackWin ?
                         "0-1" : end.result == GameResult::Draw ? "1/2-1/2" : "*";
    right.center(complete ? san.c_str() : result, 23, 2);
    right.center(lesson ? "Repetir no PC" : "Nova mesa no PC", 53);
    return;
  }
  if (game.state() == GameState::AwaitingPromotion) {
    header(left, "ULTIMA FILEIRA");
    sprite(left, 30, 22, pawn); sprite(left, 74, 22, lesson ? tower : queen);
    header(right, "PROMOCAO");
    right.center(game.selectedPromotion() == Promotion::None ? "Q R B N" : "ENTER", 22, 2);
    right.center(lesson ? "Use promote R" : "Escolha no PC", 53);
    return;
  }
  const auto side = game.position().sideToMove();
  const char* color = side == Color::White ? "BRANCAS" : "PRETAS";
  if (const auto* step = app.step()) {
    char progress[16];
    std::snprintf(progress, sizeof(progress), "AULA %u/%u", unsigned(ply + 1), lesson->count);
    header(left, progress);
    boardMap(left, expected, (Occupancy{1} << step->move.from) | (Occupancy{1} << step->move.to), now);
    left.text(color, 58, 20);
    sprite(left, 73, 34, silhouette(game.position().pieceAt(step->move.from)));
    header(right, lesson->title);
    right.center(app.suggestedCoordinates().c_str(), 16, 2);
    right.center(step->cue, 39);
    right.center(app.incomplete() ? "Complete o lance" : "Enter confirma", 54);
    return;
  }
  header(left, game.positionStatus().inCheck ? "XEQUE" : "VEZ DE");
  border(left, now / 85);
  const auto& name = record.playerName(side);
  const std::string player = app.computerMode() && side == Color::Black ? "MAQUINA" :
                             name == "?" ? color : name;
  if (player.size() <= 8) {
    const int center = (128 - int(player.size()) * 16) / 2;
    const unsigned t = age % 3600;
    const int entrance = t < 380 ? (128 - center) * int(380 - t) / 380 : 0;
    const int exit = t > 3220 ? (128 - center) * int(t - 3220) / 380 : 0;
    left.text(player.c_str(), center + entrance - exit, 18, 2, true);
  } else {
    const int travel = int(player.size()) * 16 + 128;
    left.text(player.c_str(), 128 - int((now / 15) % travel), 18, 2, true);
  }
  left.center(game.positionStatus().inCheck ? "PROTEJA O REI" : "JOGADA       >>>", 53);
  std::string focus = app.suggestedCoordinates();
  const bool guided = !focus.empty();
  if (!guided) {
    const auto removed = expected & ~observed, added = observed & ~expected;
    if (removed && !(removed & (removed - 1)) && !(added & (added - 1))) {
      focus = firstSquare(removed);
      if (added) focus += ">" + firstSquare(added);
    } else focus = observed != expected ? "EM CURSO" : san.empty() ? "JOGUE" : san;
  }
  header(right, guided ? "MOVA NO QUADRO" : observed != expected ? "LEITURA DA MESA" : "TABULEIRO");
  right.center(focus.c_str(), 22, focus.size() <= 8 ? 2 : 1);
  right.center(app.incomplete() ? "Complete o lance" : "Enter confirma", 53);
}

} // namespace arcade
} // namespace principal
