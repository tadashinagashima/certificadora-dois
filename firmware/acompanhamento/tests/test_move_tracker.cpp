#include "MoveTracker.h"

#include <cstring>
#include <initializer_list>
#include <iostream>

using chessboard::AttemptSnapshot;
using chessboard::MoveTracker;
using chessboard::Occupancy;
using chessboard::kHistoryCapacity;

// Checks stay active in Release builds, unlike assert().
#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n";           \
      return false;                                                             \
    }                                                                           \
  } while (false)

constexpr Occupancy square(char file, char rank) {
  return Occupancy{1} << (8 * (rank - '1') + file - 'a');
}

constexpr Occupancy e2 = square('e', '2');
constexpr Occupancy e3 = square('e', '3');
constexpr Occupancy e4 = square('e', '4');
constexpr Occupancy d5 = square('d', '5');
constexpr Occupancy h8 = square('h', '8');

bool frameEquals(const AttemptSnapshot& attempt, std::size_t index,
                 Occupancy removed, Occupancy added, std::uint32_t timestampMs) {
  CHECK(index < attempt.frameCount);
  CHECK(attempt.frames[index].removed == removed);
  CHECK(attempt.frames[index].added == added);
  CHECK(attempt.frames[index].timestampMs == timestampMs);
  return true;
}

struct Step {
  Occupancy occupancy;
  Occupancy removed;
  Occupancy added;
};

// Fixtures represent observed occupancy, not complete legal chess positions.
bool sequence(Occupancy initial, std::initializer_list<Step> steps) {
  MoveTracker tracker;
  tracker.observe(initial, 0);
  std::size_t index = 0;
  std::uint32_t time = 25;
  for (const auto& step : steps) {
    tracker.observe(step.occupancy, time);
    const auto attempt = tracker.snapshot();
    CHECK(attempt.reliable());
    CHECK(attempt.initialOccupancy == initial);
    CHECK(attempt.currentOccupancy == step.occupancy);
    CHECK(attempt.frameCount == index + 1);
    CHECK(frameEquals(attempt, index, step.removed, step.added, time));
    ++index;
    time += 25;
  }
  return true;
}

bool noBaseline() {
  MoveTracker tracker;
  const auto attempt = tracker.snapshot();
  CHECK(!attempt.initialized);
  CHECK(!attempt.readingAvailable);
  CHECK(!attempt.reliable());
  CHECK(!attempt.historyOverflow && !attempt.inputInterrupted);
  CHECK(attempt.frameCount == 0);
  tracker.discardAttempt();
  CHECK(!tracker.snapshot().initialized);
  CHECK(!tracker.snapshot().reliable());
  return true;
}

bool initialization() {
  for (Occupancy initial : {Occupancy{0}, Occupancy{0xFFFF00000000FFFFULL}}) {
    MoveTracker tracker;
    tracker.observe(initial, 100);
    const auto attempt = tracker.snapshot();
    CHECK(attempt.initialized && attempt.readingAvailable && attempt.reliable());
    CHECK(attempt.initialOccupancy == initial);
    CHECK(attempt.currentOccupancy == initial);
    CHECK(attempt.frameCount == 0);
  }
  return true;
}

bool unchangedFrames() {
  MoveTracker tracker;
  tracker.observe(e2, 0);
  for (std::uint32_t time = 1; time <= 1000; ++time) tracker.observe(e2, time);
  CHECK(tracker.snapshot().frameCount == 0);
  CHECK(tracker.snapshot().reliable());
  tracker.observe(0, 1001);
  tracker.observe(0, 1002);
  CHECK(tracker.snapshot().frameCount == 1);
  CHECK(frameEquals(tracker.snapshot(), 0, e2, 0, 1001));
  return true;
}

bool quietMove() {
  return sequence(e2, {{0, e2, 0}, {e4, 0, e4}});
}

bool replacement() {
  return sequence(e2, {{0, e2, 0}, {e2, 0, e2}});
}

