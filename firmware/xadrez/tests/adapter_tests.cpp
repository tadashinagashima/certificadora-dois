#include "ChessAdapter.h"

#include <cstdio>
#include <cstring>

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (false)

namespace {
using namespace chessboard;

const char* startFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

Square square(const char* name) {
  return static_cast<Square>((name[1] - '1') * 8 + name[0] - 'a');
}

Move move(const char* from, const char* to, Promotion promotion = Promotion::None) {
  return {square(from), square(to), promotion};
}

std::uint64_t bit(const char* name) {
  return std::uint64_t{1} << square(name);
}

bool sameMove(const Move& a, const Move& b) {
  return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

bool contains(const LegalMoves& list, const Move& candidate) {
  for (std::size_t i = 0; i < list.count; ++i) {
    if (sameMove(list.moves[i], candidate)) return true;
  }
  return false;
}

struct State {
  std::string fen;
  std::uint64_t occupied;
  PositionStatus status;
};

State saved(const ChessAdapter& board) {
  return {board.fen(), board.occupancy(), board.status()};
}

bool unchanged(const ChessAdapter& board, const State& before) {
  const auto after = board.status();
  CHECK(board.fen() == before.fen);
  CHECK(board.occupancy() == before.occupied);
  CHECK(board.sideToMove() == before.status.sideToMove);
  CHECK(after.sideToMove == before.status.sideToMove);
  CHECK(after.inCheck == before.status.inCheck);
  CHECK(after.terminal == before.status.terminal);
  CHECK(after.insufficientMaterial == before.status.insufficientMaterial);
  CHECK(after.fiftyMoveClaim == before.status.fiftyMoveClaim);
  CHECK(after.threefoldRepetitionClaim == before.status.threefoldRepetitionClaim);
  return true;
}

bool reject(ChessAdapter& board, const Move& candidate) {
  const auto before = saved(board);
  std::string san = "unchanged";
  CHECK(!board.isLegal(candidate));
  CHECK(!board.san(candidate, san));
  CHECK(san == "unchanged");
  CHECK(!board.apply(candidate));
  CHECK(unchanged(board, before));
  return true;
}

bool play(ChessAdapter& board, const Move& candidate, const char* expectedSan = nullptr) {
  const auto before = saved(board);
  CHECK(board.isLegal(candidate));
  std::string san;
  CHECK(board.san(candidate, san));
  if (expectedSan) CHECK(san == expectedSan);
  CHECK(unchanged(board, before));
  CHECK(board.apply(candidate));
  CHECK(board.sideToMove() != before.status.sideToMove);
  return true;
}

bool knightCycle(ChessAdapter& board) {
  CHECK(play(board, move("g1", "f3")));
  CHECK(play(board, move("g8", "f6")));
  CHECK(play(board, move("f3", "g1")));
  CHECK(play(board, move("f6", "g8")));
  return true;
}

bool initialCoordinates() {
  ChessAdapter board;
  CHECK(board.fen() == startFen);
  CHECK(board.sideToMove() == Color::White);
  CHECK(board.occupancy() == UINT64_C(0xffff00000000ffff));
  const Piece white[] = {Piece::WhiteRook, Piece::WhiteKnight, Piece::WhiteBishop,
    Piece::WhiteQueen, Piece::WhiteKing, Piece::WhiteBishop, Piece::WhiteKnight, Piece::WhiteRook};
  const Piece black[] = {Piece::BlackRook, Piece::BlackKnight, Piece::BlackBishop,
    Piece::BlackQueen, Piece::BlackKing, Piece::BlackBishop, Piece::BlackKnight, Piece::BlackRook};
  for (Square index = 0; index < 64; ++index) {
    const Piece expected = index < 8 ? white[index] : index < 16 ? Piece::WhitePawn :
      index < 48 ? Piece::Empty : index < 56 ? Piece::BlackPawn : black[index - 56];
    CHECK(board.pieceAt(index) == expected);
  }
  CHECK(board.pieceAt(64) == Piece::Empty);
  CHECK(board.pieceAt(255) == Piece::Empty);
  for (Square target = 1; target < 63; ++target) {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
      int empty = 0;
      for (int file = 0; file < 8; ++file) {
        const int index = rank * 8 + file;
        const char piece = index == 0 ? 'K' : index == 63 ? 'k' : index == target ? 'N' : ' ';
        if (piece == ' ') ++empty;
        else {
          if (empty) fen += static_cast<char>('0' + empty);
          empty = 0;
          fen += piece;
        }
      }
      if (empty) fen += static_cast<char>('0' + empty);
      if (rank) fen += '/';
    }
    fen += " b - - 0 1";
    CHECK(board.loadFen(fen.c_str()));
    CHECK(board.pieceAt(target) == Piece::WhiteKnight);
    CHECK(board.pieceAt(0) == Piece::WhiteKing);
    CHECK(board.pieceAt(63) == Piece::BlackKing);
    CHECK(board.occupancy() == (UINT64_C(0x8000000000000001) | (std::uint64_t{1} << target)));
  }
  return true;
}

bool legalMoves() {
  ChessAdapter board;
  const auto before = saved(board);
  const auto list = board.legalMoves();
  CHECK(list.count == 20);
  CHECK(list.count <= list.moves.size());
  for (int file = 0; file < 8; ++file) {
    CHECK(contains(list, {static_cast<Square>(8 + file), static_cast<Square>(16 + file), Promotion::None}));
    CHECK(contains(list, {static_cast<Square>(8 + file), static_cast<Square>(24 + file), Promotion::None}));
  }
  CHECK(contains(list, move("b1", "a3")));
  CHECK(contains(list, move("b1", "c3")));
  CHECK(contains(list, move("g1", "f3")));
  CHECK(contains(list, move("g1", "h3")));
  for (std::size_t i = 0; i < list.count; ++i) {
    CHECK(board.isLegal(list.moves[i]));
    CHECK(list.moves[i].from < 64 && list.moves[i].to < 64);
    for (std::size_t j = i + 1; j < list.count; ++j) CHECK(!sameMove(list.moves[i], list.moves[j]));
  }
  CHECK(unchanged(board, before));
  CHECK(play(board, move("e2", "e4")));
  CHECK(list.count == 20 && contains(list, move("e2", "e4")));
  CHECK(reject(board, move("e2", "e4")));
  return true;
}

bool invalidMoves() {
  ChessAdapter board;
  CHECK(reject(board, Move{}));
  CHECK(reject(board, {64, square("e4"), Promotion::None}));
  CHECK(reject(board, {255, square("e4"), Promotion::None}));
  CHECK(reject(board, {square("e2"), 64, Promotion::None}));
  CHECK(reject(board, {square("e2"), 255, Promotion::None}));
  CHECK(reject(board, move("e2", "e5")));
  CHECK(reject(board, move("e7", "e5")));
  CHECK(reject(board, move("c1", "h6")));
  CHECK(reject(board, move("e2", "e2")));
  CHECK(reject(board, move("e2", "e4", Promotion::Queen)));
  CHECK(reject(board, move("e2", "e4", static_cast<Promotion>(255))));
  CHECK(board.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  CHECK(reject(board, move("a7", "a8")));
  CHECK(reject(board, move("a7", "a8", static_cast<Promotion>(255))));
  CHECK(board.loadFen("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1"));
  CHECK(reject(board, move("e2", "d2")));
  return true;
}

bool queriesPreserve() {
  ChessAdapter board;
  CHECK(knightCycle(board));
  const auto before = saved(board);
  CHECK(!before.status.threefoldRepetitionClaim);
  for (int i = 0; i < 3; ++i) {
    CHECK(board.legalMoves().count == 20);
    CHECK(board.isLegal(move("e2", "e4")));
    CHECK(board.pieceAt(square("g1")) == Piece::WhiteKnight);
    std::string san;
    CHECK(board.san(move("e2", "e4"), san) && san == "e4");
    CHECK(reject(board, move("e2", "e5")));
    CHECK(unchanged(board, before));
  }
  CHECK(knightCycle(board));
  CHECK(board.status().threefoldRepetitionClaim);
  const auto repeated = saved(board);
  CHECK(reject(board, move("e2", "e5")));
  CHECK(unchanged(board, repeated));
  return true;
}

bool applyReset() {
  ChessAdapter board;
  CHECK(play(board, move("e2", "e4"), "e4"));
  CHECK(board.fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  CHECK(board.occupancy() == ((UINT64_C(0xffff00000000ffff) & ~bit("e2")) | bit("e4")));
  CHECK(board.pieceAt(square("e2")) == Piece::Empty);
  CHECK(board.pieceAt(square("e4")) == Piece::WhitePawn);
  board.reset();
  CHECK(board.fen() == startFen);
  CHECK(board.legalMoves().count == 20);
  CHECK(knightCycle(board));
  CHECK(knightCycle(board));
  CHECK(board.status().threefoldRepetitionClaim);
  board.reset();
  CHECK(board.fen() == startFen);
  CHECK(!board.status().threefoldRepetitionClaim);
  CHECK(knightCycle(board));
  CHECK(!board.status().threefoldRepetitionClaim);
  return true;
}

bool fenLoading() {
  ChessAdapter board;
  CHECK(knightCycle(board));
  CHECK(knightCycle(board));
  const auto before = saved(board);
  const char* invalid[] = {nullptr, "", "not fen", "8/8/8/8/8/8/8/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/8/P3K3 w - - 0 1", "4k3/8/8/8/8/8/4R3/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/8/4K3 w - - -1 1", "4k3/8/8/8/8/8/8/4K3 w - - 0 0"};
  for (const auto fen : invalid) {
    CHECK(!board.loadFen(fen));
    CHECK(unchanged(board, before));
  }
  CHECK(board.loadFen(startFen));
  CHECK(board.fen() == startFen);
  CHECK(!board.status().threefoldRepetitionClaim);
  CHECK(knightCycle(board));
  CHECK(!board.status().threefoldRepetitionClaim);
  const char* fixture = "4k3/8/8/8/8/8/8/R3K3 b - - 17 42";
  CHECK(board.loadFen(fixture));
  CHECK(board.fen() == fixture);
  CHECK(board.sideToMove() == Color::Black);
  return true;
}

bool capture() {
  ChessAdapter board;
  CHECK(board.loadFen("k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1"));
  const auto before = board.occupancy();
  CHECK(play(board, move("e4", "d5"), "exd5"));
  CHECK(board.pieceAt(square("e4")) == Piece::Empty);
  CHECK(board.pieceAt(square("d5")) == Piece::WhitePawn);
  CHECK(board.occupancy() == (before & ~bit("e4")));
  return true;
}

bool castling() {
  struct Case { const char* fen; const char* from; const char* to; const char* rookFrom;
    const char* rookTo; const char* san; Piece king; Piece rook; };
  const Case cases[] = {
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "g1", "h1", "f1", "O-O", Piece::WhiteKing, Piece::WhiteRook},
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "c1", "a1", "d1", "O-O-O", Piece::WhiteKing, Piece::WhiteRook},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "g8", "h8", "f8", "O-O", Piece::BlackKing, Piece::BlackRook},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "c8", "a8", "d8", "O-O-O", Piece::BlackKing, Piece::BlackRook}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    const auto before = board.occupancy();
    CHECK(play(board, move(test.from, test.to), test.san));
    CHECK(board.pieceAt(square(test.from)) == Piece::Empty);
    CHECK(board.pieceAt(square(test.rookFrom)) == Piece::Empty);
    CHECK(board.pieceAt(square(test.to)) == test.king);
    CHECK(board.pieceAt(square(test.rookTo)) == test.rook);
    CHECK(board.occupancy() == ((before & ~bit(test.from) & ~bit(test.rookFrom)) | bit(test.to) | bit(test.rookTo)));
  }
  return true;
}

