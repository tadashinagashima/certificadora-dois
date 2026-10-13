#include "interpreter_tests.h"
#include "MoveInterpreter.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (false)

namespace {
using namespace chessboard;

Square square(const char* name) {
  return static_cast<Square>((name[1] - '1') * 8 + name[0] - 'a');
}

Occupancy bit(const char* name) {
  return Occupancy{1} << square(name);
}

Move move(const char* from, const char* to, Promotion promotion = Promotion::None) {
  return {square(from), square(to), promotion};
}

bool hasStatus(const InterpretationResult& result, InterpretationStatus expected) {
  CHECK(result.status == expected);
  CHECK(result.move.from == kInvalidSquare);
  CHECK(result.move.to == kInvalidSquare);
  CHECK(result.move.promotion == Promotion::None);
  return true;
}

bool identified(const InterpretationResult& result, const Move& expected) {
  CHECK(result.status == InterpretationStatus::Identified);
  CHECK(result.move.from == expected.from);
  CHECK(result.move.to == expected.to);
  CHECK(result.move.promotion == expected.promotion);
  return true;
}

bool sameSnapshot(const AttemptSnapshot& a, const AttemptSnapshot& b) {
  CHECK(a.initialOccupancy == b.initialOccupancy);
  CHECK(a.currentOccupancy == b.currentOccupancy);
  CHECK(a.frameCount == b.frameCount);
  CHECK(a.initialized == b.initialized);
  CHECK(a.readingAvailable == b.readingAvailable);
  CHECK(a.historyOverflow == b.historyOverflow);
  CHECK(a.inputInterrupted == b.inputInterrupted);
  for (std::size_t i = 0; i < a.frames.size(); ++i) {
    CHECK(a.frames[i].removed == b.frames[i].removed);
    CHECK(a.frames[i].added == b.frames[i].added);
    CHECK(a.frames[i].timestampMs == b.frames[i].timestampMs);
  }
  return true;
}

bool ordinaryFrames() {
  ChessAdapter board;
  const Occupancy before = board.occupancy();
  for (int grouped = 0; grouped < 2; ++grouped) {
    MoveTracker tracker;
    tracker.observe(before, 0);
    if (!grouped) {
      tracker.observe(before & ~bit("e2"), 1);
      CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
    }
    tracker.observe((before & ~bit("e2")) | bit("e4"), 2);
    CHECK(identified(interpretMove(tracker.snapshot(), board), move("e2", "e4")));
  }
  return true;
}

bool squareLimits() {
  struct Case { const char* fen; const char* from; const char* to; };
  const Case cases[] = {
    {"4k3/8/8/8/8/8/8/R3K3 w - - 0 1", "a1", "a8"},
    {"4k2r/8/8/8/8/8/8/4K3 b - - 0 1", "h8", "h1"},
    {"4k3/8/8/8/8/8/8/4K2R w - - 0 1", "h1", "h8"},
    {"r3k3/8/8/8/8/8/8/4K3 b - - 0 1", "a8", "a1"}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    MoveTracker tracker;
    tracker.observe(board.occupancy(), 0);
    tracker.observe((board.occupancy() & ~bit(test.from)) | bit(test.to), 1);
    CHECK(identified(interpretMove(tracker.snapshot(), board), move(test.from, test.to)));
  }
  return true;
}

bool captureOrders() {
  struct Case { const char* fen; const char* from; const char* to; };
  const Case cases[] = {
    {"k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4", "d5"},
    {"4k3/8/8/4p3/3P4/8/8/K7 b - - 0 1", "e5", "d4"}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    const Occupancy before = board.occupancy();
    for (int order = 0; order < 3; ++order) {
      MoveTracker tracker;
      tracker.observe(before, 0);
      if (order < 2) tracker.observe(before & ~bit(order ? test.from : test.to), 1);
      tracker.observe(before & ~bit(test.from) & ~bit(test.to), 2);
      tracker.observe(before & ~bit(test.from), 3);
      CHECK(identified(interpretMove(tracker.snapshot(), board), move(test.from, test.to)));
    }
  }
  return true;
}

bool captureVisibility() {
  ChessAdapter board;
  CHECK(board.loadFen("k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1"));
  const Occupancy before = board.occupancy();
  MoveTracker tracker;
  tracker.observe(before, 0);
  tracker.observe(before & ~bit("e4"), 1);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  tracker.observe(before & ~bit("e4") & ~bit("d5"), 2);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  tracker.observe(before & ~bit("e4"), 3);
  CHECK(identified(interpretMove(tracker.snapshot(), board), move("e4", "d5")));
  return true;
}

bool captureAmbiguity() {
  ChessAdapter board;
  CHECK(board.loadFen("k7/8/8/2p1p3/3B4/8/8/4K3 w - - 0 1"));
  const Occupancy before = board.occupancy();
  for (int reverse = 0; reverse < 2; ++reverse) {
    MoveTracker tracker;
    tracker.observe(before, 0);
    tracker.observe(before & ~bit(reverse ? "e5" : "c5"), 1);
    tracker.observe(before, 2);
    tracker.observe(before & ~bit("d4") & ~bit(reverse ? "c5" : "e5"), 3);
    tracker.observe(before & ~bit("d4"), 4);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::Ambiguous));
  }
  return true;
}

