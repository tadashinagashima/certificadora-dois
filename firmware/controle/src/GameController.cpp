#include "GameController.h"

namespace chessboard {

GameController::GameController() {
  checkAutomaticEnd();
}

GameController::GameController(const ChessAdapter& initial) : position_(initial) {
  checkAutomaticEnd();
}

void GameController::reset() {
  position_.reset();
  tracker_ = MoveTracker{};
  state_ = GameState::AwaitingPosition;
  selection_ = Promotion::None;
  ending_ = GameEnd{};
  checkAutomaticEnd();
}

void GameController::observe(Occupancy occupancy, std::uint32_t timestampMs) {
  if (state_ == GameState::Finished) return;

  if (state_ == GameState::AwaitingPromotion &&
      occupancy != tracker_.snapshot().currentOccupancy) {
    selection_ = Promotion::None;
    state_ = GameState::Tracking;
  }
  tracker_.observe(occupancy, timestampMs);

  if (state_ == GameState::AwaitingPosition && occupancy == position_.occupancy()) {
    tracker_.discardAttempt();
    state_ = GameState::Tracking;
  }
}

void GameController::markInputInterrupted() {
  if (state_ == GameState::Finished) return;
  tracker_.markInputInterrupted();
  selection_ = Promotion::None;
  state_ = GameState::AwaitingPosition;
}

ConfirmationResult GameController::confirm() {
  if (state_ == GameState::Finished) return {ConfirmationStatus::GameFinished, {}};

  const auto snapshot = tracker_.snapshot();
  if (state_ == GameState::AwaitingPosition) {
    return {snapshot.reliable() ? ConfirmationStatus::PositionMismatch
                                : ConfirmationStatus::UnreliableInput, {}};
  }

  const auto interpretation = interpretMove(snapshot, position_, selection_);
  switch (interpretation.status) {
    case InterpretationStatus::Identified:
      if (position_.apply(interpretation.move)) {
        tracker_.discardAttempt();
        selection_ = Promotion::None;
        state_ = GameState::Tracking;
        checkAutomaticEnd();
        return {ConfirmationStatus::Accepted, interpretation.move};
      }
      break;
    case InterpretationStatus::NoMove:
      tracker_.discardAttempt();
      selection_ = Promotion::None;
      state_ = GameState::Tracking;
      return {ConfirmationStatus::NoMove, {}};
    case InterpretationStatus::PromotionRequired:
      state_ = GameState::AwaitingPromotion;
      return {ConfirmationStatus::PromotionRequired, {}};
    default:
      break;
  }

  // A rejected attempt is retained until a new observation matches the position.
  selection_ = Promotion::None;
  state_ = GameState::AwaitingPosition;
  switch (interpretation.status) {
    case InterpretationStatus::NoLegalMatch:
      return {ConfirmationStatus::NoLegalMatch, {}};
    case InterpretationStatus::Ambiguous:
      return {ConfirmationStatus::Ambiguous, {}};
    case InterpretationStatus::UnreliableInput:
      return {ConfirmationStatus::UnreliableInput, {}};
    case InterpretationStatus::PositionMismatch:
      return {ConfirmationStatus::PositionMismatch, {}};
    default:
      return {ConfirmationStatus::ApplyFailed, {}};
  }
}

bool GameController::selectPromotion(Promotion selection) {
  if (state_ != GameState::AwaitingPromotion) return false;
  switch (selection) {
    case Promotion::Queen:
    case Promotion::Rook:
    case Promotion::Bishop:
    case Promotion::Knight:
      selection_ = selection;
      return true;
    default:
      return false;
  }
}

bool GameController::resign(Color player) {
  if (state_ == GameState::Finished) return false;
  if (player != Color::White && player != Color::Black) return false;
  end(EndReason::Resignation, player == Color::White ? GameResult::BlackWin
                                                   : GameResult::WhiteWin);
  return true;
}

bool GameController::finish() {
  if (state_ == GameState::Finished) return false;
  end(EndReason::Manual, GameResult::Unfinished);
  return true;
}

void GameController::checkAutomaticEnd() {
  const auto status = position_.status();
  if (status.terminal == TerminalState::Checkmate) {
    end(EndReason::Checkmate, status.sideToMove == Color::White ? GameResult::BlackWin
                                                             : GameResult::WhiteWin);
  } else if (status.terminal == TerminalState::Stalemate) {
    end(EndReason::Stalemate, GameResult::Draw);
  } else if (status.insufficientMaterial) {
    end(EndReason::InsufficientMaterial, GameResult::Draw);
  }
}

void GameController::end(EndReason reason, GameResult result) {
  state_ = GameState::Finished;
  ending_ = {reason, result};
  selection_ = Promotion::None;
}

GameState GameController::state() const { return state_; }
Promotion GameController::selectedPromotion() const { return selection_; }
GameEnd GameController::ending() const { return ending_; }
const ChessAdapter& GameController::position() const { return position_; }
AttemptSnapshot GameController::attempt() const { return tracker_.snapshot(); }

}  // namespace chessboard
