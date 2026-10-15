#include "MoveInterpreter.h"

namespace chessboard {
namespace {

bool readHistory(const AttemptSnapshot& attempt, Occupancy& removed) {
  if (!attempt.reliable() || attempt.frameCount > attempt.frames.size()) return false;
  Occupancy occupied = attempt.initialOccupancy;
  for (std::size_t i = 0; i < attempt.frameCount; ++i) {
    const auto& frame = attempt.frames[i];
    if ((frame.removed & frame.added) || (frame.removed & ~occupied) ||
        (frame.added & occupied) || !(frame.removed | frame.added)) return false;
    occupied = (occupied & ~frame.removed) | frame.added;
    removed |= frame.removed;
  }
  return occupied == attempt.currentOccupancy;
}

}  // namespace

InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection) {
  Occupancy removed = 0;
  if (!readHistory(attempt, removed)) return {InterpretationStatus::UnreliableInput, {}};
  if (attempt.initialOccupancy != position.occupancy()) {
    return {InterpretationStatus::PositionMismatch, {}};
  }
  if (attempt.currentOccupancy == attempt.initialOccupancy) {
    return {InterpretationStatus::NoMove, {}};
  }

  const auto legal = position.legalMoves();
  Move candidate;
  std::size_t matches = 0;
  bool samePromotionPath = true;
  for (std::size_t i = 0; i < legal.count; ++i) {
    const auto& move = legal.moves[i];
    if (selection != Promotion::None && move.promotion != selection) continue;

    // A capture keeps its destination occupied; the vacancy must have been observed.
    const Occupancy destination = Occupancy{1} << move.to;
    if ((attempt.initialOccupancy & destination) && !(removed & destination)) continue;

    ChessAdapter simulated = position;
    if (!simulated.apply(move) || simulated.occupancy() != attempt.currentOccupancy) continue;
    if (move.promotion == Promotion::None ||
        (matches && (candidate.from != move.from || candidate.to != move.to))) {
      samePromotionPath = false;
    }
    if (matches == 0) candidate = move;
    ++matches;
  }

  if (matches == 0) return {InterpretationStatus::NoLegalMatch, {}};
  if (selection == Promotion::None && samePromotionPath) {
    return {InterpretationStatus::PromotionRequired, {}};
  }
  if (matches > 1) return {InterpretationStatus::Ambiguous, {}};
  return {InterpretationStatus::Identified, candidate};
}

}  // namespace chessboard