bool intermediatePlacements() {
  ChessAdapter board;
  const Occupancy before = board.occupancy();
  MoveTracker tracker;
  tracker.observe(before, 0);
  tracker.observe(before & ~bit("e2"), 1);
  tracker.observe((before & ~bit("e2")) | bit("e3"), 2);
  tracker.observe(before & ~bit("e2"), 3);
  tracker.observe((before & ~bit("e2")) | bit("e4"), 4);
  CHECK(identified(interpretMove(tracker.snapshot(), board), move("e2", "e4")));
  return true;
}

bool castlingOrders() {
  struct Case { const char* fen; const char* from; const char* to;
    const char* rookFrom; const char* rookTo; };
  const Case cases[] = {
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "g1", "h1", "f1"},
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "c1", "a1", "d1"},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "g8", "h8", "f8"},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "c8", "a8", "d8"}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    const Occupancy before = board.occupancy();
    for (int order = 0; order < 3; ++order) {
      MoveTracker tracker;
      tracker.observe(before, 0);
      if (order < 2) {
        const char* firstFrom = order ? test.rookFrom : test.from;
        const char* firstTo = order ? test.rookTo : test.to;
        tracker.observe(before & ~bit(firstFrom), 1);
        tracker.observe((before & ~bit(firstFrom)) | bit(firstTo), 2);
        tracker.observe((before & ~bit(test.from) & ~bit(test.rookFrom)) | bit(firstTo), 3);
      }
      tracker.observe((before & ~bit(test.from) & ~bit(test.rookFrom)) |
                      bit(test.to) | bit(test.rookTo), 4);
      CHECK(identified(interpretMove(tracker.snapshot(), board), move(test.from, test.to)));
    }
  }
  return true;
}

