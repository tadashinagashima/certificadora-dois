#include "BoardApp.h"
#include <utility>

namespace principal {
using namespace chessboard;

BoardApp::BoardApp() { start(); }

bool BoardApp::start(const ChessAdapter& initial, const Tutorial* lesson) {
  GameController next(initial);
  GameRecord nextRecord;
  GameMetadata metadata;
  if (lesson) metadata.event = std::string("Tutorial - ") + lesson->title;
  auto result = nextRecord.begin(next.position(), metadata);
  if (result == RecordStatus::Ok && next.state() == GameState::Finished)
    result = nextRecord.finish(next.position(), next.ending());
  if (result != RecordStatus::Ok) { fault_ = result; return false; }
  game_ = std::move(next);
  record_ = std::move(nextRecord);
  fault_ = RecordStatus::Ok;
  haveRaw_ = haveStable_ = rejecting_ = saved_ = false;
  lesson_ = lesson;
  if (lesson_) computerMode_ = false;
  restoreReason_ = ConfirmationStatus::NoMove;
  lastEnPassant_ = false;
  updateSuggestion();
  return true;
}

void BoardApp::setComputerMode(bool enabled) {
  computerMode_ = enabled;
  updateSuggestion();
}

const LessonStep* BoardApp::step() const {
  if (!lesson_ || record_.finished() || record_.plyCount() >= lesson_->count) return nullptr;
  return &lesson_->steps[record_.plyCount()];
}

void BoardApp::updateSuggestion() {
  if (const auto* instruction = step()) {
    suggestion_ = instruction->move;
    return;
  }
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
  if (game_.state() == GameState::Tracking || game_.state() == GameState::AwaitingPromotion)
    restoreReason_ = ConfirmationStatus::UnreliableInput;
  game_.markInputInterrupted();
  haveRaw_ = haveStable_ = false;
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
  allDebounced_ = true;
  const auto previous = filtered_;
  for (unsigned i = 0; i < 64; ++i) {
    const Occupancy bit = Occupancy{1} << i;
    if ((raw ^ raw_) & bit) changed_[i] = now;
    if (now - changed_[i] >= board::REED_DEBOUNCE_MS)
      filtered_ = (filtered_ & ~bit) | (raw & bit);
    else allDebounced_ = false;
  }
  raw_ = raw;
  if (!haveStable_ && !allDebounced_) return;
  haveStable_ = true;
  const bool official = filtered_ == game_.position().occupancy();
  // A partially debounced frame cannot release the restoration gate.
  if (game_.state() == GameState::AwaitingPosition && official && !allDebounced_) return;
  game_.observe(filtered_, now);
  if (official && game_.state() == GameState::Tracking) {
    if (previous != filtered_) game_.clearAttempt();
    restoreReason_ = ConfirmationStatus::NoMove;
  }
}

bool BoardApp::settled(std::uint32_t now) const {
  return haveStable_ && allDebounced_ &&
         now - lastSample_ <= board::MAX_SAMPLE_GAP_MS;
}

void BoardApp::reject(std::uint32_t now) { rejecting_ = true; rejectedAt_ = now; }

ConfirmationStatus BoardApp::confirm(std::uint32_t now) {
  if (fault_ != RecordStatus::Ok) { reject(now); return ConfirmationStatus::ApplyFailed; }
  if (game_.state() == GameState::Finished) return ConfirmationStatus::GameFinished;
  if (!settled(now)) {
    inputLost();
    reject(now);
    return ConfirmationStatus::UnreliableInput;
  }
  // Publish only after both position and PGN accept; preserve repetition history.
  GameController next = game_;
  const auto* expected = suggestion_.from == kInvalidSquare ? nullptr : &suggestion_;
  const auto confirmation = next.confirm(expected, computerMode_);
  const auto result = confirmation.status;
  if (result == ConfirmationStatus::Accepted) {
    if (lesson_ && record_.plyCount() + 1 == lesson_->count && next.state() != GameState::Finished)
      next.finish();
    const auto recorded = record_.record(game_.position(), confirmation, next.position(), next.ending());
    if (recorded != RecordStatus::Ok) {
      fault_ = recorded;
      reject(now);
      return ConfirmationStatus::ApplyFailed;
    }
    const Move& move = confirmation.move;
    const auto piece = game_.position().pieceAt(move.from);
    lastEnPassant_ = (piece == Piece::WhitePawn || piece == Piece::BlackPawn) &&
        move.from % 8 != move.to % 8 && game_.position().pieceAt(move.to) == Piece::Empty;
    rejecting_ = false;
  }
  game_ = std::move(next);
  if (result == ConfirmationStatus::Accepted) updateSuggestion();
  if (result != ConfirmationStatus::Accepted && result != ConfirmationStatus::NoMove &&
      result != ConfirmationStatus::PromotionRequired) {
    if (restoreReason_ == ConfirmationStatus::NoMove) restoreReason_ = result;
    reject(now);
  }
  return result;
}

bool BoardApp::promote(Promotion piece) {
  return fault_ == RecordStatus::Ok && game_.selectPromotion(piece);
}

bool BoardApp::finish(bool resignation, Color player) {
  if (fault_ != RecordStatus::Ok) return false;
  GameController next = game_;
  if (!(resignation ? next.resign(player) : next.finish())) return false;
  const auto result = record_.finish(next.position(), next.ending());
  if (result != RecordStatus::Ok) { fault_ = result; return false; }
  game_ = std::move(next);
  suggestion_ = {};
  restoreReason_ = ConfirmationStatus::NoMove;
  return true;
}

void BoardApp::tick(std::uint32_t now) {
  if (haveRaw_ && now - lastSample_ > board::MAX_SAMPLE_GAP_MS) inputLost();
  if (rejecting_ && now - rejectedAt_ >= 400) rejecting_ = false;
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
