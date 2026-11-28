#include "BoardApp.h"
#include <utility>

namespace principal {
using namespace chessboard;

BoardApp::BoardApp() { start(); }

bool BoardApp::start(const ChessAdapter& initial) {
  GameController next(initial);
  GameRecord nextRecord;
  auto result = nextRecord.begin(next.position());
  if (result == RecordStatus::Ok && next.state() == GameState::Finished)
    result = nextRecord.finish(next.position(), next.ending());
  if (result != RecordStatus::Ok) { fault_ = result; return false; }
  game_ = std::move(next);
  record_ = std::move(nextRecord);
  fault_ = RecordStatus::Ok;
  haveRaw_ = haveStable_ = rejecting_ = saved_ = false;
  adjustment_ = ConfirmationStatus::NoMove;
  landed_ = evaluateMove_ = false;
  updateSuggestion();
  return true;
}

void BoardApp::setComputerMode(bool enabled) {
  computerMode_ = enabled;
  updateSuggestion();
  evaluateMove_ = landed_;
}

void BoardApp::updateSuggestion() {
  suggestion_ = computerMode_ && !record_.finished() && game_.position().sideToMove() == Color::Black
      ? game_.position().computerMove() : Move{};
}

std::string BoardApp::suggestedCoordinates() const {
  if (suggestion_.from == kInvalidSquare) return {};
  std::string text{board::squareFile(suggestion_.from), board::squareRank(suggestion_.from), '>',
                   board::squareFile(suggestion_.to), board::squareRank(suggestion_.to)};
  if (suggestion_.promotion != Promotion::None) {
    text += '=';
    text += " QRBN"[static_cast<unsigned>(suggestion_.promotion)];
  }
  return text;
}

void BoardApp::inputLost() {
  game_.markInputInterrupted();
  haveRaw_ = haveStable_ = false;
  adjustment_ = ConfirmationStatus::NoMove;
  landed_ = evaluateMove_ = false;
}

void BoardApp::sample(Occupancy raw, std::uint32_t now) {
  if (haveRaw_ && now - lastSample_ > board::MAX_SAMPLE_GAP_MS) inputLost();
  lastSample_ = now;
  if (!haveRaw_) {
    raw_ = filtered_ = raw;
    changed_.fill(now);
    haveRaw_ = true;
    return;
  }
  bool initialStable = true;
  const auto previous = filtered_;
  for (unsigned i = 0; i < 64; ++i) {
    const Occupancy bit = Occupancy{1} << i;
    if ((raw ^ raw_) & bit) changed_[i] = now;
    if (now - changed_[i] >= board::REED_DEBOUNCE_MS)
      filtered_ = (filtered_ & ~bit) | (raw & bit);
    else initialStable = false;
  }
  raw_ = raw;
  if (!haveStable_ && !initialStable) return;
  haveStable_ = true;
  game_.observe(filtered_, now);
  if (previous != filtered_ && game_.state() == GameState::Tracking) {
    lastChange_ = now;
    if (filtered_ == game_.position().occupancy()) {
      // A piece returned home. Drop old vacancies before the next capture.
      game_.clearAttempt();
      adjustment_ = ConfirmationStatus::NoMove;
      landed_ = evaluateMove_ = false;
    } else {
      landed_ |= (filtered_ & ~previous) != 0;
      evaluateMove_ = landed_;
    }
  }
}

bool BoardApp::settled(std::uint32_t now) const {
  return haveStable_ && raw_ == filtered_ &&
         now - lastSample_ <= board::MAX_SAMPLE_GAP_MS;
}

void BoardApp::reject(std::uint32_t now) { rejecting_ = true; rejectedAt_ = now; }

ConfirmationStatus BoardApp::confirm(std::uint32_t now, bool allowPartial) {
  if (fault_ != RecordStatus::Ok) { reject(now); return ConfirmationStatus::ApplyFailed; }
  if (game_.state() == GameState::Finished) return ConfirmationStatus::GameFinished;
  if (!settled(now)) { reject(now); return ConfirmationStatus::UnreliableInput; }
  // Publish only after both position and PGN accept; preserve repetition history.
  GameController next = game_;
  const auto confirmation = next.confirm(suggestion_.from != kInvalidSquare ? &suggestion_ : nullptr, allowPartial);
  const auto result = confirmation.status;
  if (allowPartial && (result == ConfirmationStatus::NoLegalMatch || result == ConfirmationStatus::Ambiguous ||
                       result == ConfirmationStatus::UnexpectedMove || result == ConfirmationStatus::InProgress)) {
    // Keep the live attempt, including its landings. Removing an extra
    // magnet can complete the move without another lift at the origin.
    if (result == ConfirmationStatus::Ambiguous) game_.requireFreshCaptureTarget();
    if (result != ConfirmationStatus::NoLegalMatch || adjustment_ != ConfirmationStatus::Ambiguous)
      adjustment_ = result == ConfirmationStatus::InProgress ? ConfirmationStatus::NoMove : result;
    return result;
  }
  adjustment_ = ConfirmationStatus::NoMove;
  landed_ = evaluateMove_ = false;
  if (result == ConfirmationStatus::Accepted) {
    const auto recorded = record_.record(game_.position(), confirmation, next.position(), next.ending());
    if (recorded != RecordStatus::Ok) {
      fault_ = recorded;
      reject(now);
      return ConfirmationStatus::ApplyFailed;
    }
    rejecting_ = false;
  }
  game_ = std::move(next);
  if (result == ConfirmationStatus::Accepted) updateSuggestion();
  if (result != ConfirmationStatus::Accepted && result != ConfirmationStatus::NoMove &&
      result != ConfirmationStatus::PromotionRequired) reject(now);
  return result;
}

bool BoardApp::promote(Promotion piece) {
  if (fault_ != RecordStatus::Ok || !game_.selectPromotion(piece)) return false;
  evaluateMove_ = true;
  return true;
}

bool BoardApp::finish(bool resignation, Color player) {
  if (fault_ != RecordStatus::Ok) return false;
  GameController next = game_;
  if (!(resignation ? next.resign(player) : next.finish())) return false;
  const auto result = record_.finish(next.position(), next.ending());
  if (result != RecordStatus::Ok) { fault_ = result; return false; }
  game_ = std::move(next);
  suggestion_ = {};
  adjustment_ = ConfirmationStatus::NoMove;
  return true;
}

void BoardApp::tick(std::uint32_t now) {
  if (haveRaw_ && now - lastSample_ > board::MAX_SAMPLE_GAP_MS) inputLost();
  if (rejecting_ && now - rejectedAt_ >= 1800) rejecting_ = false;
  if (evaluateMove_ && settled(now) && now - lastChange_ >= board::MOVE_SETTLE_MS) {
    evaluateMove_ = false;
    confirm(now, true);
  }
}

Indicators BoardApp::indicators(std::uint32_t now) const {
  Indicators out;
  if (rejecting_ && now - rejectedAt_ < 400) {
    out.red = true;
    out.buzzer = now - rejectedAt_ < 120;
  } else if (fault_ != RecordStatus::Ok) out.red = true;
  else if (game_.state() == GameState::Finished) {
    out.green = saved_ && (now % 1000 < 500);
    out.red = !saved_ && (now % 500 < 250);
  } else if (!settled(now) || game_.state() == GameState::AwaitingPosition) out.red = true;
  else if (game_.state() == GameState::AwaitingPromotion) {
    unsigned pulses = 0;
    switch (game_.selectedPromotion()) {
      case Promotion::Queen: pulses = 1; break;
      case Promotion::Rook: pulses = 2; break;
      case Promotion::Bishop: pulses = 3; break;
      case Promotion::Knight: pulses = 4; break;
      default: break;
    }
    const auto phase = now % 2000;
    out.red = true;
    out.green = pulses ? phase < pulses * 300 && phase % 300 < 150 : phase < 1000;
  } else out.green = true;
  return out;
}

} // namespace principal