bool castlingConstraints() {
  const char* cases[] = {
    "r3kr1r/8/8/8/8/8/8/R3K2R w KQ - 0 1",
    "r3k2r/8/8/8/8/8/8/R3K2R w Qkq - 0 1"
  };
  ChessAdapter board;
  for (const auto fen : cases) {
    CHECK(board.loadFen(fen));
    MoveTracker tracker;
    tracker.observe(board.occupancy(), 0);
    tracker.observe((board.occupancy() & ~bit("e1") & ~bit("h1")) | bit("g1") | bit("f1"), 1);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  }
  CHECK(board.loadFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
  MoveTracker tracker;
  tracker.observe(board.occupancy(), 0);
  tracker.observe((board.occupancy() & ~bit("e1")) | bit("g1"), 1);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  return true;
}

bool enPassantOrders() {
  struct Case { const char* fen; const char* from; const char* to; const char* captured; };
  const Case cases[] = {
    {"k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5", "d6", "d5"},
    {"4k3/8/8/8/3Pp3/8/8/K7 b - d3 0 1", "e4", "d3", "d4"}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    const Occupancy before = board.occupancy();
    for (int order = 0; order < 3; ++order) {
      MoveTracker tracker;
      tracker.observe(before, 0);
      if (order == 0) {
        tracker.observe(before & ~bit(test.captured), 1);
        tracker.observe(before & ~bit(test.captured) & ~bit(test.from), 2);
      } else if (order == 1) {
        tracker.observe(before & ~bit(test.from), 1);
        tracker.observe((before & ~bit(test.from)) | bit(test.to), 2);
      }
      tracker.observe((before & ~bit(test.from) & ~bit(test.captured)) | bit(test.to), 3);
      CHECK(identified(interpretMove(tracker.snapshot(), board), move(test.from, test.to)));
    }
  }
  return true;
}

bool enPassantConstraints() {
  const char* cases[] = {
    "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
    "k7/8/8/3pP3/8/8/8/4K3 w - - 0 1"
  };
  ChessAdapter board;
  for (const auto fen : cases) {
    CHECK(board.loadFen(fen));
    MoveTracker tracker;
    tracker.observe(board.occupancy(), 0);
    tracker.observe((board.occupancy() & ~bit("e5") & ~bit("d5")) | bit("d6"), 1);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  }
  CHECK(board.loadFen("k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
  MoveTracker tracker;
  tracker.observe(board.occupancy(), 0);
  tracker.observe((board.occupancy() & ~bit("e5")) | bit("d6"), 1);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  return true;
}

const Promotion promotions[] = {Promotion::Queen, Promotion::Rook, Promotion::Bishop, Promotion::Knight};

bool promotionChoices() {
  struct Case { const char* fen; const char* from; const char* to; };
  const Case cases[] = {
    {"7k/P7/8/8/8/8/8/4K3 w - - 0 1", "a7", "a8"},
    {"4k3/8/8/8/8/8/p7/7K b - - 0 1", "a2", "a1"}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    const Occupancy before = board.occupancy();
    MoveTracker tracker;
    tracker.observe(before, 0);
    tracker.observe((before & ~bit(test.from)) | bit(test.to), 1);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::PromotionRequired));
    for (const auto choice : promotions) {
      CHECK(identified(interpretMove(tracker.snapshot(), board, choice), move(test.from, test.to, choice)));
    }
    tracker.observe(before & ~bit(test.from), 2);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board, Promotion::Queen), InterpretationStatus::NoLegalMatch));
    tracker.observe((before & ~bit(test.from)) | bit(test.to), 3);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::PromotionRequired));
    CHECK(identified(interpretMove(tracker.snapshot(), board, Promotion::Knight),
                     move(test.from, test.to, Promotion::Knight)));
  }
  return true;
}

bool promotionCaptures() {
  struct Case { const char* fen; const char* from; const char* to; };
  const Case cases[] = {
    {"1r5k/P7/8/8/8/8/8/4K3 w - - 0 1", "a7", "b8"},
    {"4k3/8/8/8/8/8/p7/1R5K b - - 0 1", "a2", "b1"}
  };
  ChessAdapter board;
  for (const auto& test : cases) {
    CHECK(board.loadFen(test.fen));
    const Occupancy before = board.occupancy();
    MoveTracker tracker;
    tracker.observe(before, 0);
    tracker.observe(before & ~bit(test.from) & ~bit(test.to), 1);
    tracker.observe(before & ~bit(test.from), 2);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::PromotionRequired));
    for (const auto choice : promotions) {
      CHECK(identified(interpretMove(tracker.snapshot(), board, choice), move(test.from, test.to, choice)));
    }
    tracker.observe(before & ~bit(test.from) & ~bit(test.to), 3);
    tracker.observe(before & ~bit(test.from), 4);
    CHECK(identified(interpretMove(tracker.snapshot(), board, Promotion::Bishop),
                     move(test.from, test.to, Promotion::Bishop)));
  }
  return true;
}

bool promotionAmbiguity() {
  ChessAdapter board;
  CHECK(board.loadFen("2r1r2k/3P4/8/8/8/8/8/K7 w - - 0 1"));
  const Occupancy before = board.occupancy();
  MoveTracker tracker;
  tracker.observe(before, 0);
  tracker.observe(before & ~bit("c8") & ~bit("e8") & ~bit("d7"), 1);
  tracker.observe(before & ~bit("d7"), 2);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::Ambiguous));
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board, Promotion::Queen), InterpretationStatus::Ambiguous));
  return true;
}

