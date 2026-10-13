#include "ChessAdapter.h"

namespace chessboard {
namespace {

static_assert(sizeof(thc::Move) == 4, "THC requires 32-bit moves");
static_assert(kLegalMoveCapacity == MAXMOVES, "Review capacity when changing THC");

Promotion promotionOf(thc::SPECIAL special) {
  switch (special) {
    case thc::SPECIAL_PROMOTION_QUEEN: return Promotion::Queen;
    case thc::SPECIAL_PROMOTION_ROOK: return Promotion::Rook;
    case thc::SPECIAL_PROMOTION_BISHOP: return Promotion::Bishop;
    case thc::SPECIAL_PROMOTION_KNIGHT: return Promotion::Knight;
    default: return Promotion::None;
  }
}

Move fromThc(const thc::Move& move) {
  return {static_cast<Square>(static_cast<unsigned>(move.src) ^ 56U),
          static_cast<Square>(static_cast<unsigned>(move.dst) ^ 56U),
          promotionOf(move.special)};
}

bool findLegal(thc::ChessRules& position, const Move& candidate, thc::Move& selected) {
  if (candidate.from >= kInvalidSquare || candidate.to >= kInvalidSquare) return false;
  thc::MOVELIST legal;
  position.GenLegalMoveList(&legal);
  for (int i = 0; i < legal.count; ++i) {
    const Move move = fromThc(legal.moves[i]);
    if (move.from == candidate.from && move.to == candidate.to &&
        move.promotion == candidate.promotion) {
      selected = legal.moves[i];
      return true;
    }
  }
  return false;
}

}  // namespace

void ChessAdapter::reset() {
  // THC's Init() alone resets history, not the board.
  rules_ = thc::ChessRules{};
}

bool ChessAdapter::loadFen(const char* fenText) {
  if (!fenText) return false;
  thc::ChessRules position;
  thc::ILLEGAL_REASON reason;
  if (!position.Forsyth(fenText) || !position.IsLegal(reason) ||
      position.half_move_clock < 0 || position.full_move_count < 1) return false;
  rules_ = position;
  return true;
}

std::string ChessAdapter::fen() const {
  auto position = rules_;
  return position.ForsythPublish();
}

Color ChessAdapter::sideToMove() const {
  return rules_.white ? Color::White : Color::Black;
}

Piece ChessAdapter::pieceAt(Square square) const {
  if (square >= kInvalidSquare) return Piece::Empty;
  switch (rules_.squares[square ^ 56U]) {
    case 'P': return Piece::WhitePawn;
    case 'N': return Piece::WhiteKnight;
    case 'B': return Piece::WhiteBishop;
    case 'R': return Piece::WhiteRook;
    case 'Q': return Piece::WhiteQueen;
    case 'K': return Piece::WhiteKing;
    case 'p': return Piece::BlackPawn;
    case 'n': return Piece::BlackKnight;
    case 'b': return Piece::BlackBishop;
    case 'r': return Piece::BlackRook;
    case 'q': return Piece::BlackQueen;
    case 'k': return Piece::BlackKing;
    default: return Piece::Empty;
  }
}

std::uint64_t ChessAdapter::occupancy() const {
  std::uint64_t occupied = 0;
  for (Square square = 0; square < kInvalidSquare; ++square) {
    if (rules_.squares[square ^ 56U] != ' ') occupied |= std::uint64_t{1} << square;
  }
  return occupied;
}

LegalMoves ChessAdapter::legalMoves() const {
  auto position = rules_;
  thc::MOVELIST legal;
  position.GenLegalMoveList(&legal);
  LegalMoves result;
  result.count = static_cast<std::size_t>(legal.count);
  for (std::size_t i = 0; i < result.count; ++i) result.moves[i] = fromThc(legal.moves[i]);
  return result;
}

bool ChessAdapter::isLegal(const Move& move) const {
  auto position = rules_;
  thc::Move selected;
  return findLegal(position, move, selected);
}

bool ChessAdapter::san(const Move& move, std::string& output) const {
  auto position = rules_;
  thc::Move selected;
  if (!findLegal(position, move, selected)) return false;
  output = selected.NaturalOut(&position);
  return true;
}

bool ChessAdapter::apply(const Move& move) {
  auto position = rules_;
  thc::Move selected;
  if (!findLegal(position, move, selected)) return false;
  rules_.PlayMove(selected);
  return true;
}

PositionStatus ChessAdapter::status() const {
  auto position = rules_;
  PositionStatus result;
  result.sideToMove = sideToMove();
  result.inCheck = position.AttackedPiece(position.white ? position.wking_square : position.bking_square);
  thc::TERMINAL terminal;
  position.Evaluate(terminal);
  switch (terminal) {
    case thc::TERMINAL_WCHECKMATE:
    case thc::TERMINAL_BCHECKMATE: result.terminal = TerminalState::Checkmate; break;
    case thc::TERMINAL_WSTALEMATE:
    case thc::TERMINAL_BSTALEMATE: result.terminal = TerminalState::Stalemate; break;
    default: break;
  }
  thc::DRAWTYPE material;
  result.insufficientMaterial = position.IsInsufficientDraw(true, material) &&
                                material == thc::DRAWTYPE_INSUFFICIENT_AUTO;
  result.fiftyMoveClaim = position.half_move_clock >= 100;
  result.threefoldRepetitionClaim = position.GetRepetitionCount() >= 3;
  return result;
}

}  // namespace chessboard
