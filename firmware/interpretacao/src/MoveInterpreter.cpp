#include "MoveInterpreter.h"

namespace chessboard {
namespace {

bool readHistory(const AttemptSnapshot& attempt, Occupancy& touched, Occupancy& landed) {
  if (!attempt.reliable() || attempt.frameCount > attempt.frames.size()) return false;
  Occupancy occupied = attempt.initialOccupancy;
  const Occupancy origins = occupied & ~attempt.currentOccupancy;
  for (std::size_t i = 0; i < attempt.frameCount; ++i) {
    const auto& frame = attempt.frames[i];
    if ((frame.removed & frame.added) || (frame.removed & ~occupied) ||
        (frame.added & occupied) || !(frame.removed | frame.added)) return false;
    occupied = (occupied & ~frame.removed) | frame.added;
    touched |= frame.removed | frame.added;
    if (frame.removed & origins) landed = 0; // Another lift starts a new landing.
    if ((occupied & origins) == 0) landed |= frame.added;
  }
  return occupied == attempt.currentOccupancy;
}

// THC has already checked legality. Only the physical footprint is needed here.
Occupancy endpoint(const ChessAdapter& position, Occupancy initial, const Move& move) {
  const Occupancy dest = Occupancy{1} << move.to;
  Occupancy result = (initial & ~(Occupancy{1} << move.from)) | dest;
  const auto piece = position.pieceAt(move.from);
  const int delta = int(move.to) - int(move.from);
  if ((piece == Piece::WhiteKing || piece == Piece::BlackKing) && (delta == 2 || delta == -2)) {
    const int rank = move.from & 56;
    const bool shortSide = delta == 2;
    result &= ~(Occupancy{1} << (rank + (shortSide ? 7 : 0)));
    result |= Occupancy{1} << (rank + (shortSide ? 5 : 3));
  } else if ((piece == Piece::WhitePawn || piece == Piece::BlackPawn) &&
             (move.from % 8 != move.to % 8) && !(initial & dest)) {
    result &= ~(Occupancy{1} << ((move.from & 56) | (move.to & 7)));
  }
  return result;
}

}  // namespace

InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection, Occupancy briefLandings, bool verifyingCapture) {
  Occupancy touched = 0, landed = 0;
  if (!readHistory(attempt, touched, landed)) return {InterpretationStatus::UnreliableInput, {}};
  if (attempt.initialOccupancy != position.occupancy()) {
    return {InterpretationStatus::PositionMismatch, {}};
  }
  if (attempt.currentOccupancy == attempt.initialOccupancy) {
    return {InterpretationStatus::NoMove, {}};
  }

  auto legal = position.legalMoves();
  const Occupancy changed = attempt.initialOccupancy ^ attempt.currentOccupancy;
  const Occupancy missing = changed & attempt.initialOccupancy;
  Occupancy targets = 0;
  std::size_t matches = 0;
  bool incomplete = false;
  for (std::size_t i = 0; i < legal.count; ++i) {
    const Move move = legal.moves[i];
    const Occupancy source = Occupancy{1} << move.from;
    const Occupancy dest = Occupancy{1} << move.to;
    const Occupancy final = endpoint(position, attempt.initialOccupancy, move);
    if (final != attempt.currentOccupancy) {
      // Any subset can be a gesture in progress, including the rook moving
      // first or an en passant victim removed before the attacking pawn.
      const Occupancy footprint = (final ^ attempt.initialOccupancy) | dest;
      if (!(changed & ~footprint)) incomplete = true;
      continue;
    }
    if (!(source & missing)) continue;
    legal.moves[matches++] = move;
    targets |= dest;
  }
  if (!matches)
    return {incomplete ? InterpretationStatus::Incomplete : InterpretationStatus::NoLegalMatch, {}};

  if (targets & attempt.initialOccupancy) {
    // Stable replacement after lifting the mover is strong evidence. A one-scan
    // vacancy only suffices when the legal final position has a single target.
    const Occupancy stable = targets & landed;
    const Occupancy brief = verifyingCapture ? 0 : targets & briefLandings;
    const bool oneTarget = !(targets & (targets - 1));
    if (stable && !(stable & (stable - 1)) && !(brief & ~stable) &&
        (!verifyingCapture || !(touched & ~(missing | stable)))) targets = stable;
    else if (!(oneTarget && brief && !verifyingCapture))
      return {InterpretationStatus::Ambiguous, {}, targets};
  }

  Move path, chosen;
  for (std::size_t i = 0; i < matches; ++i) {
    const Move move = legal.moves[i];
    if (!(targets & (Occupancy{1} << move.to))) continue;
    if (path.from != kInvalidSquare && (path.from != move.from || path.to != move.to))
      return {InterpretationStatus::Ambiguous, {}};
    path = move;
    if (move.promotion == selection) chosen = move;
  }
  if (path.promotion != Promotion::None && selection == Promotion::None)
    return {InterpretationStatus::PromotionRequired, {}};
  if (chosen.from == kInvalidSquare) return {InterpretationStatus::NoLegalMatch, {}};
  return {InterpretationStatus::Identified, chosen};
}

}  // namespace chessboard
