#include "GameController.h"

namespace chessboard {

GameController::GameController(const ChessAdapter& initial) : position_(initial) {
  checkAutomaticEnd();
}

void GameController::reset() {
  *this = GameController{};
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

ConfirmationResult GameController::confirm(const Move* expected, bool allowPartial) {
  if (state_ == GameState::Finished) return {ConfirmationStatus::GameFinished, {}};

  const auto snapshot = tracker_.snapshot();
  if (state_ == GameState::AwaitingPosition) {
    return {snapshot.reliable() ? ConfirmationStatus::PositionMismatch
                                : ConfirmationStatus::UnreliableInput, {}};
  }

  const auto interpretation = interpretMove(snapshot, position_, expected ? expected->promotion : selection_, expected, allowPartial);
  ConfirmationStatus failure = ConfirmationStatus::ApplyFailed;
  switch (interpretation.status) {
    case InterpretationStatus::InProgress:
      return {ConfirmationStatus::InProgress, {}};
    case InterpretationStatus::Identified:
      if (expected && (expected->from != interpretation.move.from || expected->to != interpretation.move.to ||
                       expected->promotion != interpretation.move.promotion)) {
        failure = ConfirmationStatus::UnexpectedMove;
        break;
      }
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
      if (expected) {
        failure = ConfirmationStatus::UnexpectedMove;
        break;
      }
      state_ = GameState::AwaitingPromotion;
      return {ConfirmationStatus::PromotionRequired, {}};
    case InterpretationStatus::NoLegalMatch:
      failure = ConfirmationStatus::NoLegalMatch;
      break;
    case InterpretationStatus::Ambiguous:
      failure = ConfirmationStatus::Ambiguous;
      break;
    case InterpretationStatus::UnreliableInput:
      failure = ConfirmationStatus::UnreliableInput;
      break;
    case InterpretationStatus::PositionMismatch:
      failure = ConfirmationStatus::PositionMismatch;
      break;
  }

  // Keep the rejected attempt until a new observation matches the official position.
  selection_ = Promotion::None;
  state_ = GameState::AwaitingPosition;
  return {failure, {}};
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
  positionStatus_ = position_.status();
  if (positionStatus_.terminal == TerminalState::Checkmate) {
    const auto winner = positionStatus_.sideToMove == Color::White ? GameResult::BlackWin : GameResult::WhiteWin;
    end(EndReason::Checkmate, winner);
  } else if (positionStatus_.terminal == TerminalState::Stalemate) {
    end(EndReason::Stalemate, GameResult::Draw);
  } else if (positionStatus_.insufficientMaterial) {
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
