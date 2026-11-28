#pragma once

#include "MoveTracker.h"
#include "ChessAdapter.h"

namespace chessboard {

enum class InterpretationStatus {
  Identified,
  NoMove,
  NoLegalMatch,
  Ambiguous,
  Incomplete,
  PromotionRequired,
  UnreliableInput,
  PositionMismatch
};

struct InterpretationResult {
  InterpretationStatus status = InterpretationStatus::NoMove;
  // Only Identified carries a complete, legal move.
  Move move{};
  Occupancy captureTargets = 0;
};

InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection = Promotion::None,
                                   Occupancy briefLandings = 0, bool verifyingCapture = false);

}  // namespace chessboard
