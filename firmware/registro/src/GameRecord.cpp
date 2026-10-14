#include "GameRecord.h"

#include <limits>

namespace chessboard {
namespace {

bool validValue(const std::string& value, std::size_t tagLength) {
  if (value.size() > kMetadataValueCapacity) return false;
  std::size_t escapedSize = value.size();
  for (unsigned char ch : value) {
    if (ch < 32 || ch == 127) return false;
    if (ch == '"' || ch == '\\') ++escapedSize;
  }
  // PGN also limits header lines to fewer than 256 printing characters.
  return tagLength + escapedSize + 5 < 256;
}

bool validDatePart(const std::string& date, std::size_t offset, std::size_t length,
                   unsigned maximum) {
  const std::string part = date.substr(offset, length);
  if (part == std::string(length, '?')) return true;
  unsigned value = 0;
  for (char ch : part) {
    if (ch < '0' || ch > '9') return false;
    value = value * 10 + static_cast<unsigned>(ch - '0');
  }
  return value >= 1 && value <= maximum;
}

bool validMetadata(const GameMetadata& metadata) {
  const auto& date = metadata.date;
  return validValue(metadata.event, 5) && validValue(metadata.site, 4) &&
         validValue(metadata.round, 5) && validValue(metadata.white, 5) &&
         validValue(metadata.black, 5) && date.size() == 10 &&
         date[4] == '.' && date[7] == '.' &&
         validDatePart(date, 0, 4, 9999) && validDatePart(date, 5, 2, 12) &&
         validDatePart(date, 8, 2, 31);
}

void normalize(std::string& value) {
  if (value.empty()) value = "?";
}

bool fullmoveNumber(const std::string& fen, std::uint32_t& result) {
  const auto separator = fen.find_last_of(' ');
  if (separator == std::string::npos || separator + 1 == fen.size()) return false;
  result = 0;
  for (std::size_t i = separator + 1; i < fen.size(); ++i) {
    if (fen[i] < '0' || fen[i] > '9') return false;
    const auto digit = static_cast<std::uint32_t>(fen[i] - '0');
    if (result > (std::numeric_limits<std::uint32_t>::max() - digit) / 10) return false;
    result = result * 10 + digit;
  }
  return result != 0;
}

const char* resultMarker(GameResult result) {
  switch (result) {
    case GameResult::WhiteWin: return "1-0";
    case GameResult::BlackWin: return "0-1";
    case GameResult::Draw: return "1/2-1/2";
    default: return "*";
  }
}

bool validEnd(const ChessAdapter& official, GameEnd ending) {
  switch (ending.reason) {
    case EndReason::Manual:
      return ending.result == GameResult::Unfinished;
    case EndReason::Resignation:
      return ending.result == GameResult::WhiteWin || ending.result == GameResult::BlackWin;
    case EndReason::Checkmate: {
      const auto status = official.status();
      const auto winner = status.sideToMove == Color::White ? GameResult::BlackWin : GameResult::WhiteWin;
      return status.terminal == TerminalState::Checkmate && ending.result == winner;
    }
    case EndReason::Stalemate:
      return ending.result == GameResult::Draw && official.status().terminal == TerminalState::Stalemate;
    case EndReason::InsufficientMaterial:
      return ending.result == GameResult::Draw && official.status().insufficientMaterial;
    default:
      return false;
  }
}

void appendTag(std::string& output, const char* name, const std::string& value) {
  output += '[';
  output += name;
  output += " \"";
  for (char ch : value) {
    if (ch == '"' || ch == '\\') output += '\\';
    output += ch;
  }
  output += "\"]\n";
}

void appendToken(std::string& output, const std::string& token, std::size_t& lineLength) {
  if (lineLength != 0) {
    if (lineLength + 1 + token.size() > 79) {
      output += '\n';
      lineLength = 0;
    } else {
      output += ' ';
      ++lineLength;
    }
  }
  output += token;
  lineLength += token.size();
}

}  // namespace

RecordStatus GameRecord::begin(const ChessAdapter& initial, const GameMetadata& metadata) {
  GameMetadata normalized = metadata;
  normalize(normalized.event);
  normalize(normalized.site);
  normalize(normalized.round);
  normalize(normalized.white);
  normalize(normalized.black);
  if (normalized.date.empty()) normalized.date = "????.??.??";
  if (!validMetadata(normalized)) return RecordStatus::InvalidMetadata;

  const std::string fen = initial.fen();
  std::uint32_t number = 0;
  if (!fullmoveNumber(fen, number)) return RecordStatus::InvalidContext;
  metadata_ = normalized;
  initialFen_ = fen;
  expectedFen_ = fen;
  movetext_.clear();
  fullmove_ = number;
  plyCount_ = 0;
  nextSide_ = initial.sideToMove();
  ending_ = GameEnd{};
  started_ = true;
  finished_ = false;
  customStart_ = fen != ChessAdapter{}.fen();
  return RecordStatus::Ok;
}

RecordStatus GameRecord::record(const ChessAdapter& before,
                              const ConfirmationResult& confirmation, const ChessAdapter& after) {
  if (confirmation.status != ConfirmationStatus::Accepted) return RecordStatus::Ignored;
  if (!started_) return RecordStatus::NotStarted;
  if (finished_) return RecordStatus::AlreadyFinished;
  if (before.fen() != expectedFen_) return RecordStatus::InvalidContext;

  std::string san;
  if (!before.san(confirmation.move, san)) return RecordStatus::InvalidMove;
  ChessAdapter expected = before;
  if (!expected.apply(confirmation.move)) return RecordStatus::InvalidMove;
  const std::string afterFen = after.fen();
  if (expected.fen() != afterFen) return RecordStatus::InvalidContext;

  std::string addition;
  if (plyCount_ != 0) addition += ' ';
  if (nextSide_ == Color::White) addition += std::to_string(fullmove_) + ". ";
  else if (plyCount_ == 0) addition += std::to_string(fullmove_) + "... ";
  addition += san;
  if (addition.size() > kMovetextCapacity - movetext_.size()) return RecordStatus::CapacityExceeded;

  movetext_ += addition;
  expectedFen_ = afterFen;
  if (nextSide_ == Color::Black) ++fullmove_;
  nextSide_ = after.sideToMove();
  ++plyCount_;
  return RecordStatus::Ok;
}

RecordStatus GameRecord::finish(const ChessAdapter& official, GameEnd ending) {
  if (!started_) return RecordStatus::NotStarted;
  if (finished_) {
    return ending.reason == ending_.reason && ending.result == ending_.result &&
                   official.fen() == expectedFen_ ? RecordStatus::Ok : RecordStatus::AlreadyFinished;
  }
  if (official.fen() != expectedFen_) return RecordStatus::InvalidContext;
  if (!validEnd(official, ending)) return RecordStatus::InvalidResult;
  ending_ = ending;
  finished_ = true;
  return RecordStatus::Ok;
}

bool GameRecord::started() const { return started_; }
bool GameRecord::finished() const { return finished_; }
std::size_t GameRecord::plyCount() const { return plyCount_; }
const std::string& GameRecord::movetext() const { return movetext_; }

std::string GameRecord::pgn() const {
  if (!started_) return {};
  std::string output;
  appendTag(output, "Event", metadata_.event);
  appendTag(output, "Site", metadata_.site);
  appendTag(output, "Date", metadata_.date);
  appendTag(output, "Round", metadata_.round);
  appendTag(output, "White", metadata_.white);
  appendTag(output, "Black", metadata_.black);
  appendTag(output, "Result", resultMarker(ending_.result));
  if (customStart_) {
    appendTag(output, "FEN", initialFen_);
    appendTag(output, "SetUp", "1");
  }
  output += '\n';
  std::size_t lineLength = 0;
  std::size_t start = 0;
  while (start < movetext_.size()) {
    auto end = movetext_.find(' ', start);
    if (end == std::string::npos) end = movetext_.size();
    appendToken(output, movetext_.substr(start, end - start), lineLength);
    start = end + 1;
  }
  appendToken(output, resultMarker(ending_.result), lineLength);
  output += "\n\n";
  return output;
}

}  // namespace chessboard
