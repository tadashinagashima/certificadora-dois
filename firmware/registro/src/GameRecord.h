#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "GameController.h"

namespace chessboard {

constexpr std::size_t kMovetextCapacity = 8192;
constexpr std::size_t kMetadataValueCapacity = 128;

enum class RecordStatus {
  Ok, Ignored, NotStarted, AlreadyFinished, InvalidMetadata,
  InvalidContext, InvalidMove, CapacityExceeded, InvalidResult
};

struct GameMetadata {
  std::string event = "?";
  std::string site = "?";
  std::string date = "????.??.??";
  std::string round = "?";
  std::string white = "?";
  std::string black = "?";
};

class GameRecord {
 public:
  RecordStatus begin(const ChessAdapter& initial,
                     const GameMetadata& metadata = GameMetadata{});
  RecordStatus record(const ChessAdapter& before, const ConfirmationResult& confirmation,
                      const ChessAdapter& after);
  RecordStatus finish(const ChessAdapter& official, GameEnd ending);

  bool started() const;
  bool finished() const;
  std::size_t plyCount() const;
  const std::string& movetext() const;
  std::string pgn() const;

 private:
  GameMetadata metadata_{};
  std::string initialFen_;
  std::string expectedFen_;
  std::string movetext_;
  std::uint32_t fullmove_ = 1;
  std::size_t plyCount_ = 0;
  Color nextSide_ = Color::White;
  GameEnd ending_{};
  bool started_ = false;
  bool finished_ = false;
  bool customStart_ = false;
};

}  // namespace chessboard
