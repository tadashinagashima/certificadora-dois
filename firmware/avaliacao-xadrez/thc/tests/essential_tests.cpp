#include "essential_tests.h"
#include "thc.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

#define REQUIRE(condition) do { if (!(condition)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (false)

namespace {
const char* startFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool play(thc::ChessRules& board, const char* uci, const char* san = nullptr) {
  const auto before = board.ForsythPublish();
  thc::Move move;
  REQUIRE(move.TerseIn(&board, uci));
  REQUIRE(board.ForsythPublish() == before);
  if (san) {
    REQUIRE(move.NaturalOut(&board) == san);
    REQUIRE(board.ForsythPublish() == before);
  }
  board.PlayMove(move);  // Only after legal lookup; PlayMove itself does not validate.
  return true;
}

bool internals() {
  thc::ChessRules board;
  REQUIRE(board.TestInternals(nullptr));
  static_assert(sizeof(thc::Move) == 4, "THC requires 32-bit moves");
  std::printf("sizeof ChessRules=%u ChessPosition=%u Move=%u MOVELIST=%u\n",
              static_cast<unsigned>(sizeof(thc::ChessRules)),
              static_cast<unsigned>(sizeof(thc::ChessPosition)),
              static_cast<unsigned>(sizeof(thc::Move)),
              static_cast<unsigned>(sizeof(thc::MOVELIST)));
  return true;
}

bool initialAndCoordinates() {
  thc::ChessRules board;
  REQUIRE(board.ForsythPublish() == startFen);
  thc::MOVELIST list;
  board.GenLegalMoveList(&list);
  REQUIRE(list.count == 20);
  REQUIRE(board.ForsythPublish() == startFen);
  for (int projectIndex = 0; projectIndex < 64; ++projectIndex) {
    const auto square = thc::make_square(static_cast<char>('a' + projectIndex % 8),
                                          static_cast<char>('1' + projectIndex / 8));
    REQUIRE(static_cast<int>(square) == (projectIndex ^ 56));
  }
  REQUIRE(play(board, "e2e4", "e4"));
  REQUIRE(board.ForsythPublish() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
  return true;
}

bool illegalMovesPreservePosition() {
  thc::ChessRules board;
  thc::Move move;
  REQUIRE(!move.TerseIn(&board, "e2e5"));
  REQUIRE(!move.TerseIn(&board, "e7e5"));
  REQUIRE(!move.TerseIn(&board, "c1h6"));
  REQUIRE(board.ForsythPublish() == startFen);
  REQUIRE(board.Forsyth("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1"));
  const auto before = board.ForsythPublish();
  REQUIRE(!move.TerseIn(&board, "e2d2"));  // Exposes own king.
  REQUIRE(board.ForsythPublish() == before);
  return true;
}

bool capture() {
  thc::ChessRules board;
  REQUIRE(board.Forsyth("k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1"));
  REQUIRE(play(board, "e4d5", "exd5"));
  REQUIRE(board.squares[thc::e4] == ' ' && board.squares[thc::d5] == 'P');
  REQUIRE(!board.white && board.half_move_clock == 0);
  return true;
}

bool castling() {
  struct Case { const char* fen; const char* uci; const char* san;
    thc::Square kingFrom, rookFrom, kingTo, rookTo; char king, rook; };
  const Case cases[] = {
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "O-O", thc::e1, thc::h1, thc::g1, thc::f1, 'K', 'R'},
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "O-O-O", thc::e1, thc::a1, thc::c1, thc::d1, 'K', 'R'},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", "O-O", thc::e8, thc::h8, thc::g8, thc::f8, 'k', 'r'},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", "O-O-O", thc::e8, thc::a8, thc::c8, thc::d8, 'k', 'r'}
  };
  for (const auto& test : cases) {
    thc::ChessRules board;
    REQUIRE(board.Forsyth(test.fen));
    const bool whiteBefore = board.white;
    REQUIRE(play(board, test.uci, test.san));
    REQUIRE(board.squares[test.kingFrom] == ' ' && board.squares[test.rookFrom] == ' ');
    REQUIRE(board.squares[test.kingTo] == test.king && board.squares[test.rookTo] == test.rook);
    REQUIRE(board.white != whiteBefore);
  }
  return true;
}

