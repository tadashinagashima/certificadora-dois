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
  haveRaw_ = haveStable_ = rejecting_ = storageError_ = false;
  savedPath_.clear();
  saveRequested_ = record_.finished();
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
    GameRecord nextRecord = record_;
    auto result = nextRecord.record(game_.position(), confirmation, next.position());
    if (result == RecordStatus::Ok && next.state() == GameState::Finished)
      result = nextRecord.finish(next.position(), next.ending());
    if (result != RecordStatus::Ok) {
      fault_ = result;
      reject(now);
      return ConfirmationStatus::ApplyFailed;
    }
    record_ = std::move(nextRecord);
    saveRequested_ = record_.finished();
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
  saveRequested_ = true;
  return true;
}

bool BoardApp::requestSave() {
  if (!record_.finished()) return false;
  if (savedPath_.empty()) saveRequested_ = true;
  return true;
}
bool BoardApp::takeSaveRequest() {
  const bool requested = saveRequested_;
  saveRequested_ = false;
  return requested;
}
void BoardApp::saved(const std::string& path) {
  savedPath_ = path;
  storageError_ = path.empty();
}

void BoardApp::released(unsigned button, std::uint32_t held, std::uint32_t now) {
  if (game_.state() == GameState::Finished) {
    if (button == 2) requestSave();
    if (button == 0 && held >= board::LONG_PRESS_MS && !savedPath_.empty()) start();
    return;
  }
  if (button == 0) confirm(now);
  if (button == 1 && game_.state() == GameState::AwaitingPromotion) {
    Promotion next = Promotion::Queen;
    switch (game_.selectedPromotion()) {
      case Promotion::Queen: next = Promotion::Rook; break;
      case Promotion::Rook: next = Promotion::Bishop; break;
      case Promotion::Bishop: next = Promotion::Knight; break;
      default: break;
    }
    promote(next);
  }
  if (button == 2) finish(held >= board::LONG_PRESS_MS, game_.position().sideToMove());
}

void BoardApp::tick(std::uint32_t now, std::uint8_t buttons) {
  if (haveRaw_ && now - lastSample_ > board::MAX_SAMPLE_GAP_MS) inputLost();
  if (rejecting_ && now - rejectedAt_ >= 400) rejecting_ = false;
  // Simultaneous buttons cancel the gesture until every button is released.
  if (buttons && (buttons & (buttons - 1))) buttonChord_ = true;
  for (unsigned i = 0; i < buttons_.size(); ++i) {
    auto& b = buttons_[i];
    const bool pressed = (buttons & (1U << i)) != 0;
    if (pressed != b.raw) { b.raw = pressed; b.changed = now; }
    if (b.stable != b.raw && now - b.changed >= board::BUTTON_DEBOUNCE_MS) {
      b.stable = b.raw;
      if (b.stable) b.pressed = now;
      else if (!buttonChord_) released(i, now - b.pressed, now);
    }
  }
  if (!buttons && !buttons_[0].stable && !buttons_[1].stable && !buttons_[2].stable)
    buttonChord_ = false;
}

Indicators BoardApp::indicators(std::uint32_t now) const {
  Indicators out;
  if (rejecting_ && now - rejectedAt_ < 400) {
    out.red = true;
    out.buzzer = now - rejectedAt_ < 120;
  } else if (fault_ != RecordStatus::Ok) out.red = true;
  else if (game_.state() == GameState::Finished) {
    out.green = !savedPath_.empty() && (now % 1000 < 500);
    out.red = savedPath_.empty() && (now % 500 < 250);
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
