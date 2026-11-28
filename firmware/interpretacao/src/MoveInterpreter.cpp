#include "MoveInterpreter.h"

namespace chessboard {
namespace {

bool readHistory(const AttemptSnapshot& attempt, bool automatic, Occupancy& captureTargets) {
  if (!attempt.reliable() || attempt.frameCount > attempt.frames.size()) return false;
  Occupancy occupied = attempt.initialOccupancy;
  const Occupancy missing = occupied & ~attempt.currentOccupancy;
  for (std::size_t i = 0; i < attempt.frameCount; ++i) {
    const auto& frame = attempt.frames[i];
    if ((frame.removed & frame.added) || (frame.removed & ~occupied) ||
        (frame.added & occupied) || !(frame.removed | frame.added)) return false;
    occupied = (occupied & ~frame.removed) | frame.added;
    if (!automatic) {
      captureTargets |= frame.removed;
    } else if (i >= attempt.captureEvidenceFrom) {
      // For an ordinary capture only the origin ends empty. A target touched
      // and restored before the moving piece was lifted is no landing evidence.
      if (frame.removed & missing) captureTargets = 0;
      if (!(occupied & missing)) captureTargets |= frame.added;
    }
  }
  return occupied == attempt.currentOccupancy;
}

}  // namespace

InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection, const Move* expected, bool allowPartial) {
  Occupancy captureTargets = 0;
  if (!readHistory(attempt, allowPartial, captureTargets)) return {InterpretationStatus::UnreliableInput, {}};
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
  bool partial = false;
  const Occupancy changed = attempt.initialOccupancy ^ attempt.currentOccupancy;
  for (std::size_t i = 0; i < legal.count; ++i) {
    const auto& move = legal.moves[i];
    if (attempt.currentOccupancy & (Occupancy{1} << move.from)) continue;
    if (selection != Promotion::None && move.promotion != selection) continue;

    // A capture keeps its destination occupied; the vacancy must have been observed.
    const Occupancy destination = Occupancy{1} << move.to;
    if ((attempt.initialOccupancy & destination) && !(captureTargets & destination)) continue;

    ChessAdapter simulated = position;
    if (!simulated.apply(move)) continue;
    const Occupancy final = simulated.occupancy();
    if (final != attempt.currentOccupancy) {
      // King placed before rook, or pawn placed before removing the en passant victim.
      const Occupancy path = final ^ attempt.initialOccupancy;
      const Occupancy mainPiece = (Occupancy{1} << move.from) | destination;
      if (allowPartial && (path & ~mainPiece) && !(changed & ~path) &&
          (changed & mainPiece) == mainPiece) partial = true;
      continue;
    }
    // The announced move resolves ties only after its physical evidence matches.
    if (expected && move.from == expected->from && move.to == expected->to &&
        move.promotion == expected->promotion) return {InterpretationStatus::Identified, move};
    if (move.promotion == Promotion::None ||
        (matches && (candidate.from != move.from || candidate.to != move.to))) {
      samePromotionPath = false;
    }
    if (matches == 0) candidate = move;
    ++matches;
  }

  if (matches == 0) return {partial ? InterpretationStatus::InProgress : InterpretationStatus::NoLegalMatch, {}};
  if (selection == Promotion::None && samePromotionPath) {
    return {InterpretationStatus::PromotionRequired, {}};
  }
  if (matches > 1) return {InterpretationStatus::Ambiguous, {}};
  return {InterpretationStatus::Identified, candidate};
}

}  // namespace chessboard