bool captureAttackerFirst() {
  return sequence(e4 | d5, {{d5, e4, 0}, {0, d5, 0}, {d5, 0, d5}});
}

bool captureVictimFirst() {
  return sequence(e4 | d5, {{e4, d5, 0}, {0, e4, 0}, {d5, 0, d5}});
}

bool captureGrouped() {
  return sequence(e4 | d5, {{0, e4 | d5, 0}, {d5, 0, d5}});
}

bool castlingKingside() {
  const auto king = square('e', '1');
  const auto rook = square('h', '1');
  const auto kingEnd = square('g', '1');
  const auto rookEnd = square('f', '1');
  return sequence(king | rook, {{rook, king, 0}, {rook | kingEnd, 0, kingEnd},
                               {kingEnd, rook, 0}, {kingEnd | rookEnd, 0, rookEnd}});
}

bool castlingQueenside() {
  const auto king = square('e', '1');
  const auto rook = square('a', '1');
  const auto kingEnd = square('c', '1');
  const auto rookEnd = square('d', '1');
  return sequence(king | rook, {{rook, king, 0}, {rook | kingEnd, 0, kingEnd},
                               {kingEnd, rook, 0}, {kingEnd | rookEnd, 0, rookEnd}});
}

bool enPassant() {
  const auto pawn = square('e', '5');
  const auto destination = square('d', '6');
  CHECK(sequence(pawn | d5, {{d5, pawn, 0}, {d5 | destination, 0, destination},
                             {destination, d5, 0}}));
  return sequence(pawn | d5, {{pawn, d5, 0}, {0, pawn, 0}, {destination, 0, destination}});
}

bool promotion() {
  const auto pawn = square('a', '7');
  const auto destination = square('a', '8');
  // Replacing the pawn at the destination must not erase that intermediate lift.
  CHECK(sequence(pawn, {{0, pawn, 0}, {destination, 0, destination},
                         {0, destination, 0}, {destination, 0, destination}}));
  // Replacement can also happen before placing the promoted piece.
  return sequence(pawn, {{0, pawn, 0}, {destination, 0, destination}});
}

bool groupedChanges() {
  const auto initial = square('e', '1') | square('h', '1');
  const auto final = square('f', '1') | square('g', '1');
  CHECK(sequence(initial, {{final, initial, final}}));
  return sequence(0, {{Occupancy{0xFFFFFFFFFFFFFFFFULL}, 0, Occupancy{0xFFFFFFFFFFFFFFFFULL}},
                      {0, Occupancy{0xFFFFFFFFFFFFFFFFULL}, 0}});
}

bool all64Squares() {
  static_assert(square('a', '1') == 1, "a1 fixture");
  static_assert(square('h', '1') == 128, "h1 fixture");
  static_assert(square('a', '8') == Occupancy{0x0100000000000000ULL}, "a8 fixture");
  static_assert(h8 == Occupancy{0x8000000000000000ULL}, "h8 fixture");
  for (unsigned index = 0; index < 64; ++index) {
    const Occupancy bit = Occupancy{1} << index;
    CHECK(sequence(0, {{bit, 0, bit}, {0, bit, 0}}));
  }
  return true;
}

bool discardAndCorrection() {
  MoveTracker tracker;
  tracker.observe(e2, 0);
  tracker.observe(0, 25);
  tracker.observe(e3, 50);
  tracker.discardAttempt();
  const auto fresh = tracker.snapshot();
  CHECK(fresh.initialOccupancy == e3 && fresh.currentOccupancy == e3);
  CHECK(fresh.frameCount == 0 && fresh.reliable());
  tracker.observe(0, 75);
  tracker.observe(e4, 100);
  const auto corrected = tracker.snapshot();
  CHECK(corrected.initialOccupancy == e3 && corrected.currentOccupancy == e4);
  CHECK(corrected.frameCount == 2);
  CHECK(frameEquals(corrected, 0, e3, 0, 75));
  CHECK(frameEquals(corrected, 1, 0, e4, 100));
  tracker.discardAttempt();
  tracker.observe(e4, 125);
  CHECK(tracker.snapshot().frameCount == 0);
  return true;
}