bool castlingConstraints() {
  ChessAdapter board;
  CHECK(board.loadFen("r3kr1r/8/8/8/8/8/8/R3K2R w KQ - 0 1"));
  CHECK(reject(board, move("e1", "g1")));
  CHECK(board.isLegal(move("e1", "c1")));
  CHECK(board.loadFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
  CHECK(play(board, move("h1", "h2")));
  CHECK(play(board, move("h8", "h7")));
  CHECK(play(board, move("h2", "h1")));
  CHECK(play(board, move("h7", "h8")));
  CHECK(reject(board, move("e1", "g1")));
  CHECK(board.isLegal(move("e1", "c1")));
  return true;
}

bool enPassant() {
  ChessAdapter board;
  CHECK(board.loadFen("k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
  const auto before = board.occupancy();
  CHECK(play(board, move("e5", "d6"), "exd6"));
  CHECK(board.pieceAt(square("e5")) == Piece::Empty);
  CHECK(board.pieceAt(square("d5")) == Piece::Empty);
  CHECK(board.pieceAt(square("d6")) == Piece::WhitePawn);
  CHECK(board.occupancy() == ((before & ~bit("e5") & ~bit("d5")) | bit("d6")));
  CHECK(board.loadFen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
  CHECK(reject(board, move("e5", "d6")));
  board.reset();
  CHECK(play(board, move("e2", "e4")));
  CHECK(play(board, move("a7", "a6")));
  CHECK(play(board, move("e4", "e5")));
  CHECK(play(board, move("d7", "d5")));
  CHECK(board.isLegal(move("e5", "d6")));
  CHECK(play(board, move("g1", "f3")));
  CHECK(play(board, move("a6", "a5")));
  CHECK(reject(board, move("e5", "d6")));
  return true;
}

bool promotions() {
  const Promotion choices[] = {Promotion::Queen, Promotion::Rook, Promotion::Bishop, Promotion::Knight};
  const Piece white[] = {Piece::WhiteQueen, Piece::WhiteRook, Piece::WhiteBishop, Piece::WhiteKnight};
  const Piece black[] = {Piece::BlackQueen, Piece::BlackRook, Piece::BlackBishop, Piece::BlackKnight};
  const char* whiteSan[] = {"a8=Q+", "a8=R+", "a8=B", "a8=N"};
  const char* blackSan[] = {"a1=Q+", "a1=R+", "a1=B", "a1=N"};
  const char* captureSan[] = {"axb8=Q+", "axb8=R+", "axb8=B", "axb8=N"};
  ChessAdapter board;
  for (int i = 0; i < 4; ++i) {
    CHECK(board.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(contains(board.legalMoves(), move("a7", "a8", choices[i])));
    CHECK(play(board, move("a7", "a8", choices[i]), whiteSan[i]));
    CHECK(board.pieceAt(square("a8")) == white[i]);
    CHECK(board.pieceAt(square("a7")) == Piece::Empty);
    CHECK(board.loadFen("4k3/8/8/8/8/8/p7/7K b - - 0 1"));
    CHECK(contains(board.legalMoves(), move("a2", "a1", choices[i])));
    CHECK(play(board, move("a2", "a1", choices[i]), blackSan[i]));
    CHECK(board.pieceAt(square("a1")) == black[i]);
    CHECK(board.pieceAt(square("a2")) == Piece::Empty);
    CHECK(board.loadFen("1r5k/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(play(board, move("a7", "b8", choices[i]), captureSan[i]));
    CHECK(board.pieceAt(square("b8")) == white[i]);
    CHECK(board.occupancy() == (bit("b8") | bit("h8") | bit("e1")));
  }
  return true;
}

bool san() {
  ChessAdapter board;
  CHECK(board.loadFen("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1"));
  CHECK(play(board, move("b1", "d2"), "Nbd2"));
  CHECK(board.loadFen("7k/8/8/8/8/R7/8/R6K w - - 0 1"));
  CHECK(play(board, move("a1", "a2"), "R1a2"));
  CHECK(board.loadFen("4k3/8/8/8/8/8/R7/6K1 w - - 0 1"));
  CHECK(play(board, move("a2", "e2"), "Re2+"));
  CHECK(board.status().inCheck);
  return true;
}

bool terminal() {
  ChessAdapter board;
  CHECK(!board.status().inCheck);
  CHECK(board.status().terminal == TerminalState::None);
  CHECK(play(board, move("f2", "f3"), "f3"));
  CHECK(play(board, move("e7", "e5"), "e5"));
  CHECK(play(board, move("g2", "g4"), "g4"));
  CHECK(play(board, move("d8", "h4"), "Qh4#"));
  CHECK(board.status().terminal == TerminalState::Checkmate);
  CHECK(board.status().sideToMove == Color::White);
  CHECK(board.status().inCheck);
  CHECK(board.legalMoves().count == 0);
  CHECK(reject(board, move("e2", "e4")));
  CHECK(board.loadFen("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"));
  CHECK(board.status().terminal == TerminalState::Stalemate);
  CHECK(board.status().sideToMove == Color::Black);
  CHECK(!board.status().inCheck);
  CHECK(board.legalMoves().count == 0);
  return true;
}

bool material() {
  ChessAdapter board;
  const char* automatic[] = {"4k3/8/8/8/8/8/8/4K3 w - - 0 1",
    "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", "4k3/8/8/8/8/8/8/1N2K3 b - - 0 1"};
  for (const auto fen : automatic) {
    CHECK(board.loadFen(fen));
    CHECK(board.status().insufficientMaterial);
    CHECK(board.status().terminal == TerminalState::None);
  }
  CHECK(board.loadFen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
  CHECK(!board.status().insufficientMaterial);
  CHECK(board.loadFen("r3k3/8/8/8/8/8/8/4K3 b - - 0 1"));
  CHECK(!board.status().insufficientMaterial);
  return true;
}

bool fiftyMove() {
  ChessAdapter board;
  CHECK(board.loadFen("r3k3/8/8/8/8/8/8/R3K3 w - - 99 1"));
  CHECK(!board.status().fiftyMoveClaim);
  CHECK(play(board, move("a1", "a2")));
  CHECK(board.status().fiftyMoveClaim);
  CHECK(board.status().terminal == TerminalState::None);
  CHECK(play(board, move("a8", "a2")));
  CHECK(!board.status().fiftyMoveClaim);
  CHECK(board.loadFen("4k3/8/8/8/8/8/P7/4K3 w - - 100 1"));
  CHECK(board.status().fiftyMoveClaim);
  CHECK(play(board, move("a2", "a3")));
  CHECK(!board.status().fiftyMoveClaim);
  return true;
}

bool repetition() {
  ChessAdapter board;
  CHECK(!board.status().threefoldRepetitionClaim);
  CHECK(knightCycle(board));
  CHECK(!board.status().threefoldRepetitionClaim);
  CHECK(knightCycle(board));
  CHECK(board.status().threefoldRepetitionClaim);
  CHECK(board.status().terminal == TerminalState::None);
  CHECK(play(board, move("e2", "e4")));
  CHECK(!board.status().threefoldRepetitionClaim);
  return true;
}

bool copyHistory() {
  ChessAdapter original;
  CHECK(knightCycle(original));
  CHECK(knightCycle(original));
  const auto before = saved(original);
  {
    ChessAdapter copy(original);
    CHECK(unchanged(copy, before));
    CHECK(play(copy, move("e2", "e4")));
    CHECK(!copy.status().threefoldRepetitionClaim);
    CHECK(unchanged(original, before));
  }
  {
    ChessAdapter assigned;
    CHECK(play(assigned, move("d2", "d4")));
    assigned = original;
    CHECK(unchanged(assigned, before));
    assigned.reset();
    CHECK(!assigned.status().threefoldRepetitionClaim);
    CHECK(unchanged(original, before));
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) return 2;
  const char* selected = argc == 2 ? argv[1] : nullptr;
  struct Case { const char* name; bool (*run)(); };
  const Case cases[] = {
    {"initial_coordinates", initialCoordinates},
    {"legal_moves", legalMoves}, {"invalid_moves", invalidMoves},
    {"queries_preserve", queriesPreserve}, {"apply_reset", applyReset},
    {"fen_loading", fenLoading}, {"capture", capture},
    {"castling", castling}, {"castling_constraints", castlingConstraints},
    {"en_passant", enPassant}, {"promotions", promotions},
    {"san", san}, {"terminal", terminal}, {"material", material},
    {"fifty_move", fiftyMove}, {"repetition", repetition}, {"copy_history", copyHistory}
  };
  int failures = 0;
  int executed = 0;
  for (const auto& test : cases) {
    if (selected && std::strcmp(selected, test.name)) continue;
    const bool passed = test.run();
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.name);
    if (!passed) ++failures;
    ++executed;
  }
  return executed ? failures : -1;
}