bool castlingConstraints() {
  thc::ChessRules board;
  thc::Move move;
  REQUIRE(board.Forsyth("r3kr1r/8/8/8/8/8/8/R3K2R w KQ - 0 1"));
  REQUIRE(!move.TerseIn(&board, "e1g1"));  // f1 is attacked.
  REQUIRE(move.TerseIn(&board, "e1c1"));
  REQUIRE(board.Forsyth("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
  REQUIRE(play(board, "h1h2")); REQUIRE(play(board, "h8h7"));
  REQUIRE(play(board, "h2h1")); REQUIRE(play(board, "h7h8"));
  REQUIRE(!move.TerseIn(&board, "e1g1"));  // Returning the rook does not restore rights.
  REQUIRE(move.TerseIn(&board, "e1c1"));
  return true;
}

bool enPassant() {
  thc::ChessRules board;
  REQUIRE(board.Forsyth("k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
  REQUIRE(play(board, "e5d6", "exd6"));
  REQUIRE(board.squares[thc::e5] == ' ' && board.squares[thc::d5] == ' ');
  REQUIRE(board.squares[thc::d6] == 'P');
  REQUIRE(board.Forsyth("k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
  thc::Move move;
  REQUIRE(!move.TerseIn(&board, "e5d6"));  // En passant cannot expose the king.
  REQUIRE(board.Forsyth("k7/8/8/3pP3/8/8/8/4K3 w - - 0 1"));
  REQUIRE(!move.TerseIn(&board, "e5d6"));  // No en passant right in this position.
  return true;
}

bool promotions() {
  const char* whiteMoves[] = {"a7a8q", "a7a8r", "a7a8b", "a7a8n"};
  const char* blackMoves[] = {"a2a1q", "a2a1r", "a2a1b", "a2a1n"};
  const char* whiteSan[] = {"a8=Q+", "a8=R+", "a8=B", "a8=N"};
  const char* blackSan[] = {"a1=Q+", "a1=R+", "a1=B", "a1=N"};
  const char whitePieces[] = "QRBN";
  const char blackPieces[] = "qrbn";
  for (int i = 0; i < 4; ++i) {
    thc::ChessRules board;
    REQUIRE(board.Forsyth("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
    REQUIRE(play(board, whiteMoves[i], whiteSan[i]));
    REQUIRE(board.squares[thc::a8] == whitePieces[i] && !board.white);
    REQUIRE(board.Forsyth("4k3/8/8/8/8/8/p7/7K b - - 0 1"));
    REQUIRE(play(board, blackMoves[i], blackSan[i]));
    REQUIRE(board.squares[thc::a1] == blackPieces[i] && board.white);
  }
  return true;
}

bool sanDisambiguation() {
  thc::ChessRules board;
  REQUIRE(board.Forsyth("4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1"));
  REQUIRE(play(board, "b1d2", "Nbd2"));
  REQUIRE(board.Forsyth("7k/8/8/8/8/R7/8/R6K w - - 0 1"));
  REQUIRE(play(board, "a1a2", "R1a2"));
  REQUIRE(board.Forsyth("4k3/8/8/8/8/8/R7/6K1 w - - 0 1"));
  REQUIRE(play(board, "a2e2", "Re2+"));
  return true;
}

bool checkmate() {
  thc::ChessRules board;
  REQUIRE(play(board, "f2f3", "f3")); REQUIRE(play(board, "e7e5", "e5"));
  REQUIRE(play(board, "g2g4", "g4")); REQUIRE(play(board, "d8h4", "Qh4#"));
  thc::TERMINAL result;
  REQUIRE(board.Evaluate(result));
  REQUIRE(result == thc::TERMINAL_WCHECKMATE);
  thc::MOVELIST moves;
  board.GenLegalMoveList(&moves);
  REQUIRE(moves.count == 0);
  return true;
}

bool stalemate() {
  thc::ChessRules board;
  REQUIRE(board.Forsyth("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"));
  thc::TERMINAL result;
  REQUIRE(board.Evaluate(result));
  REQUIRE(result == thc::TERMINAL_BSTALEMATE);
  return true;
}

bool drawQueries() {
  thc::ChessRules board;
  thc::DRAWTYPE result;
  REQUIRE(board.Forsyth("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
  REQUIRE(board.IsDraw(true, result) && result == thc::DRAWTYPE_INSUFFICIENT_AUTO);
  REQUIRE(board.Forsyth("r3k3/8/8/8/8/8/8/R3K3 w - - 99 1"));
  REQUIRE(!board.IsDraw(true, result));
  REQUIRE(play(board, "a1a2"));
  REQUIRE(board.half_move_clock == 100);
  REQUIRE(board.IsDraw(false, result) && result == thc::DRAWTYPE_50MOVE);
  REQUIRE(board.Forsyth(startFen));
  for (int cycle = 0; cycle < 2; ++cycle) {
    REQUIRE(play(board, "g1f3")); REQUIRE(play(board, "g8f6"));
    REQUIRE(play(board, "f3g1")); REQUIRE(play(board, "f6g8"));
  }
  const auto before = board.ForsythPublish();
  REQUIRE(board.GetRepetitionCount() == 3);
  REQUIRE(board.IsDraw(true, result) && result == thc::DRAWTYPE_REPITITION);
  REQUIRE(board.ForsythPublish() == before);
  thc::ChessRules copy = board;
  REQUIRE(copy.GetRepetitionCount() == 3);
  REQUIRE(play(copy, "e2e4"));
  REQUIRE(copy.GetRepetitionCount() == 1);
  REQUIRE(board.ForsythPublish() == before && board.GetRepetitionCount() == 3);
  return true;
}

bool copyAndUndo() {
  thc::ChessRules official;
  thc::Move move;
  REQUIRE(move.TerseIn(&official, "e2e4"));
  thc::ChessRules previous = official;
  official.PlayMove(move);
  REQUIRE(previous.ForsythPublish() == startFen);
  REQUIRE(move.NaturalOut(&previous) == "e4");
  REQUIRE(previous.ForsythPublish() == startFen);
  const auto beforeProbe = official.ForsythPublish();
  REQUIRE(move.TerseIn(&official, "g8f6"));
  official.PushMove(move);
  official.PopMove(move);
  REQUIRE(official.ForsythPublish() == beforeProbe);
  return true;
}

std::uint64_t perft(thc::ChessRules& board, unsigned depth) {
  if (!depth) return 1;
  thc::MOVELIST list;
  board.GenLegalMoveList(&list);
  std::uint64_t nodes = 0;
  for (int i = 0; i < list.count; ++i) {
    auto move = list.moves[i];
    board.PushMove(move);
    nodes += perft(board, depth - 1);
    board.PopMove(move);
  }
  return nodes;
}

bool perftInitial() {
  thc::ChessRules board;
  REQUIRE(perft(board, 1) == 20);
  REQUIRE(perft(board, 2) == 400);
  REQUIRE(perft(board, 3) == 8902);
  REQUIRE(board.ForsythPublish() == startFen);
  return true;
}

bool perftKiwipete() {
  thc::ChessRules board;
  const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
  REQUIRE(board.Forsyth(fen));
  REQUIRE(perft(board, 1) == 48);
  REQUIRE(perft(board, 2) == 2039);
  REQUIRE(board.ForsythPublish() == fen);
  return true;
}
}  // namespace

int runEssentialTests(const char* selected, void (*report)(const char*, bool)) {
  struct Case { const char* name; bool (*run)(); };
  const Case cases[] = {
    {"internals", internals}, {"initial_coordinates", initialAndCoordinates},
    {"illegal_preserves", illegalMovesPreservePosition}, {"capture", capture},
    {"castling", castling}, {"castling_constraints", castlingConstraints},
    {"en_passant", enPassant}, {"promotions", promotions},
    {"san_disambiguation", sanDisambiguation}, {"checkmate", checkmate},
    {"stalemate", stalemate}, {"draw_queries", drawQueries},
    {"copy_undo", copyAndUndo}, {"perft_initial", perftInitial},
    {"perft_kiwipete", perftKiwipete}
  };
  int failures = 0;
  int executed = 0;
  for (const auto& test : cases) {
    if (selected && std::strcmp(selected, test.name)) continue;
    const bool passed = test.run();
    if (report) report(test.name, passed);
    if (!passed) ++failures;
    ++executed;
  }
  return executed ? failures : -1;
}
