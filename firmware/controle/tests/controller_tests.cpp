#include "GameController.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <type_traits>
#include <utility>

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (false)

namespace {
using namespace chessboard;

static_assert(std::is_same<decltype(std::declval<const GameController&>().position()),
                           const ChessAdapter&>::value, "The official position is read-only");
static_assert(std::is_same<decltype(std::declval<const GameController&>().attempt()),
                           AttemptSnapshot>::value, "Attempt access returns a snapshot");

Square square(const char* name) {
  return static_cast<Square>((name[1] - '1') * 8 + name[0] - 'a');
}

Occupancy bit(const char* name) {
  return Occupancy{1} << square(name);
}

Move move(const char* from, const char* to, Promotion promotion = Promotion::None) {
  return {square(from), square(to), promotion};
}

bool status(const ConfirmationResult& result, ConfirmationStatus expected) {
  CHECK(result.status == expected);
  CHECK(result.move.from == kInvalidSquare);
  CHECK(result.move.to == kInvalidSquare);
  CHECK(result.move.promotion == Promotion::None);
  return true;
}

bool accepted(const ConfirmationResult& result, const Move& expected) {
  CHECK(result.status == ConfirmationStatus::Accepted);
  CHECK(result.move.from == expected.from);
  CHECK(result.move.to == expected.to);
  CHECK(result.move.promotion == expected.promotion);
  return true;
}

bool arm(GameController& game) {
  game.observe(game.position().occupancy(), 0);
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.attempt().reliable());
  CHECK(game.attempt().frameCount == 0);
  CHECK(game.attempt().initialOccupancy == game.position().occupancy());
  return true;
}

void physicalMove(GameController& game, const char* from, const char* to,
                  const char* captured = nullptr) {
  Occupancy physical = game.position().occupancy() & ~bit(from);
  if (captured) physical &= ~bit(captured);
  game.observe(physical, 1);
  game.observe(physical | bit(to), 2);
}

bool play(GameController& game, const char* from, const char* to,
          const char* captured = nullptr) {
  const auto before = game.position().fen();
  physicalMove(game, from, to, captured);
  CHECK(game.position().fen() == before);
  CHECK(accepted(game.confirm(), move(from, to)));
  CHECK(game.position().fen() != before);
  CHECK(game.position().occupancy() == game.attempt().currentOccupancy);
  CHECK(game.attempt().frameCount == 0);
  CHECK(game.attempt().initialOccupancy == game.position().occupancy());
  return true;
}

bool startupBaseline() {
  GameController game;
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(!game.attempt().initialized);
  CHECK(game.selectedPromotion() == Promotion::None);
  CHECK(game.ending().reason == EndReason::None);
  CHECK(game.ending().result == GameResult::Ongoing);
  const auto before = game.position().fen();
  CHECK(status(game.confirm(), ConfirmationStatus::UnreliableInput));
  CHECK(status(game.confirm(), ConfirmationStatus::UnreliableInput));
  CHECK(game.position().fen() == before);
  CHECK(!game.selectPromotion(Promotion::Queen));
  CHECK(arm(game));
  CHECK(status(game.confirm(), ConfirmationStatus::NoMove));
  CHECK(game.state() == GameState::Tracking);
  return true;
}

bool positionGate() {
  GameController game;
  const Occupancy official = game.position().occupancy();
  const auto fen = game.position().fen();
  game.observe(official & ~bit("e2"), 0);
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(status(game.confirm(), ConfirmationStatus::PositionMismatch));
  game.observe((official & ~bit("e2")) | bit("e4"), 1);
  CHECK(status(game.confirm(), ConfirmationStatus::PositionMismatch));
  CHECK(game.position().fen() == fen);
  game.observe(official, 2);
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.attempt().reliable());
  CHECK(game.attempt().frameCount == 0);
  CHECK(play(game, "e2", "e4"));
  return true;
}

