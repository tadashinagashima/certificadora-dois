#include "GameRecord.h"

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
  metadata_ = normalized;
  initialFen_ = fen;
  expectedFen_ = fen;
  movetext_.clear();
  lastSan_.clear();
  plyCount_ = 0;
  ending_ = GameEnd{};
  customStart_ = fen != ChessAdapter{}.fen();
  return RecordStatus::Ok;
}

RecordStatus GameRecord::record(const ChessAdapter& before,
                               const ConfirmationResult& confirmation,
                               const ChessAdapter& after, GameEnd ending) {
  if (confirmation.status != ConfirmationStatus::Accepted) return RecordStatus::Ignored;
  if (!started()) return RecordStatus::NotStarted;
  if (finished()) return RecordStatus::AlreadyFinished;
  if (before.fen() != expectedFen_) return RecordStatus::InvalidContext;

  std::string san;
  if (!before.san(confirmation.move, san)) return RecordStatus::InvalidMove;
  ChessAdapter expected = before;
  if (!expected.apply(confirmation.move)) return RecordStatus::InvalidMove;
  const std::string afterFen = after.fen();
  if (expected.fen() != afterFen) return RecordStatus::InvalidContext;

  if (ending.reason == EndReason::None) {
    if (ending.result != GameResult::Ongoing) return RecordStatus::InvalidResult;
  } else if (!validEnd(after, ending)) {
    return RecordStatus::InvalidResult;
  }

  std::string addition;
  if (plyCount_ != 0) addition += ' ';
  if (before.sideToMove() == Color::White) addition += std::to_string(before.fullmoveNumber()) + ". ";
  else if (plyCount_ == 0) addition += std::to_string(before.fullmoveNumber()) + "... ";
  addition += san;
  if (addition.size() > kMovetextCapacity - movetext_.size()) return RecordStatus::CapacityExceeded;

  movetext_ += addition;
  lastSan_ = san;
  expectedFen_ = afterFen;
  ending_ = ending;
  ++plyCount_;
  return RecordStatus::Ok;
}

RecordStatus GameRecord::finish(const ChessAdapter& official, GameEnd ending) {
  if (!started()) return RecordStatus::NotStarted;
  if (finished()) {
    return ending.reason == ending_.reason && ending.result == ending_.result &&
                   official.fen() == expectedFen_ ? RecordStatus::Ok : RecordStatus::AlreadyFinished;
  }
  if (official.fen() != expectedFen_) return RecordStatus::InvalidContext;
  if (!validEnd(official, ending)) return RecordStatus::InvalidResult;
  ending_ = ending;
  return RecordStatus::Ok;
}

bool GameRecord::started() const { return !initialFen_.empty(); }
bool GameRecord::finished() const { return ending_.reason != EndReason::None; }
std::size_t GameRecord::plyCount() const { return plyCount_; }
const std::string& GameRecord::movetext() const { return movetext_; }

bool GameRecord::setPlayerName(Color side, const std::string& name) {
  if (!started() || finished() || !validValue(name, 5)) return false;
  (side == Color::White ? metadata_.white : metadata_.black) = name.empty() ? "?" : name;
  return true;
}

std::string GameRecord::pgn() const {
  if (!started()) return {};
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
