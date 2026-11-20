#pragma once

#include "BoardApp.h"
#include <array>
#include <string>

namespace principal {

// Console adapter; simulated inputs are explicitly disabled on the physical board.
class SerialGame {
 public:
  explicit SerialGame(bool simulation = true) : simulation_(simulation) {}
  std::string command(const std::string& line, std::uint32_t now);
  void inputLost();
  BoardApp& app() { return app_; }

 private:
  std::string start(const std::string& scenario);
  std::string confirm();
  std::string end(bool resignation, chessboard::Color player);
  std::string status() const;
  std::string fault(chessboard::RecordStatus error);
  void advance(std::uint32_t duration);
  BoardApp app_;
  bool simulation_;
  std::uint32_t now_ = 0, simNow_ = 0, simSampleAt_ = 0;
  chessboard::Occupancy simRaw_ = 0;
  bool simAvailable_ = false;
  std::uint8_t simButtons_ = 0;
};

// A bad/oversize line is discarded through LF; never execute a valid suffix.
class LineReceiver {
 public:
  static constexpr std::size_t capacity = 96;
  enum class Result { Pending, Line, Invalid };
  Result push(char ch);
  const char* line() const { return data_.data(); }
  bool pending() const { return size_ != 0 || invalid_ || cr_; }
  void discard() { size_ = 0; invalid_ = true; }

 private:
  std::array<char, capacity + 1> data_{};
  std::size_t size_ = 0;
  bool invalid_ = false;
  bool cr_ = false;
};

}  // namespace principal