bool confirmationOnly() {
  GameController game;
  CHECK(arm(game));
  const auto fen = game.position().fen();
  physicalMove(game, "e2", "e4");
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.position().fen() == fen);
  CHECK(game.position().sideToMove() == Color::White);
  CHECK(game.position().pieceAt(square("e2")) == Piece::WhitePawn);
  CHECK(game.position().pieceAt(square("e4")) == Piece::Empty);
  CHECK(!game.selectPromotion(Promotion::Queen));
  CHECK(accepted(game.confirm(), move("e2", "e4")));
  CHECK(game.position().pieceAt(square("e2")) == Piece::Empty);
  CHECK(game.position().pieceAt(square("e4")) == Piece::WhitePawn);
  CHECK(game.position().sideToMove() == Color::Black);
  CHECK(game.attempt().frameCount == 0);
  CHECK(game.attempt().reliable());
  return true;
}

bool duplicateConfirmation() {
  GameController game;
  CHECK(arm(game));
  CHECK(play(game, "e2", "e4"));
  const auto fen = game.position().fen();
  for (int i = 0; i < 3; ++i) {
    CHECK(status(game.confirm(), ConfirmationStatus::NoMove));
    CHECK(game.position().fen() == fen);
    CHECK(game.state() == GameState::Tracking);
  }
  auto copy = game.attempt();
  copy.currentOccupancy = 0;
  copy.initialOccupancy = 0;
  copy.inputInterrupted = true;
  CHECK(copy.currentOccupancy != game.attempt().currentOccupancy);
  CHECK(game.attempt().reliable());
  CHECK(play(game, "e7", "e5"));
  return true;
}

bool moveSequence() {
  struct Step { const char* from; const char* to; const char* captured; };
  const Step steps[] = {
    {"e2", "e4", nullptr}, {"d7", "d5", nullptr}, {"e4", "d5", "d5"},
    {"d8", "d5", "d5"}, {"b1", "c3", nullptr}, {"d5", "d8", nullptr},
    {"d2", "d4", nullptr}, {"g8", "f6", nullptr}, {"g1", "f3", nullptr},
    {"g7", "g6", nullptr}, {"c1", "f4", nullptr}, {"f8", "g7", nullptr}
  };
  GameController game;
  CHECK(arm(game));
  for (const auto& step : steps) {
    CHECK(play(game, step.from, step.to, step.captured));
    CHECK(game.state() == GameState::Tracking);
  }
  CHECK(game.position().pieceAt(square("d4")) == Piece::WhitePawn);
  CHECK(game.position().pieceAt(square("g7")) == Piece::BlackBishop);
  CHECK(game.position().pieceAt(square("e4")) == Piece::Empty);
  CHECK(game.position().sideToMove() == Color::White);
  return true;
}

bool captures() {
  struct Case { const char* fen; const char* from; const char* to; Piece piece; };
  const Case cases[] = {
    {"k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4", "d5", Piece::WhitePawn},
    {"4k3/8/8/4p3/3P4/8/8/K7 b - - 0 1", "e5", "d4", Piece::BlackPawn}
  };
  for (const auto& test : cases) {
    ChessAdapter position;
    CHECK(position.loadFen(test.fen));
    for (int order = 0; order < 3; ++order) {
      GameController game(position);
      CHECK(arm(game));
      const auto before = position.occupancy();
      if (order < 2) game.observe(before & ~bit(order ? test.from : test.to), 1);
      game.observe(before & ~bit(test.from) & ~bit(test.to), 2);
      game.observe(before & ~bit(test.from), 3);
      CHECK(accepted(game.confirm(), move(test.from, test.to)));
      CHECK(game.position().pieceAt(square(test.from)) == Piece::Empty);
      CHECK(game.position().pieceAt(square(test.to)) == test.piece);
      CHECK(game.position().occupancy() == (before & ~bit(test.from)));
    }
  }
  return true;
}

