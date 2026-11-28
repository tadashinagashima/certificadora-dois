#pragma once

#include "MoveTracker.h"
#include "ChessAdapter.h"

namespace chessboard {

enum class InterpretationStatus {
  Identified,
  InProgress,
  NoMove,
  NoLegalMatch,
  Ambiguous,
  PromotionRequired,
  UnreliableInput,
  PositionMismatch
};

struct InterpretationResult {
  InterpretationStatus status = InterpretationStatus::NoMove;
  // Only Identified carries a complete, legal move.
  Move move{};
};

InterpretationResult interpretMove(const AttemptSnapshot& attempt,
                                   const ChessAdapter& position,
                                   Promotion selection = Promotion::None,
                                   const Move* expected = nullptr, bool allowPartial = false);

}  // namespace chessboard