bool selectionConstraints() {
  ChessAdapter board;
  MoveTracker tracker;
  tracker.observe(board.occupancy(), 0);
  tracker.observe((board.occupancy() & ~bit("e2")) | bit("e4"), 1);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board, Promotion::Queen), InterpretationStatus::NoLegalMatch));
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board, static_cast<Promotion>(255)),
                  InterpretationStatus::NoLegalMatch));
  CHECK(board.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  MoveTracker promotion;
  promotion.observe(board.occupancy(), 0);
  promotion.observe((board.occupancy() & ~bit("a7")) | bit("a8"), 1);
  CHECK(hasStatus(interpretMove(promotion.snapshot(), board, static_cast<Promotion>(255)),
                  InterpretationStatus::NoLegalMatch));
  return true;
}

bool noMove() {
  ChessAdapter board;
  MoveTracker tracker;
  tracker.observe(board.occupancy(), 0);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoMove));
  tracker.observe(board.occupancy() & ~bit("b1"), 1);
  tracker.observe(board.occupancy(), 2);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoMove));
  return true;
}

bool unreliableInput() {
  ChessAdapter board;
  MoveTracker tracker;
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::UnreliableInput));
  tracker.observe(board.occupancy(), 0);
  tracker.markInputInterrupted();
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::UnreliableInput));
  tracker.observe((board.occupancy() & ~bit("e2")) | bit("e4"), 1);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::UnreliableInput));
  MoveTracker valid;
  valid.observe(board.occupancy(), 0);
  valid.observe((board.occupancy() & ~bit("e2")) | bit("e4"), 1);
  for (int flag = 0; flag < 4; ++flag) {
    auto attempt = valid.snapshot();
    if (flag == 0) attempt.initialized = false;
    if (flag == 1) attempt.readingAvailable = false;
    if (flag == 2) attempt.historyOverflow = true;
    if (flag == 3) attempt.inputInterrupted = true;
    CHECK(hasStatus(interpretMove(attempt, board), InterpretationStatus::UnreliableInput));
  }
  return true;
}

bool malformedHistory() {
  ChessAdapter board;
  MoveTracker tracker;
  tracker.observe(board.occupancy(), 0);
  tracker.observe((board.occupancy() & ~bit("e2")) | bit("e4"), 1);
  for (int defect = 0; defect < 7; ++defect) {
    auto attempt = tracker.snapshot();
    if (defect == 0) attempt.frameCount = kHistoryCapacity + 1;
    if (defect == 1) attempt.frames[0] = FrameChanges{};
    if (defect == 2) attempt.frames[0].removed |= bit("e4");
    if (defect == 3) attempt.frames[0].removed |= bit("a3");
    if (defect == 4) attempt.frames[0].added |= bit("a1");
    if (defect == 5) attempt.currentOccupancy ^= bit("b3");
    if (defect == 6) attempt.frameCount = 0;
    CHECK(hasStatus(interpretMove(attempt, board), InterpretationStatus::UnreliableInput));
  }
  return true;
}

bool positionMismatch() {
  ChessAdapter board;
  MoveTracker tracker;
  tracker.observe(board.occupancy() & ~bit("a2"), 0);
  tracker.observe((board.occupancy() & ~bit("a2") & ~bit("e2")) | bit("e4"), 1);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::PositionMismatch));
  MoveTracker initial;
  initial.observe(board.occupancy(), 0);
  CHECK(board.apply(move("d2", "d4")));
  CHECK(hasStatus(interpretMove(initial.snapshot(), board), InterpretationStatus::PositionMismatch));
  return true;
}