bool captureVisibility() {
  ChessAdapter position;
  CHECK(position.loadFen("k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1"));
  GameController game(position);
  CHECK(arm(game));
  game.observe(position.occupancy() & ~bit("e4"), 1);
  CHECK(status(game.confirm(), ConfirmationStatus::NoLegalMatch));
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(game.position().fen() == position.fen());
  CHECK(game.attempt().frameCount == 1);
  CHECK(arm(game));
  CHECK(play(game, "e4", "d5", "d5"));
  return true;
}

bool castling() {
  struct Case { const char* fen; const char* from; const char* to;
    const char* rookFrom; const char* rookTo; Piece king; Piece rook; };
  const Case cases[] = {
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "g1", "h1", "f1", Piece::WhiteKing, Piece::WhiteRook},
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "c1", "a1", "d1", Piece::WhiteKing, Piece::WhiteRook},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "g8", "h8", "f8", Piece::BlackKing, Piece::BlackRook},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "c8", "a8", "d8", Piece::BlackKing, Piece::BlackRook}
  };
  for (const auto& test : cases) {
    ChessAdapter position;
    CHECK(position.loadFen(test.fen));
    GameController game(position);
    CHECK(arm(game));
    const auto physical = (position.occupancy() & ~bit(test.from) & ~bit(test.rookFrom)) |
                          bit(test.to) | bit(test.rookTo);
    game.observe(physical, 1);
    CHECK(game.position().fen() == position.fen());
    CHECK(accepted(game.confirm(), move(test.from, test.to)));
    CHECK(game.position().pieceAt(square(test.to)) == test.king);
    CHECK(game.position().pieceAt(square(test.rookTo)) == test.rook);
    CHECK(game.position().pieceAt(square(test.from)) == Piece::Empty);
    CHECK(game.position().pieceAt(square(test.rookFrom)) == Piece::Empty);
    CHECK(game.position().occupancy() == physical);
  }
  return true;
}

bool enPassant() {
  struct Case { const char* fen; const char* from; const char* to; const char* captured; Piece pawn; };
  const Case cases[] = {
    {"k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5", "d6", "d5", Piece::WhitePawn},
    {"4k3/8/8/8/3Pp3/8/8/K7 b - d3 0 1", "e4", "d3", "d4", Piece::BlackPawn}
  };
  for (const auto& test : cases) {
    ChessAdapter position;
    CHECK(position.loadFen(test.fen));
    GameController game(position);
    CHECK(arm(game));
    CHECK(play(game, test.from, test.to, test.captured));
    CHECK(game.position().pieceAt(square(test.to)) == test.pawn);
    CHECK(game.position().pieceAt(square(test.captured)) == Piece::Empty);
    CHECK(game.position().occupancy() == ((position.occupancy() & ~bit(test.from) &
                                          ~bit(test.captured)) | bit(test.to)));
  }
  return true;
}

bool illegalMoves() {
  for (const auto& candidate : {move("e2", "e5"), move("e7", "e5"), move("c1", "h6")}) {
    GameController game;
    CHECK(arm(game));
    const auto fen = game.position().fen();
    game.observe((game.position().occupancy() & ~(Occupancy{1} << candidate.from)) |
                 (Occupancy{1} << candidate.to), 1);
    CHECK(status(game.confirm(), ConfirmationStatus::NoLegalMatch));
    CHECK(game.position().fen() == fen);
    CHECK(game.state() == GameState::AwaitingPosition);
    CHECK(game.attempt().frameCount == 1);
    CHECK(status(game.confirm(), ConfirmationStatus::PositionMismatch));
    CHECK(arm(game));
    CHECK(play(game, "d2", "d4"));
  }
  return true;
}

bool noMove() {
  GameController game;
  CHECK(arm(game));
  const auto before = game.position().fen();
  game.observe(game.position().occupancy() & ~bit("e2"), 1);
  game.observe(game.position().occupancy(), 2);
  CHECK(game.attempt().frameCount == 2);
  CHECK(status(game.confirm(), ConfirmationStatus::NoMove));
  CHECK(game.attempt().frameCount == 0);
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.position().fen() == before);
  CHECK(play(game, "d2", "d4"));
  return true;
}

