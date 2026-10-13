#pragma once

#include "MoveInterpreter.h"

namespace chessboard {

enum class GameState { AwaitingPosition, Tracking, AwaitingPromotion, Finished };
enum class GameResult { Ongoing, WhiteWin, BlackWin, Draw, Unfinished };
enum class EndReason { None, Checkmate, Stalemate, InsufficientMaterial, Resignation, Manual };

struct GameEnd {
  EndReason reason = EndReason::None;
  GameResult result = GameResult::Ongoing;
};

enum class ConfirmationStatus {
  Accepted, NoMove, NoLegalMatch, Ambiguous, PromotionRequired,
  UnreliableInput, PositionMismatch, ApplyFailed, GameFinished
};

struct ConfirmationResult {
  ConfirmationStatus status = ConfirmationStatus::UnreliableInput;
  // Only Accepted carries the move applied to the official position.
  Move move{};
};

class GameController {
 public:
  GameController();
  explicit GameController(const ChessAdapter& initial);

  void reset();
  void observe(Occupancy occupancy, std::uint32_t timestampMs);
  void markInputInterrupted();
  ConfirmationResult confirm();
  bool selectPromotion(Promotion selection);
  bool resign(Color player);
  bool finish();

  GameState state() const;
  Promotion selectedPromotion() const;
  GameEnd ending() const;
  const ChessAdapter& position() const;
  AttemptSnapshot attempt() const;

 private:
  void checkAutomaticEnd();
  void end(EndReason reason, GameResult result);

  ChessAdapter position_;
  MoveTracker tracker_;
  GameState state_ = GameState::AwaitingPosition;
  Promotion selection_ = Promotion::None;
  GameEnd ending_{};
};

}  // namespace chessboard