bool historyCapacity() {
  ChessAdapter board;
  const Occupancy before = board.occupancy();
  MoveTracker tracker;
  tracker.observe(before, 0);
  for (std::size_t i = 0; i < kHistoryCapacity - 2; ++i) {
    tracker.observe(i % 2 ? before : before & ~bit("g1"), static_cast<std::uint32_t>(i));
  }
  tracker.observe(before & ~bit("e2"), 100);
  tracker.observe((before & ~bit("e2")) | bit("e4"), 101);
  CHECK(tracker.snapshot().frameCount == kHistoryCapacity);
  CHECK(tracker.snapshot().reliable());
  CHECK(identified(interpretMove(tracker.snapshot(), board), move("e2", "e4")));
  tracker.observe(before & ~bit("e2"), 102);
  CHECK(tracker.snapshot().historyOverflow);
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::UnreliableInput));
  return true;
}

bool discardStaleFrames() {
  ChessAdapter board;
  const Occupancy before = board.occupancy();
  MoveTracker tracker;
  tracker.observe(before, 0);
  for (int i = 0; i < 4; ++i) tracker.observe(i % 2 ? before : before & ~bit("e2"), 1);
  tracker.discardAttempt();
  CHECK(tracker.snapshot().frameCount == 0);
  CHECK(tracker.snapshot().frames[2].removed == bit("e2"));
  CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoMove));
  tracker.observe(before & ~bit("g1"), 2);
  tracker.observe((before & ~bit("g1")) | bit("f3"), 3);
  CHECK(tracker.snapshot().frameCount == 2);
  CHECK(identified(interpretMove(tracker.snapshot(), board), move("g1", "f3")));
  return true;
}

bool timestamps() {
  ChessAdapter board;
  const Occupancy before = board.occupancy();
  for (int equal = 0; equal < 2; ++equal) {
    MoveTracker tracker;
    tracker.observe(before, UINT32_MAX - 1);
    tracker.observe(before & ~bit("e2"), UINT32_MAX);
    tracker.observe((before & ~bit("e2")) | bit("e4"), equal ? UINT32_MAX : 0);
    CHECK(identified(interpretMove(tracker.snapshot(), board), move("e2", "e4")));
  }
  return true;
}

bool knightCycle(ChessAdapter& board) {
  CHECK(board.apply(move("g1", "f3")));
  CHECK(board.apply(move("g8", "f6")));
  CHECK(board.apply(move("f3", "g1")));
  CHECK(board.apply(move("f6", "g8")));
  return true;
}

bool preservesInputs() {
  ChessAdapter board;
  CHECK(knightCycle(board));
  CHECK(knightCycle(board));
  CHECK(board.status().threefoldRepetitionClaim);
  const std::string beforeFen = board.fen();
  const Occupancy beforeOccupancy = board.occupancy();
  MoveTracker tracker;
  tracker.observe(beforeOccupancy, 0);
  tracker.observe((beforeOccupancy & ~bit("e2")) | bit("e4"), 1);
  const auto attempt = tracker.snapshot();
  const auto saved = attempt;
  for (int i = 0; i < 3; ++i) {
    CHECK(identified(interpretMove(attempt, board), move("e2", "e4")));
    CHECK(hasStatus(interpretMove(attempt, board, Promotion::Queen), InterpretationStatus::NoLegalMatch));
    CHECK(board.fen() == beforeFen);
    CHECK(board.occupancy() == beforeOccupancy);
    CHECK(board.status().threefoldRepetitionClaim);
    CHECK(sameSnapshot(attempt, saved));
    CHECK(sameSnapshot(tracker.snapshot(), saved));
  }
  CHECK(knightCycle(board));
  CHECK(board.status().threefoldRepetitionClaim);
  return true;
}

bool legalConstraints() {
  ChessAdapter board;
  for (const auto candidate : {move("e2", "e5"), move("e7", "e5"), move("c1", "h6")}) {
    MoveTracker tracker;
    tracker.observe(board.occupancy(), 0);
    tracker.observe((board.occupancy() & ~(Occupancy{1} << candidate.from)) |
                    (Occupancy{1} << candidate.to), 1);
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoLegalMatch));
  }
  CHECK(board.loadFen("k3r3/8/8/8/8/8/4R3/4K3 w - - 0 1"));
  MoveTracker pinned;
  pinned.observe(board.occupancy(), 0);
  pinned.observe((board.occupancy() & ~bit("e2")) | bit("d2"), 1);
  CHECK(hasStatus(interpretMove(pinned.snapshot(), board), InterpretationStatus::NoLegalMatch));
  CHECK(board.loadFen("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"));
  MoveTracker terminal;
  terminal.observe(board.occupancy(), 0);
  terminal.observe((board.occupancy() & ~bit("h8")) | bit("g8"), 1);
  CHECK(hasStatus(interpretMove(terminal.snapshot(), board), InterpretationStatus::NoLegalMatch));
  return true;
}

