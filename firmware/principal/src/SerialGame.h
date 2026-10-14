#pragma once

#include <GameRecord.h>
#include <array>
#include <string>

namespace principal {

// One owner, called serially from loop(). No hardware or chess rules here.
class SerialGame {
 public:
  SerialGame();
  std::string command(const std::string& line, std::uint32_t now);
  void inputLost();

 private:
  std::string start(const std::string& scenario);
  std::string confirm();
  std::string end(bool resignation, chessboard::Color player);
  std::string status() const;
  std::string fault(chessboard::RecordStatus error);
  chessboard::GameController game_;
  chessboard::GameRecord record_;
  chessboard::RecordStatus fault_ = chessboard::RecordStatus::Ok;
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