bool snapshotIndependence() {
  MoveTracker tracker;
  tracker.observe(e2, 0);
  tracker.observe(0, 25);
  auto saved = tracker.snapshot();
  tracker.observe(e4, 50);
  tracker.discardAttempt();
  CHECK(saved.initialOccupancy == e2 && saved.currentOccupancy == 0);
  CHECK(saved.frameCount == 1 && saved.reliable());
  CHECK(frameEquals(saved, 0, e2, 0, 25));
  saved.currentOccupancy = h8;
  saved.frames[0].added = h8;
  CHECK(tracker.snapshot().currentOccupancy == e4);
  CHECK(tracker.snapshot().frameCount == 0);
  tracker.observe(0, 75);
  CHECK(tracker.snapshot().frameCount == 1);
  CHECK(frameEquals(tracker.snapshot(), 0, e4, 0, 75));
  return true;
}

void fillHistory(MoveTracker& tracker) {
  tracker.observe(e2, 0);
  for (std::size_t i = 0; i < kHistoryCapacity; ++i) {
    tracker.observe(i % 2 == 0 ? 0 : e2, static_cast<std::uint32_t>(i + 1));
  }
}

bool capacityAndOverflow() {
  MoveTracker tracker;
  fillHistory(tracker);
  const auto full = tracker.snapshot();
  CHECK(full.frameCount == kHistoryCapacity && full.reliable());
  for (unsigned i = 0; i < 100; ++i) tracker.observe(full.currentOccupancy, 100);
  CHECK(tracker.snapshot().reliable());
  tracker.observe(h8, 200);
  tracker.observe(d5, 225);
  const auto lost = tracker.snapshot();
  CHECK(lost.historyOverflow && !lost.inputInterrupted && !lost.reliable());
  CHECK(lost.readingAvailable && lost.currentOccupancy == d5);
  CHECK(lost.initialOccupancy == e2 && lost.frameCount == full.frameCount);
  for (std::size_t i = 0; i < full.frameCount; ++i) {
    CHECK(frameEquals(lost, i, full.frames[i].removed, full.frames[i].added,
                      full.frames[i].timestampMs));
  }
  tracker.discardAttempt();
  CHECK(tracker.snapshot().reliable() && tracker.snapshot().frameCount == 0);
  CHECK(tracker.snapshot().initialOccupancy == d5);
  tracker.observe(0, 250);
  CHECK(frameEquals(tracker.snapshot(), 0, d5, 0, 250));
  return true;
}

bool interruption() {
  MoveTracker tracker;
  tracker.observe(e2, 0);
  tracker.observe(0, 25);
  tracker.markInputInterrupted();
  tracker.markInputInterrupted();
  const auto interrupted = tracker.snapshot();
  CHECK(!interrupted.readingAvailable && interrupted.inputInterrupted);
  CHECK(!interrupted.reliable() && !interrupted.historyOverflow);
  CHECK(interrupted.currentOccupancy == 0 && interrupted.initialOccupancy == e2);
  CHECK(interrupted.frameCount == 1);
  tracker.observe(e4, 100);
  tracker.observe(0, 125);
  const auto resumed = tracker.snapshot();
  CHECK(resumed.readingAvailable && resumed.inputInterrupted && !resumed.reliable());
  CHECK(resumed.currentOccupancy == 0 && resumed.frameCount == 1);
  CHECK(frameEquals(resumed, 0, e2, 0, 25));
  tracker.discardAttempt();
  CHECK(tracker.snapshot().reliable());
  tracker.observe(e4, 150);
  CHECK(frameEquals(tracker.snapshot(), 0, 0, e4, 150));
  return true;
}

