#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "thc/thc.h"

namespace chessboard {

// a1=0, h1=7, a8=56, h8=63.
using Square = std::uint8_t;
constexpr Square kInvalidSquare = 64;
constexpr std::size_t kLegalMoveCapacity = 218;

enum class Color { White, Black };
enum class Piece {
  Empty,
  WhitePawn, WhiteKnight, WhiteBishop, WhiteRook, WhiteQueen, WhiteKing,
  BlackPawn, BlackKnight, BlackBishop, BlackRook, BlackQueen, BlackKing
};
enum class Promotion : std::uint8_t { None, Queen, Rook, Bishop, Knight };

struct Move {
  Square from = kInvalidSquare;
  Square to = kInvalidSquare;
  Promotion promotion = Promotion::None;
};

struct LegalMoves {
  std::array<Move, kLegalMoveCapacity> moves{};
  std::size_t count = 0;
};

enum class TerminalState { None, Checkmate, Stalemate };

struct PositionStatus {
  Color sideToMove = Color::White;
  bool inCheck = false;
  TerminalState terminal = TerminalState::None;
  bool insufficientMaterial = false;
  bool fiftyMoveClaim = false;
  bool threefoldRepetitionClaim = false;
};

class ChessAdapter {
 public:
  void reset();

  // Position setup clears repetition history; accepts THC's FEN dialect.
  bool loadFen(const char* fen);
  std::string fen() const;

  Color sideToMove() const;
  Piece pieceAt(Square square) const;
  std::uint64_t occupancy() const;
  LegalMoves legalMoves() const;
  bool isLegal(const Move& move) const;

  // SAN is generated in the current position; failure leaves output unchanged.
  bool san(const Move& move, std::string& output) const;
  bool apply(const Move& move);
  PositionStatus status() const;

 private:
  thc::ChessRules rules_;
};

}  // namespace chessboard