bool ambiguous() {
  ChessAdapter position;
  CHECK(position.loadFen("r6k/8/8/8/8/8/8/Rn2K3 w - - 0 1"));
  GameController game(position);
  CHECK(arm(game));
  const auto occupied = position.occupancy();
  game.observe(occupied & ~bit("a8") & ~bit("b1"), 1);
  game.observe(occupied, 2);
  game.observe(occupied & ~bit("a1"), 3);
  CHECK(status(game.confirm(), ConfirmationStatus::Ambiguous));
  CHECK(game.position().fen() == position.fen());
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(!game.selectPromotion(Promotion::Queen));
  CHECK(game.attempt().frameCount == 3);
  return true;
}

bool interruption() {
  for (int initialized = 0; initialized < 2; ++initialized) {
    GameController game;
    const auto before = game.position().fen();
    if (initialized) {
      CHECK(arm(game));
      physicalMove(game, "e2", "e4");
    }
    game.markInputInterrupted();
    CHECK(status(game.confirm(), ConfirmationStatus::UnreliableInput));
    CHECK(game.state() == GameState::AwaitingPosition);
    CHECK(!game.attempt().reliable());
    CHECK(game.position().fen() == before);
    game.observe((game.position().occupancy() & ~bit("e2")) | bit("e4"), 3);
    CHECK(game.state() == GameState::AwaitingPosition);
    CHECK(game.position().fen() == before);
    CHECK(status(game.confirm(), initialized ? ConfirmationStatus::UnreliableInput
                                            : ConfirmationStatus::PositionMismatch));
    CHECK(arm(game));
    CHECK(!game.attempt().inputInterrupted);
    CHECK(play(game, "e2", "e4"));
  }
  return true;
}

bool overflow() {
  GameController game;
  CHECK(arm(game));
  const auto occupied = game.position().occupancy();
  for (std::uint32_t i = 0; i < kHistoryCapacity + 1; ++i) {
    game.observe(i % 2 ? occupied : occupied & ~bit("e2"), i + 1);
  }
  CHECK(game.attempt().historyOverflow);
  CHECK(status(game.confirm(), ConfirmationStatus::UnreliableInput));
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(game.position().occupancy() == occupied);
  CHECK(game.attempt().frameCount == kHistoryCapacity);
  CHECK(arm(game));
  CHECK(!game.attempt().historyOverflow);
  CHECK(play(game, "e2", "e4"));
  return true;
}

bool promotionChoices() {
  const Promotion choices[] = {Promotion::Queen, Promotion::Rook, Promotion::Bishop, Promotion::Knight};
  const Piece pieces[][4] = {
    {Piece::WhiteQueen, Piece::WhiteRook, Piece::WhiteBishop, Piece::WhiteKnight},
    {Piece::BlackQueen, Piece::BlackRook, Piece::BlackBishop, Piece::BlackKnight}
  };
  const char* fens[] = {"7k/P7/8/8/8/8/8/4K3 w - - 0 1", "4k3/8/8/8/8/8/p7/7K b - - 0 1"};
  const char* from[] = {"a7", "a2"};
  const char* to[] = {"a8", "a1"};
  for (int color = 0; color < 2; ++color) {
    ChessAdapter position;
    CHECK(position.loadFen(fens[color]));
    for (int i = 0; i < 4; ++i) {
      GameController game(position);
      CHECK(arm(game));
      physicalMove(game, from[color], to[color]);
      CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
      CHECK(game.state() == GameState::AwaitingPromotion);
      CHECK(game.selectedPromotion() == Promotion::None);
      CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
      CHECK(game.position().fen() == position.fen());
      CHECK(game.selectPromotion(choices[i]));
      CHECK(game.selectedPromotion() == choices[i]);
      CHECK(game.position().fen() == position.fen());
      CHECK(accepted(game.confirm(), move(from[color], to[color], choices[i])));
      CHECK(game.position().pieceAt(square(to[color])) == pieces[color][i]);
      CHECK(game.position().pieceAt(square(from[color])) == Piece::Empty);
      CHECK(game.selectedPromotion() == Promotion::None);
      CHECK(game.attempt().frameCount == 0);
      CHECK(game.attempt().currentOccupancy == game.position().occupancy());
    }
  }
  return true;
}