bool interruptionBeforeBaseline() {
  MoveTracker tracker;
  tracker.markInputInterrupted();
  CHECK(!tracker.snapshot().initialized && !tracker.snapshot().reliable());
  tracker.observe(e2, 100);
  CHECK(tracker.snapshot().reliable());
  CHECK(tracker.snapshot().frameCount == 0);
  CHECK(tracker.snapshot().initialOccupancy == e2);
  return true;
}

bool discardDuringInterruption() {
  MoveTracker tracker;
  tracker.observe(e2, 0);
  tracker.markInputInterrupted();
  CHECK(tracker.snapshot().currentOccupancy == e2);  // Failure is not an empty board.
  tracker.discardAttempt();
  CHECK(!tracker.snapshot().reliable() && !tracker.snapshot().readingAvailable);
  tracker.observe(e4, 100);
  CHECK(!tracker.snapshot().reliable());
  CHECK(tracker.snapshot().frameCount == 0);  // No delta across an acquisition gap.
  tracker.discardAttempt();
  CHECK(tracker.snapshot().reliable() && tracker.snapshot().initialOccupancy == e4);
  return true;
}

bool combinedLosses() {
  MoveTracker tracker;
  fillHistory(tracker);
  tracker.observe(h8, 100);
  tracker.markInputInterrupted();
  const auto lost = tracker.snapshot();
  CHECK(lost.historyOverflow && lost.inputInterrupted && !lost.reliable());
  CHECK(!lost.readingAvailable && lost.currentOccupancy == h8);
  tracker.discardAttempt();
  CHECK(!tracker.snapshot().historyOverflow);
  CHECK(tracker.snapshot().inputInterrupted && !tracker.snapshot().reliable());
  return true;
}

bool timestampWrap() {
  MoveTracker tracker;
  tracker.observe(e2, 0xFFFFFF00U);
  tracker.observe(0, 0xFFFFFFF0U);
  tracker.observe(e4, 5);
  tracker.observe(0, 5);  // Equal timestamps do not merge separate observations.
  const auto attempt = tracker.snapshot();
  CHECK(attempt.reliable() && attempt.frameCount == 3);
  CHECK(frameEquals(attempt, 0, e2, 0, 0xFFFFFFF0U));
  CHECK(frameEquals(attempt, 1, 0, e4, 5));
  CHECK(frameEquals(attempt, 2, e4, 0, 5));
  return true;
}

struct TestCase {
  const char* name;
  bool (*run)();
};

int main(int argc, char** argv) {
  const TestCase cases[] = {
      {"no_baseline", noBaseline}, {"initialization", initialization},
      {"unchanged_frames", unchangedFrames}, {"quiet_move", quietMove},
      {"replacement", replacement}, {"capture_attacker_first", captureAttackerFirst},
      {"capture_victim_first", captureVictimFirst}, {"capture_grouped", captureGrouped},
      {"castling_kingside", castlingKingside}, {"castling_queenside", castlingQueenside},
      {"en_passant", enPassant}, {"promotion", promotion},
      {"grouped_changes", groupedChanges}, {"all_64_squares", all64Squares},
      {"discard_and_correction", discardAndCorrection},
      {"snapshot_independence", snapshotIndependence},
      {"capacity_and_overflow", capacityAndOverflow}, {"interruption", interruption},
      {"interruption_before_baseline", interruptionBeforeBaseline},
      {"discard_during_interruption", discardDuringInterruption},
      {"combined_losses", combinedLosses}, {"timestamp_wrap", timestampWrap},
  };
  if (argc > 2) return 2;
  unsigned executed = 0;
  for (const auto& test : cases) {
    if (argc == 2 && std::strcmp(argv[1], test.name) != 0) continue;
    if (!test.run()) {
      std::cerr << "FAIL " << test.name << '\n';
      return 1;
    }
    std::cout << "PASS " << test.name << '\n';
    ++executed;
  }
  if (!executed) {
    std::cerr << "Unknown test case\n";
    return 2;
  }
  std::cout << executed << " test(s); MoveTracker=" << sizeof(MoveTracker) << " bytes\n";
  return 0;
}
