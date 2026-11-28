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

  if ((state_ == GameState::AwaitingPosition || state_ == GameState::AwaitingCapture) &&
      occupancy == position_.occupancy()) {
    captureTargets_ = 0;
    tracker_.discardAttempt();
    state_ = GameState::Tracking;
  }
}

// Reacquire the endpoint after a scheduling gap or an explicit request to
// witness a capture. Crossing the gap creates no evidence of a replacement.
void GameController::resumeObservation(Occupancy occupancy, std::uint32_t now) {
  if (state_ != GameState::AwaitingPosition && state_ != GameState::Finished) {
    tracker_ = MoveTracker{};
    tracker_.observe(position_.occupancy(), now);
    selection_ = Promotion::None;
    if (state_ != GameState::AwaitingCapture) state_ = GameState::Tracking;
  }
  observe(occupancy, now);
}

void GameController::markInputInterrupted() {
  if (state_ == GameState::Finished) return;
  tracker_.markInputInterrupted();
  captureTargets_ = 0;
  selection_ = Promotion::None;
  state_ = GameState::AwaitingPosition;
}

ConfirmationResult GameController::confirm(const Move* expected, bool useSuggestedPromotion, Occupancy briefLandings) {
  if (state_ == GameState::Finished) return {ConfirmationStatus::GameFinished, {}};

  auto snapshot = tracker_.snapshot();
  if (state_ == GameState::AwaitingPosition) {
    return {snapshot.reliable() ? ConfirmationStatus::PositionMismatch
                                : ConfirmationStatus::UnreliableInput, {}};
  }

  if (snapshot.historyOverflow && !snapshot.inputInterrupted && snapshot.readingAvailable) {
    resumeObservation(snapshot.currentOccupancy, 0);
    snapshot = tracker_.snapshot();
    briefLandings = 0;
  }
  if (state_ == GameState::AwaitingCapture && snapshot.currentOccupancy != capturePosition_)
    return {ConfirmationStatus::Incomplete, {}};

  const auto piece = expected && selection_ == Promotion::None ? expected->promotion : selection_;
  const auto interpretation = interpretMove(snapshot, position_, piece, briefLandings,
                                            state_ == GameState::AwaitingCapture);
  ConfirmationStatus failure = ConfirmationStatus::ApplyFailed;
  switch (interpretation.status) {
    case InterpretationStatus::Identified:
      if (expected && expected->from == interpretation.move.from && expected->to == interpretation.move.to &&
          expected->promotion != interpretation.move.promotion) {
        selection_ = Promotion::None;
        state_ = GameState::AwaitingPromotion;
        return {ConfirmationStatus::UnexpectedPromotion, {}};
      }
      if (expected && (expected->from != interpretation.move.from || expected->to != interpretation.move.to)) {
        // In a guided castle the rook alone can also be an ordinary legal move.
        const auto king = position_.pieceAt(expected->from);
        const int delta = int(expected->to) - int(expected->from);
        if ((king == Piece::WhiteKing || king == Piece::BlackKing) && (delta == 2 || delta == -2)) {
          const int rank = expected->from & 56;
          if (interpretation.move.from == rank + (delta == 2 ? 7 : 0) &&
              interpretation.move.to == rank + (delta == 2 ? 5 : 3))
            return {ConfirmationStatus::Incomplete, {}};
        }
        failure = ConfirmationStatus::UnexpectedMove;
        break;
      }
      if (interpretation.move.promotion != Promotion::None && selection_ == Promotion::None &&
          !useSuggestedPromotion) {
        state_ = GameState::AwaitingPromotion;
        return {ConfirmationStatus::PromotionRequired, {}};
      }
      if (position_.apply(interpretation.move)) {
        captureTargets_ = 0;
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
      if (expected && expected->promotion == Promotion::None) {
        failure = ConfirmationStatus::UnexpectedMove;
        break;
      }
      state_ = GameState::AwaitingPromotion;
      return {ConfirmationStatus::PromotionRequired, {}};
    case InterpretationStatus::NoLegalMatch:
      failure = ConfirmationStatus::NoLegalMatch;
      break;
    case InterpretationStatus::Incomplete:
      return {ConfirmationStatus::Incomplete, {}};
    case InterpretationStatus::Ambiguous:
      if (interpretation.captureTargets) {
        resumeObservation(snapshot.currentOccupancy, 0);
        capturePosition_ = snapshot.currentOccupancy;
        captureTargets_ = interpretation.captureTargets;
        state_ = GameState::AwaitingCapture;
        return {ConfirmationStatus::VerifyCapture, {}};
      }
      failure = ConfirmationStatus::Ambiguous;
      break;
    case InterpretationStatus::UnreliableInput:
      failure = ConfirmationStatus::UnreliableInput;
      break;
    case InterpretationStatus::PositionMismatch:
      failure = ConfirmationStatus::PositionMismatch;
      break;
  }

  captureTargets_ = 0;
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
  captureTargets_ = 0;
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