bool promotionCaptures() {
  struct Case { const char* fen; const char* from; const char* to; Piece queen; };
  const Case cases[] = {
    {"1r5k/P7/8/8/8/8/8/4K3 w - - 0 1", "a7", "b8", Piece::WhiteQueen},
    {"4k3/8/8/8/8/8/p7/1R5K b - - 0 1", "a2", "b1", Piece::BlackQueen}
  };
  for (const auto& test : cases) {
    ChessAdapter position;
    CHECK(position.loadFen(test.fen));
    GameController game(position);
    CHECK(arm(game));
    physicalMove(game, test.from, test.to, test.to);
    CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
    CHECK(game.selectPromotion(Promotion::Queen));
    CHECK(accepted(game.confirm(), move(test.from, test.to, Promotion::Queen)));
    CHECK(game.position().pieceAt(square(test.to)) == test.queen);
    CHECK(game.position().occupancy() == (position.occupancy() & ~bit(test.from)));
  }
  return true;
}

bool promotionSelection() {
  ChessAdapter position;
  CHECK(position.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  GameController game(position);
  CHECK(!game.selectPromotion(Promotion::Queen));
  CHECK(arm(game));
  CHECK(!game.selectPromotion(Promotion::Queen));
  physicalMove(game, "a7", "a8");
  CHECK(!game.selectPromotion(Promotion::Queen));
  CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(!game.selectPromotion(Promotion::None));
  CHECK(!game.selectPromotion(static_cast<Promotion>(255)));
  CHECK(game.selectedPromotion() == Promotion::None);
  for (const auto selection : {Promotion::Queen, Promotion::Rook, Promotion::Bishop, Promotion::Knight}) {
    CHECK(game.selectPromotion(selection));
    CHECK(game.selectedPromotion() == selection);
    CHECK(game.position().fen() == position.fen());
  }
  CHECK(!game.selectPromotion(Promotion::None));
  CHECK(game.selectedPromotion() == Promotion::Knight);
  CHECK(accepted(game.confirm(), move("a7", "a8", Promotion::Knight)));
  CHECK(!game.selectPromotion(Promotion::Queen));
  return true;
}

bool promotionObservations() {
  ChessAdapter position;
  CHECK(position.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  GameController game(position);
  CHECK(arm(game));
  physicalMove(game, "a7", "a8");
  CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(game.selectPromotion(Promotion::Queen));
  const auto physical = game.attempt().currentOccupancy;
  const auto frameCount = game.attempt().frameCount;
  game.observe(physical, 10);
  CHECK(game.state() == GameState::AwaitingPromotion);
  CHECK(game.selectedPromotion() == Promotion::Queen);
  CHECK(game.attempt().frameCount == frameCount);
  game.observe(physical & ~bit("a8"), 11);
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.selectedPromotion() == Promotion::None);
  game.observe(physical, 12);
  CHECK(game.attempt().frameCount == frameCount + 2);
  CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(game.selectedPromotion() == Promotion::None);
  CHECK(game.selectPromotion(Promotion::Rook));
  CHECK(accepted(game.confirm(), move("a7", "a8", Promotion::Rook)));
  CHECK(game.position().pieceAt(square("a8")) == Piece::WhiteRook);
  return true;
}

bool promotionInterruption() {
  ChessAdapter position;
  CHECK(position.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  GameController game(position);
  CHECK(arm(game));
  physicalMove(game, "a7", "a8");
  CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(game.selectPromotion(Promotion::Queen));
  game.markInputInterrupted();
  CHECK(status(game.confirm(), ConfirmationStatus::UnreliableInput));
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(game.selectedPromotion() == Promotion::None);
  CHECK(game.position().fen() == position.fen());
  CHECK(arm(game));
  physicalMove(game, "a7", "a8");
  CHECK(status(game.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(game.selectedPromotion() == Promotion::None);
  CHECK(game.selectPromotion(Promotion::Queen));
  CHECK(accepted(game.confirm(), move("a7", "a8", Promotion::Queen)));
  return true;
}

bool reset() {
  GameController game;
  CHECK(arm(game));
  CHECK(play(game, "e2", "e4"));
  CHECK(game.resign(Color::Black));
  game.reset();
  ChessAdapter initial;
  CHECK(game.position().fen() == initial.fen());
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(game.selectedPromotion() == Promotion::None);
  CHECK(game.ending().reason == EndReason::None);
  CHECK(game.ending().result == GameResult::Ongoing);
  CHECK(!game.attempt().initialized);
  CHECK(status(game.confirm(), ConfirmationStatus::UnreliableInput));
  CHECK(arm(game));
  CHECK(play(game, "d2", "d4"));
  game.markInputInterrupted();
  game.reset();
  CHECK(!game.attempt().initialized);
  CHECK(!game.attempt().inputInterrupted);
  CHECK(arm(game));
  CHECK(play(game, "e2", "e4"));
  return true;
}

bool knightCycle(ChessAdapter& position) {
  for (const auto& step : {move("g1", "f3"), move("g8", "f6"), move("f3", "g1"), move("f6", "g8")}) {
    CHECK(position.apply(step));
  }
  return true;
}

bool copyHistory() {
  ChessAdapter position;
  CHECK(knightCycle(position));
  CHECK(knightCycle(position));
  CHECK(position.status().threefoldRepetitionClaim);
  GameController game(position);
  const auto fen = position.fen();
  CHECK(game.position().status().threefoldRepetitionClaim);
  CHECK(game.state() == GameState::AwaitingPosition);
  CHECK(arm(game));
  CHECK(status(game.confirm(), ConfirmationStatus::NoMove));
  CHECK(game.position().status().threefoldRepetitionClaim);
  CHECK(play(game, "e2", "e4"));
  CHECK(!game.position().status().threefoldRepetitionClaim);
  CHECK(position.fen() == fen);
  CHECK(position.status().threefoldRepetitionClaim);
  game.reset();
  CHECK(!game.position().status().threefoldRepetitionClaim);
  return true;
}

bool repetition() {
  GameController game;
  CHECK(arm(game));
  for (int i = 0; i < 2; ++i) {
    CHECK(play(game, "g1", "f3"));
    CHECK(play(game, "g8", "f6"));
    CHECK(play(game, "f3", "g1"));
    CHECK(play(game, "f6", "g8"));
  }
  CHECK(game.position().status().threefoldRepetitionClaim);
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.ending().reason == EndReason::None);
  CHECK(game.ending().result == GameResult::Ongoing);
  CHECK(play(game, "e2", "e4"));
  CHECK(!game.position().status().threefoldRepetitionClaim);
  return true;
}

bool fiftyMove() {
  ChessAdapter position;
  CHECK(position.loadFen("r3k3/8/8/8/8/8/8/R3K3 w - - 99 1"));
  GameController game(position);
  CHECK(arm(game));
  CHECK(play(game, "a1", "a2"));
  CHECK(game.position().status().fiftyMoveClaim);
  CHECK(game.state() == GameState::Tracking);
  CHECK(game.ending().reason == EndReason::None);
  CHECK(play(game, "a8", "a2", "a2"));
  CHECK(!game.position().status().fiftyMoveClaim);
  CHECK(position.loadFen("r3k3/8/8/8/8/8/8/R3K3 w - - 100 1"));
  GameController claimable(position);
  CHECK(claimable.state() == GameState::AwaitingPosition);
  CHECK(claimable.ending().result == GameResult::Ongoing);
  return true;
}

bool checkmate() {
  GameController game;
  CHECK(arm(game));
  CHECK(play(game, "f2", "f3"));
  CHECK(play(game, "e7", "e5"));
  CHECK(play(game, "g2", "g4"));
  CHECK(play(game, "d8", "h4"));
  CHECK(game.state() == GameState::Finished);
  CHECK(game.ending().reason == EndReason::Checkmate);
  CHECK(game.ending().result == GameResult::BlackWin);
  CHECK(game.position().status().terminal == TerminalState::Checkmate);
  CHECK(status(game.confirm(), ConfirmationStatus::GameFinished));
  struct Case { const char* fen; GameResult result; };
  const Case cases[] = {
    {"7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", GameResult::WhiteWin},
    {"8/8/8/8/8/5k2/6q1/7K w - - 0 1", GameResult::BlackWin}
  };
  for (const auto& test : cases) {
    ChessAdapter position;
    CHECK(position.loadFen(test.fen));
    GameController terminal(position);
    CHECK(terminal.state() == GameState::Finished);
    CHECK(terminal.ending().reason == EndReason::Checkmate);
    CHECK(terminal.ending().result == test.result);
    CHECK(status(terminal.confirm(), ConfirmationStatus::GameFinished));
  }
  return true;
}

bool stalemate() {
  ChessAdapter position;
  CHECK(position.loadFen("7k/5K2/8/6Q1/8/8/8/8 w - - 0 1"));
  GameController game(position);
  CHECK(arm(game));
  CHECK(play(game, "g5", "g6"));
  CHECK(game.state() == GameState::Finished);
  CHECK(game.ending().reason == EndReason::Stalemate);
  CHECK(game.ending().result == GameResult::Draw);
  CHECK(position.loadFen("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"));
  GameController terminal(position);
  CHECK(terminal.state() == GameState::Finished);
  CHECK(terminal.ending().reason == EndReason::Stalemate);
  CHECK(terminal.ending().result == GameResult::Draw);
  return true;
}

bool insufficientMaterial() {
  ChessAdapter position;
  for (const auto fen : {"4k3/8/8/8/8/8/8/4K3 w - - 0 1",
                        "4k3/8/8/8/8/8/8/2B1K3 w - - 0 1",
                        "4k3/8/8/8/8/8/8/1N2K3 b - - 0 1"}) {
    CHECK(position.loadFen(fen));
    GameController game(position);
    CHECK(game.state() == GameState::Finished);
    CHECK(game.ending().reason == EndReason::InsufficientMaterial);
    CHECK(game.ending().result == GameResult::Draw);
  }
  CHECK(position.loadFen("4k3/8/8/8/8/8/3n4/2B1K3 w - - 0 1"));
  GameController game(position);
  CHECK(arm(game));
  CHECK(play(game, "c1", "d2", "d2"));
  CHECK(game.state() == GameState::Finished);
  CHECK(game.ending().reason == EndReason::InsufficientMaterial);
  CHECK(game.ending().result == GameResult::Draw);
  return true;
}

bool manualFinish() {
  GameController game;
  const auto fen = game.position().fen();
  CHECK(game.finish());
  CHECK(game.state() == GameState::Finished);
  CHECK(game.ending().reason == EndReason::Manual);
  CHECK(game.ending().result == GameResult::Unfinished);
  CHECK(game.position().fen() == fen);
  CHECK(!game.finish());
  CHECK(!game.resign(Color::White));
  CHECK(status(game.confirm(), ConfirmationStatus::GameFinished));
  ChessAdapter promotion;
  CHECK(promotion.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  GameController pending(promotion);
  CHECK(arm(pending));
  physicalMove(pending, "a7", "a8");
  CHECK(status(pending.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(pending.selectPromotion(Promotion::Queen));
  CHECK(pending.finish());
  CHECK(pending.position().fen() == promotion.fen());
  CHECK(pending.selectedPromotion() == Promotion::None);
  CHECK(pending.ending().reason == EndReason::Manual);
  CHECK(pending.ending().result == GameResult::Unfinished);
  return true;
}

bool resignation() {
  for (const auto player : {Color::White, Color::Black}) {
    GameController game;
    CHECK(arm(game));
    physicalMove(game, "e2", "e4");
    const auto fen = game.position().fen();
    CHECK(!game.resign(static_cast<Color>(255)));
    CHECK(game.state() == GameState::Tracking);
    CHECK(game.resign(player));
    CHECK(game.state() == GameState::Finished);
    CHECK(game.ending().reason == EndReason::Resignation);
    CHECK(game.ending().result == (player == Color::White ? GameResult::BlackWin : GameResult::WhiteWin));
    CHECK(game.position().fen() == fen);
    CHECK(!game.resign(player));
    CHECK(!game.finish());
  }
  ChessAdapter promotion;
  CHECK(promotion.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  GameController pending(promotion);
  CHECK(arm(pending));
  physicalMove(pending, "a7", "a8");
  CHECK(status(pending.confirm(), ConfirmationStatus::PromotionRequired));
  CHECK(pending.selectPromotion(Promotion::Queen));
  CHECK(pending.resign(Color::Black));
  CHECK(pending.selectedPromotion() == Promotion::None);
  CHECK(pending.position().fen() == promotion.fen());
  CHECK(pending.ending().result == GameResult::WhiteWin);
  return true;
}

bool finishedImmutable() {
  GameController game;
  CHECK(arm(game));
  CHECK(play(game, "e2", "e4"));
  CHECK(game.finish());
  const auto fen = game.position().fen();
  const auto attempt = game.attempt();
  game.observe(0, 100);
  game.markInputInterrupted();
  CHECK(status(game.confirm(), ConfirmationStatus::GameFinished));
  CHECK(!game.selectPromotion(Promotion::Queen));
  CHECK(!game.resign(Color::White));
  CHECK(!game.finish());
  CHECK(game.state() == GameState::Finished);
  CHECK(game.position().fen() == fen);
  CHECK(game.attempt().initialOccupancy == attempt.initialOccupancy);
  CHECK(game.attempt().currentOccupancy == attempt.currentOccupancy);
  CHECK(game.attempt().frameCount == attempt.frameCount);
  CHECK(game.attempt().reliable() == attempt.reliable());
  CHECK(game.ending().reason == EndReason::Manual);
  CHECK(game.ending().result == GameResult::Unfinished);
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) return 2;
  const char* selected = argc == 2 ? argv[1] : nullptr;
  struct Case { const char* name; bool (*run)(); };
  const Case cases[] = {
    {"startup_baseline", startupBaseline}, {"position_gate", positionGate},
    {"confirmation_only", confirmationOnly}, {"duplicate_confirmation", duplicateConfirmation},
    {"move_sequence", moveSequence}, {"captures", captures},
    {"capture_visibility", captureVisibility}, {"castling", castling},
    {"en_passant", enPassant}, {"illegal_moves", illegalMoves},
    {"no_move", noMove}, {"ambiguous", ambiguous},
    {"interruption", interruption}, {"overflow", overflow},
    {"promotion_choices", promotionChoices}, {"promotion_captures", promotionCaptures},
    {"promotion_selection", promotionSelection}, {"promotion_observations", promotionObservations},
    {"promotion_interruption", promotionInterruption}, {"reset", reset},
    {"copy_history", copyHistory}, {"repetition", repetition},
    {"fifty_move", fiftyMove}, {"checkmate", checkmate},
    {"stalemate", stalemate}, {"insufficient_material", insufficientMaterial},
    {"manual_finish", manualFinish}, {"resignation", resignation},
    {"finished_immutable", finishedImmutable}
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
