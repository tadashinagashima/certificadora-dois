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
  void tick(std::uint32_t now, std::uint8_t buttons);
  chessboard::ConfirmationStatus confirm(std::uint32_t now);
  bool promote(chessboard::Promotion piece);
  bool finish(bool resignation = false, chessboard::Color player = chessboard::Color::White);
  bool requestSave();
  bool takeSaveRequest();
  void saved(const std::string& path); // Empty path means failure; keep the PGN in RAM.
  Indicators indicators(std::uint32_t now) const;
  bool settled(std::uint32_t now) const;

  const chessboard::GameController& game() const { return game_; }
  const chessboard::GameRecord& record() const { return record_; }
  chessboard::RecordStatus fault() const { return fault_; }
  bool storageError() const { return storageError_; }
  const std::string& savedPath() const { return savedPath_; }

 private:
  void released(unsigned button, std::uint32_t held, std::uint32_t now);
  void reject(std::uint32_t now);
  chessboard::GameController game_;
  chessboard::GameRecord record_;
  chessboard::RecordStatus fault_ = chessboard::RecordStatus::Ok;
  chessboard::Occupancy raw_ = 0, filtered_ = 0;
  std::array<std::uint32_t, 64> changed_{};
  bool haveRaw_ = false, haveStable_ = false;
  std::uint32_t lastSample_ = 0;
  struct Button { bool raw = false, stable = false; std::uint32_t changed = 0, pressed = 0; };
  std::array<Button, 3> buttons_{};
  bool buttonChord_ = false;
  bool saveRequested_ = false, storageError_ = false;
  std::string savedPath_;
  bool rejecting_ = false;
  std::uint32_t rejectedAt_ = 0;
};

} // namespace principal
