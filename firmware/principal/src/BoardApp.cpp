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
  return true;
}

void BoardApp::inputLost() {
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
  bool initialStable = true;
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
}

bool BoardApp::settled(std::uint32_t now) const {
  return haveStable_ && raw_ == filtered_ &&
         now - lastSample_ <= board::MAX_SAMPLE_GAP_MS;
}

void BoardApp::reject(std::uint32_t now) { rejecting_ = true; rejectedAt_ = now; }

ConfirmationStatus BoardApp::confirm(std::uint32_t now) {
  if (fault_ != RecordStatus::Ok) { reject(now); return ConfirmationStatus::ApplyFailed; }
  if (game_.state() == GameState::Finished) return ConfirmationStatus::GameFinished;
  if (!settled(now)) { reject(now); return ConfirmationStatus::UnreliableInput; }
  // Publish only after both position and PGN accept; preserve repetition history.
  GameController next = game_;
  const auto confirmation = next.confirm();
  if (confirmation.status == ConfirmationStatus::Accepted) {
    const auto result = record_.record(game_.position(), confirmation, next.position(), next.ending());
    if (result != RecordStatus::Ok) {
      fault_ = result;
      reject(now);
      return ConfirmationStatus::ApplyFailed;
    }
    rejecting_ = false;
  }
  game_ = std::move(next);
  if (confirmation.status != ConfirmationStatus::Accepted &&
      confirmation.status != ConfirmationStatus::NoMove &&
      confirmation.status != ConfirmationStatus::PromotionRequired) reject(now);
  return confirmation.status;
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
  return true;
}

void BoardApp::tick(std::uint32_t now) {
  if (haveRaw_ && now - lastSample_ > board::MAX_SAMPLE_GAP_MS) inputLost();
  if (rejecting_ && now - rejectedAt_ >= 1800) rejecting_ = false;
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