bool moveSequence() {
  struct Step { const char* from; const char* to; const char* captured;
    const char* rookFrom; const char* rookTo; };
  const Step steps[] = {
    {"e2", "e4", nullptr, nullptr, nullptr}, {"d7", "d5", nullptr, nullptr, nullptr},
    {"e4", "d5", "d5", nullptr, nullptr}, {"d8", "d5", "d5", nullptr, nullptr},
    {"b1", "c3", nullptr, nullptr, nullptr}, {"d5", "d8", nullptr, nullptr, nullptr},
    {"d2", "d4", nullptr, nullptr, nullptr}, {"g8", "f6", nullptr, nullptr, nullptr},
    {"g1", "f3", nullptr, nullptr, nullptr}, {"g7", "g6", nullptr, nullptr, nullptr},
    {"c1", "f4", nullptr, nullptr, nullptr}, {"f8", "g7", nullptr, nullptr, nullptr},
    {"d1", "d2", nullptr, nullptr, nullptr}, {"e8", "g8", nullptr, "h8", "f8"},
    {"e1", "c1", nullptr, "a1", "d1"}
  };
  ChessAdapter board;
  MoveTracker tracker;
  tracker.observe(board.occupancy(), 0);
  for (const auto& step : steps) {
    const std::string before = board.fen();
    Occupancy physical = board.occupancy();
    physical &= ~bit(step.from);
    if (step.captured) physical &= ~bit(step.captured);
    tracker.observe(physical, 1);
    physical |= bit(step.to);
    if (step.rookFrom) physical = (physical & ~bit(step.rookFrom)) | bit(step.rookTo);
    tracker.observe(physical, 2);
    const auto result = interpretMove(tracker.snapshot(), board);
    CHECK(identified(result, move(step.from, step.to)));
    CHECK(board.fen() == before);
    CHECK(board.apply(result.move));
    CHECK(board.occupancy() == physical);
    tracker.discardAttempt();
    CHECK(hasStatus(interpretMove(tracker.snapshot(), board), InterpretationStatus::NoMove));
  }
  CHECK(board.pieceAt(square("c1")) == Piece::WhiteKing);
  CHECK(board.pieceAt(square("d1")) == Piece::WhiteRook);
  CHECK(board.pieceAt(square("g8")) == Piece::BlackKing);
  CHECK(board.pieceAt(square("f8")) == Piece::BlackRook);
  return true;
}
}  // namespace

int runInterpreterTests(const char* selected, void (*report)(const char*, bool)) {
  struct Case { const char* name; bool (*run)(); };
  const Case cases[] = {
    {"ordinary_frames", ordinaryFrames}, {"square_limits", squareLimits},
    {"capture_orders", captureOrders}, {"capture_visibility", captureVisibility},
    {"capture_ambiguity", captureAmbiguity}, {"intermediate_placements", intermediatePlacements},
    {"castling_orders", castlingOrders}, {"castling_constraints", castlingConstraints},
    {"en_passant_orders", enPassantOrders}, {"en_passant_constraints", enPassantConstraints},
    {"promotion_choices", promotionChoices}, {"promotion_captures", promotionCaptures},
    {"promotion_ambiguity", promotionAmbiguity}, {"selection_constraints", selectionConstraints},
    {"no_move", noMove}, {"unreliable_input", unreliableInput},
    {"malformed_history", malformedHistory}, {"position_mismatch", positionMismatch},
    {"history_capacity", historyCapacity}, {"discard_stale_frames", discardStaleFrames},
    {"timestamps", timestamps}, {"preserves_inputs", preservesInputs},
    {"legal_constraints", legalConstraints}, {"move_sequence", moveSequence}
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
