#pragma once

#include <GameRecord.h>
#include <array>
#include "../../board_config.h"

namespace principal {

using board::Indicators;

// Shared by ESP32 and the simulator. One owner, no calls from interrupts.
class BoardApp {
 public:
  BoardApp();
  bool start(const chessboard::ChessAdapter& initial = chessboard::ChessAdapter{});
  void sample(chessboard::Occupancy raw, std::uint32_t now);
  void inputLost();
  void tick(std::uint32_t now);
  chessboard::ConfirmationStatus confirm(std::uint32_t now, bool allowPartial = false);
  bool promote(chessboard::Promotion piece);
  bool finish(bool resignation = false, chessboard::Color player = chessboard::Color::White);
  void setComputerMode(bool enabled);
  bool computerMode() const { return computerMode_; }
  bool setPlayerName(chessboard::Color side, const std::string& name) {
    return record_.setPlayerName(side, name);
  }
  const chessboard::Move& suggestedMove() const { return suggestion_; }
  std::string suggestedCoordinates() const;
  bool saved() const { return saved_; }
  void acknowledgeSave() { saved_ = record_.finished(); }
  bool rejecting() const { return rejecting_; }
  chessboard::ConfirmationStatus adjustment() const { return adjustment_; }
  bool readingAvailable() const { return haveStable_; }
  chessboard::Occupancy sensorOccupancy() const { return filtered_; }
  Indicators indicators(std::uint32_t now) const;
  bool settled(std::uint32_t now) const;

  const chessboard::GameController& game() const { return game_; }
  const chessboard::GameRecord& record() const { return record_; }
  chessboard::RecordStatus fault() const { return fault_; }

 private:
  void updateSuggestion();
  bool computerMode_ = false;
  chessboard::Move suggestion_{};
  void reject(std::uint32_t now);
  chessboard::GameController game_;
  chessboard::GameRecord record_;
  chessboard::RecordStatus fault_ = chessboard::RecordStatus::Ok;
  chessboard::Occupancy raw_ = 0, filtered_ = 0;
  std::array<std::uint32_t, 64> changed_{};
  bool haveRaw_ = false, haveStable_ = false;
  std::uint32_t lastSample_ = 0;
  std::uint32_t lastChange_ = 0;
  bool landed_ = false, evaluateMove_ = false;
  chessboard::ConfirmationStatus adjustment_ = chessboard::ConfirmationStatus::NoMove;
  bool saved_ = false;
  bool rejecting_ = false;
  std::uint32_t rejectedAt_ = 0;
};

} // namespace principal
