#include "ArcadeDisplay.h"
#include <algorithm>
#include <cstring>
#include <utility>

namespace principal {
namespace arcade {
namespace {
using namespace chessboard;
constexpr int width = 128, height = 64, chunkBytes = 32;
constexpr unsigned chunkCount = width * height / 8 / chunkBytes;
struct Animation {
  uint8_t frame[height / 8][width]{}, shown[height / 8][width]{};
  unsigned chunk = chunkCount;
  std::uint32_t frameAt = 0, sceneAt = 0;
  std::size_t plies = 0;
  std::string message;
  bool celebrating = false;
} screen;

void dot(int x, int y) {
  if (x >= 0 && x < width && y >= 0 && y < height)
    screen.frame[y / 8][x] |= uint8_t(1u << (y % 8));
}

// A narrow moving highlight fills the spaces between the virtual LEDs.
void lettering(u8x8_t& oled, const char* text, int x, int y,
               int scaleX, int scaleY, int highlight) {
  for (; *text && x < width; ++text) {
    if (x + 8 * scaleX > 0) {
      uint8_t glyph[8];
      u8x8_get_glyph_data(&oled, *text, glyph, 0);
      for (int col = 0; col < 8; ++col) {
        for (int row = 0; row < 8; ++row) {
          if (!(glyph[col] & (1u << row))) continue;
          const int left = x + col * scaleX, top = y + row * scaleY;
          const int diagonal = left + top / 3 - highlight;
          const bool lit = diagonal >= 0 && diagonal < 17;
          for (int dx = 0; dx < scaleX; ++dx)
            for (int dy = 0; dy < scaleY - (lit ? 0 : 1); ++dy)
              dot(left + dx, top + dy);
        }
      }
    }
    x += 8 * scaleX;
  }
}

// Chess-piece sprites and a lightning bolt for check, drawn at double size.
constexpr uint16_t pawn[]  = {0x060,0x0f0,0x0f0,0x060,0x1f8,0x0f0,0x060,0x0f0,0x1f8,0x3fc,0x000,0x7fe};
constexpr uint16_t crown[] = {0x000,0x402,0x462,0x6f6,0x7fe,0x3fc,0x3fc,0x1f8,0x000,0x3fc,0x3fc,0x000};
constexpr uint16_t bolt[]  = {0x01c,0x038,0x070,0x0e0,0x1fc,0x3f8,0x030,0x060,0x0c0,0x180,0x100,0x000};

void replay(u8x8_t& oled, const BoardApp& app, unsigned age, bool mate) {
  const auto& san = app.record().lastSan();
  const bool check = app.game().positionStatus().inCheck;
  const bool capture = san.find('x') != std::string::npos;
  const auto* sprite = mate ? crown : (check ? bolt : pawn);
  const int t = std::min(age, 480u);
  const int jump = 4 * 14 * t * (480 - t) / (480 * 480);
  const int left = 30 + t / 14, top = 13 - jump;
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 12; ++x)
      if (sprite[y] & (1u << (11 - x)))
        for (int dy = 0; dy < 2; ++dy)
          for (int dx = 0; dx < 2; ++dx) dot(left + 2*x + dx, top + 2*y + dy);
  for (int x = left + 3 + jump / 2; x < left + 21 - jump / 2; ++x) dot(x, 38);
  if ((capture || check || mate) && age >= 360) {
    constexpr int rays[][2] = {{-2,-1},{-1,-2},{1,-2},{2,-1},{2,1},{-2,1}};
    const unsigned burst = (age - 360) % 650;
    if (burst < 450) {
      const int radius = 4 + int(burst) / 55;
      for (const auto& ray : rays) {
        const int x = left + 12 + ray[0]*radius, y = top + 10 + ray[1]*radius;
        if (y < 38) { dot(x,y); dot(x+1,y); dot(x,y+1); }
      }
    }
  }
  const char* label = mate ? "MATE" : (check ? "XEQUE" : (capture ? "CAPTURA" : san.c_str()));
  lettering(oled, label, (width - int(std::strlen(label)) * 16) / 2, 40, 2, 3, int(age % 850) / 5);
}

std::string messageFor(const BoardApp& app) {
  const auto& game = app.game();
  const auto side = game.position().sideToMove();
  if (app.fault() != RecordStatus::Ok) return "ERRO DE REGISTRO";
  if (app.record().finished()) {
    switch (game.ending().reason) {
      case EndReason::Stalemate: return "AFOGAMENTO";
      case EndReason::InsufficientMaterial: return "EMPATE";
      case EndReason::Resignation: return "ABANDONO";
      default: return "FIM DE PARTIDA";
    }
  }
  if (!app.readingAvailable()) return "AGUARDE SENSORES";
  if (app.rejecting()) return "LANCE REJEITADO";
  if (game.state() == GameState::AwaitingPosition) return "MONTE AS PECAS";
  if (game.state() == GameState::AwaitingPromotion) return "PROMOVA NO PC";
  if (app.suggestedMove().from != kInvalidSquare)
    return (game.positionStatus().inCheck ? "XEQUE - " : "") + app.suggestedCoordinates();
  const auto& name = app.record().playerName(side);
  const std::string player = name != "?" ? name : (side == Color::White ? "Brancas" : "Pretas");
  return game.positionStatus().inCheck ? "XEQUE - " + player : player;
}

void sign(u8x8_t& oled, unsigned age) {
  const int textWidth = int(screen.message.size()) * 16;
  const int rest = std::max(0, (width - textWidth) / 2);
  const unsigned travel = unsigned(textWidth + rest) * 8; // 125 pixels/s, including long names
  const unsigned t = age % (220 + 950 + travel);
  const int x = t < 220 ? rest + int(220 - t) * (width - rest) / 220
                        : rest - int(t > 1170 ? t - 1170 : 0) / 8;
  lettering(oled, screen.message.c_str(), x, 16, 2, 4, int(age % 1200) / 6 - 20);
  const int phase = int((age / 55) % 12);
  for (int i = 0; i < width; ++i) {
    if ((i + phase) % 12 < 3) dot(i, 7);
    if ((width - i + phase) % 12 < 3) dot(i, 56);
  }
}
} // namespace

void reset() { screen = Animation{}; }

void update(u8x8_t& oled, const BoardApp& app, std::uint32_t now) {
  if (screen.chunk == chunkCount) {
    if (now - screen.frameAt < 40) return;
    const auto ply = app.record().plyCount();
    const bool mate = app.game().ending().reason == EndReason::Checkmate;
    if (ply != screen.plies) {
      screen.celebrating = ply > screen.plies;
      screen.sceneAt = now;
      screen.plies = ply;
    }
    std::memset(screen.frame, 0, sizeof(screen.frame));
    if (mate || (screen.celebrating && now - screen.sceneAt < 900)) {
      replay(oled, app, now - screen.sceneAt, mate);
    } else {
      auto text = messageFor(app);
      if (screen.celebrating || text != screen.message) {
        screen.message = std::move(text);
        screen.sceneAt = now;
        screen.celebrating = false;
      }
      sign(oled, now - screen.sceneAt);
    }
    screen.frameAt = now;
    screen.chunk = 0;
  }
  while (screen.chunk < chunkCount) {
    const unsigned chunk = screen.chunk++, band = chunk / 4, x = chunk % 4 * chunkBytes;
    auto* pixels = &screen.frame[band][x];
    auto* last = &screen.shown[band][x];
    if (std::memcmp(pixels, last, chunkBytes) == 0) continue;
    u8x8_DrawTile(&oled, x / 8, band, chunkBytes / 8, pixels);
    std::memcpy(last, pixels, chunkBytes);
    return;
  }
}
}
}
