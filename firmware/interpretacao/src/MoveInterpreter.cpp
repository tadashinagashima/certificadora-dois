#include "MoveInterpreter.h"

namespace chessboard {
namespace {

bool readHistory(const AttemptSnapshot& attempt, Occupancy& removed, Occupancy& landed) {
  if (!attempt.reliable() || attempt.frameCount > attempt.frames.size()) return false;
  Occupancy occupied = attempt.initialOccupancy;
  const Occupancy origins = occupied & ~attempt.currentOccupancy;
  for (std::size_t i = 0; i < attempt.frameCount; ++i) {
    const auto& frame = attempt.frames[i];
    if ((frame.removed & frame.added) || (frame.removed & ~occupied) ||
        (frame.added & occupied) || !(frame.removed | frame.added)) return false;
    occupied = (occupied & ~frame.removed) | frame.added;
    removed |= frame.removed;
    if (frame.removed & origins) landed = 0; // Another lift starts a new landing.
    if ((occupied & origins) == 0) landed |= frame.added;
  }
  return occupied == attempt.currentOccupancy;
}

}  // namespace

InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection) {
  Occupancy removed = 0, landed = 0;
  if (!readHistory(attempt, removed, landed)) return {InterpretationStatus::UnreliableInput, {}};
  if (attempt.initialOccupancy != position.occupancy()) {
    return {InterpretationStatus::PositionMismatch, {}};
  }
  if (attempt.currentOccupancy == attempt.initialOccupancy) {
    return {InterpretationStatus::NoMove, {}};
  }

  const auto legal = position.legalMoves();
  Move path, chosen;
  for (std::size_t i = 0; i < legal.count; ++i) {
    const auto& move = legal.moves[i];
    if (attempt.currentOccupancy & (Occupancy{1} << move.from)) continue;

    // A capture keeps its destination occupied; the vacancy must have been observed.
    const Occupancy destination = Occupancy{1} << move.to;
    if (!(attempt.currentOccupancy & destination)) continue;
    if ((attempt.initialOccupancy & destination) && !(removed & destination)) continue;

    ChessAdapter simulated = position;
    if (!simulated.apply(move) || simulated.occupancy() != attempt.currentOccupancy) continue;
    if (path.from != kInvalidSquare && (path.from != move.from || path.to != move.to))
      return {InterpretationStatus::Ambiguous, {}};
    path = move;
    if (move.promotion == selection) chosen = move;
  }

  if (path.from == kInvalidSquare) return {InterpretationStatus::NoLegalMatch, {}};
  // A touched and replaced victim before the mover was lifted is not a capture.
  const Occupancy target = Occupancy{1} << path.to;
  if ((attempt.initialOccupancy & target) && !(landed & target))
    return {InterpretationStatus::NoLegalMatch, {}};
  if (path.promotion != Promotion::None && selection == Promotion::None)
    return {InterpretationStatus::PromotionRequired, {}};
  if (chosen.from == kInvalidSquare) return {InterpretationStatus::NoLegalMatch, {}};
  return {InterpretationStatus::Identified, chosen};
}

}  // namespace chessboard
