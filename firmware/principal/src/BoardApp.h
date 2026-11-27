#pragma once

#include <GameRecord.h>
#include <array>
#include <string>
#include "../../board_config.h"

namespace principal {

struct Indicators { bool green = false, red = false, buzzer = false; };

// Shared by ESP32 and the simulator. One owner, no calls from interrupts.
class BoardApp {
 public:
  BoardApp();
  bool start(const chessboard::ChessAdapter& initial = chessboard::ChessAdapter{});
  void sample(chessboard::Occupancy raw, std::uint32_t now);
  void inputLost();
  void tick(std::uint32_t now);
  chessboard::ConfirmationStatus confirm(std::uint32_t now);
  bool promote(chessboard::Promotion piece);
  bool finish(bool resignation = false, chessboard::Color player = chessboard::Color::White);
  bool saved() const { return saved_; }
  void acknowledgeSave() { saved_ = record_.finished(); }
  bool rejecting() const { return rejecting_; }
  bool readingAvailable() const { return haveStable_; }
  bool inCheck() const { return inCheck_; }
  Indicators indicators(std::uint32_t now) const;
  bool settled(std::uint32_t now) const;

  const chessboard::GameController& game() const { return game_; }
  const chessboard::GameRecord& record() const { return record_; }
  chessboard::RecordStatus fault() const { return fault_; }

 private:
  void reject(std::uint32_t now);
  chessboard::GameController game_;
  chessboard::GameRecord record_;
  chessboard::RecordStatus fault_ = chessboard::RecordStatus::Ok;
  chessboard::Occupancy raw_ = 0, filtered_ = 0;
  std::array<std::uint32_t, 64> changed_{};
  bool haveRaw_ = false, haveStable_ = false;
  std::uint32_t lastSample_ = 0;
  bool saved_ = false, inCheck_ = false;
  bool rejecting_ = false;
  std::uint32_t rejectedAt_ = 0;
};

} // namespace principal
