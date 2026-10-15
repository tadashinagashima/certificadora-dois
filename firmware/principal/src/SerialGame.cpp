#include "SerialGame.h"

#include <cstdio>
#include <utility>

namespace principal {
using namespace chessboard;
namespace {

const char* name(GameState value) {
  switch (value) {
    case GameState::AwaitingPosition: return "AwaitingPosition";
    case GameState::Tracking: return "Tracking";
    case GameState::AwaitingPromotion: return "AwaitingPromotion";
    case GameState::Finished: return "Finished";
  }
  return "Unknown";
}
const char* name(RecordStatus value) {
  switch (value) {
    case RecordStatus::Ok: return "Ok";
    case RecordStatus::Ignored: return "Ignored";
    case RecordStatus::NotStarted: return "NotStarted";
    case RecordStatus::AlreadyFinished: return "AlreadyFinished";
    case RecordStatus::InvalidMetadata: return "InvalidMetadata";
    case RecordStatus::InvalidContext: return "InvalidContext";
    case RecordStatus::InvalidMove: return "InvalidMove";
    case RecordStatus::CapacityExceeded: return "CapacityExceeded";
    case RecordStatus::InvalidResult: return "InvalidResult";
  }
  return "Unknown";
}
const char* name(ConfirmationStatus value) {
  switch (value) {
    case ConfirmationStatus::Accepted: return "Accepted";
    case ConfirmationStatus::NoMove: return "NoMove";
    case ConfirmationStatus::NoLegalMatch: return "NoLegalMatch";
    case ConfirmationStatus::Ambiguous: return "Ambiguous";
    case ConfirmationStatus::PromotionRequired: return "PromotionRequired";
    case ConfirmationStatus::UnreliableInput: return "UnreliableInput";
    case ConfirmationStatus::PositionMismatch: return "PositionMismatch";
    case ConfirmationStatus::ApplyFailed: return "ApplyFailed";
    case ConfirmationStatus::GameFinished: return "GameFinished";
  }
  return "Unknown";
}
const char* name(Promotion value) {
  switch (value) {
    case Promotion::None: return "None";
    case Promotion::Queen: return "Q";
    case Promotion::Rook: return "R";
    case Promotion::Bishop: return "B";
    case Promotion::Knight: return "N";
  }
  return "Unknown";
}
const char* name(EndReason value) {
  switch (value) {
    case EndReason::None: return "None";
    case EndReason::Checkmate: return "Checkmate";
    case EndReason::Stalemate: return "Stalemate";
    case EndReason::InsufficientMaterial: return "InsufficientMaterial";
    case EndReason::Resignation: return "Resignation";
    case EndReason::Manual: return "Manual";
  }
  return "Unknown";
}
const char* name(GameResult value) {
  switch (value) {
    case GameResult::Ongoing: return "Ongoing";
    case GameResult::WhiteWin: return "1-0";
    case GameResult::BlackWin: return "0-1";
    case GameResult::Draw: return "1/2-1/2";
    case GameResult::Unfinished: return "*";
  }
  return "Unknown";
}

struct Scenario { const char* id; const char* fen; };
// Trusted fixtures, not a public arbitrary-FEN parser (THC accepts a dialect).
const Scenario scenarios[] = {
  {"start", nullptr},
  {"castle_w", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"},
  {"castle_b", "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"},
  {"ep_w", "k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1"},
  {"ep_b", "4k3/8/8/8/3Pp3/8/8/K7 b - d3 0 1"},
  {"promotion_w", "7k/P7/8/8/8/8/8/4K3 w - - 0 1"},
  {"promotion_b", "4k3/8/8/8/8/8/p7/7K b - - 0 1"},
  {"promotion_capture_w", "1r5k/P7/8/8/8/8/8/4K3 w - - 0 1"},
  {"promotion_capture_b", "4k3/8/8/8/8/8/p7/1R5K b - - 0 1"},
  {"ambiguous", "r6k/8/8/8/8/8/8/Rn2K3 w - - 0 1"},
  {"mate", "7k/6Q1/5K2/8/8/8/8/8 b - - 0 1"},
  {"stalemate", "7k/5K2/6Q1/8/8/8/8/8 b - - 0 1"},
  {"insufficient", "7k/8/8/8/8/8/8/K7 w - - 0 1"},
  {"busy", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"}
};

std::string hex(Occupancy value) {
  char text[17];
  std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
  return text;
}
bool occupancy(const std::string& text, Occupancy& value) {
  if (text.size() != 16) return false;
  value = 0;
  for (char ch : text) {
    unsigned digit;
    if (ch >= '0' && ch <= '9') digit = static_cast<unsigned>(ch - '0');
    else if (ch >= 'a' && ch <= 'f') digit = static_cast<unsigned>(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F') digit = static_cast<unsigned>(ch - 'A' + 10);
    else return false;
    value = (value << 4) | digit;
  }
  return true;
}
}  // namespace

SerialGame::SerialGame() { fault_ = record_.begin(game_.position()); }

std::string SerialGame::fault(RecordStatus error) {
  fault_ = error;
  return std::string("ERROR RecordFailure ") + name(error) + "\n";
}

std::string SerialGame::start(const std::string& scenario) {
  const Scenario* selected = nullptr;
  for (const auto& item : scenarios) if (scenario == item.id) selected = &item;
  if (!selected) return "ERROR Scenario\n";
  ChessAdapter initial;
  if (selected->fen && !initial.loadFen(selected->fen)) return "ERROR Fixture\n";
  GameController next(initial);
  GameRecord nextRecord;
  auto result = nextRecord.begin(next.position());
  if (result == RecordStatus::Ok && next.state() == GameState::Finished)
    result = nextRecord.finish(next.position(), next.ending());
  if (result != RecordStatus::Ok) return fault(result);
  game_ = std::move(next);
  record_ = std::move(nextRecord);
  fault_ = RecordStatus::Ok;
  return "OK new " + scenario + "\n";
}

std::string SerialGame::confirm() {
  // Keep the authoritative position/history until BOTH modules accept.
  // Copies retain repetition history; no round-trip through FEN.
  GameController next = game_;
  const auto confirmation = next.confirm();
  if (confirmation.status == ConfirmationStatus::Accepted) {
    GameRecord nextRecord = record_;
    auto result = nextRecord.record(game_.position(), confirmation, next.position());
    if (result == RecordStatus::Ok && next.state() == GameState::Finished)
      result = nextRecord.finish(next.position(), next.ending());
    if (result != RecordStatus::Ok) return fault(result);
    record_ = std::move(nextRecord);
  }
  game_ = std::move(next);
  return std::string("CONFIRM ") + name(confirmation.status) + "\n";
}

std::string SerialGame::end(bool resignation, Color player) {
  GameController next = game_;
  if (!(resignation ? next.resign(player) : next.finish())) return "ERROR GameFinished\n";
  const auto result = record_.finish(next.position(), next.ending());
  if (result != RecordStatus::Ok) return fault(result);
  game_ = std::move(next);
  return "OK finish\n";
}

void SerialGame::inputLost() { game_.markInputInterrupted(); }

std::string SerialGame::status() const {
  const auto attempt = game_.attempt();
  const auto position = game_.position().status();
  std::string output = std::string("STATE ") + name(game_.state()) + " fault=" + name(fault_) +
      " plies=" + std::to_string(record_.plyCount()) + " promotion=" + name(game_.selectedPromotion()) +
      " record_finished=" + std::to_string(record_.finished()) +
      " frames=" + std::to_string(attempt.frameCount) + " reliable=" + std::to_string(attempt.reliable()) +
      " overflow=" + std::to_string(attempt.historyOverflow) + " interrupted=" + std::to_string(attempt.inputInterrupted) + "\n";
  output += "FEN " + game_.position().fen() + "\n";
  output += "OCC official=" + hex(game_.position().occupancy()) + " observed=" + hex(attempt.currentOccupancy) +
      " available=" + std::to_string(attempt.readingAvailable) + "\n";
  output += std::string("ENDING ") + name(game_.ending().reason) + " result=" + name(game_.ending().result) + "\n";
  output += "CLAIMS repetition=" + std::to_string(position.threefoldRepetitionClaim) +
      " fifty=" + std::to_string(position.fiftyMoveClaim) + "\n";
  return output;
}

std::string SerialGame::command(const std::string& line, std::uint32_t now) {
  // Exactly one optional argument; no partial parsing or silent suffixes.
  const auto separator = line.find(' ');
  const std::string verb = line.substr(0, separator);
  const std::string arg = separator == std::string::npos ? "" : line.substr(separator + 1);
  const bool bare = separator == std::string::npos;
  if (line.empty() || line.size() > LineReceiver::capacity || (!bare && (arg.empty() || arg.find(' ') != std::string::npos))) {
    inputLost();
    return "ERROR Syntax\n";
  }
  if (verb == "help" && bare) {
    std::string text = "COMMANDS new [scenario], sync, observe HEX16, lift SQ, place SQ, confirm, promote Q|R|B|N, interrupt, finish, resign white|black, status, pgn, metrics, help\nSCENARIOS";
    for (const auto& item : scenarios) text += std::string(" ") + item.id;
    return text + "\n";
  }
  if (verb == "status" && bare) return status();
  if (verb == "pgn" && bare) return "PGN_BEGIN\n" + record_.pgn() + "PGN_END\n";
  if (verb == "new") return start(bare ? "start" : arg);
  if (fault_ != RecordStatus::Ok) return std::string("ERROR FaultLatched ") + name(fault_) + "\n";
  if (verb == "confirm" && bare) return confirm();
  if (verb == "finish" && bare) return end(false, Color::White);
  if (verb == "resign" && (arg == "white" || arg == "black")) return end(true, arg == "white" ? Color::White : Color::Black);
  if (verb == "interrupt" && bare) { inputLost(); return "OK interrupt\n"; }
  if (verb == "promote" && !bare) {
    Promotion selection = Promotion::None;
    if (arg == "Q") selection = Promotion::Queen;
    if (arg == "R") selection = Promotion::Rook;
    if (arg == "B") selection = Promotion::Bishop;
    if (arg == "N") selection = Promotion::Knight;
    return game_.selectPromotion(selection) ? "OK promote\n" : "ERROR Promotion\n";
  }
  if (verb == "sync" || verb == "observe" || verb == "lift" || verb == "place") {
    Occupancy value = 0;
    if (verb == "sync" && bare) value = game_.position().occupancy();
    else if (verb == "observe" && occupancy(arg, value)) { /* complete frame */ }
    else if ((verb == "lift" || verb == "place") && arg.size() == 2 && arg[0] >= 'a' && arg[0] <= 'h' && arg[1] >= '1' && arg[1] <= '8') {
      const auto attempt = game_.attempt();
      if (!attempt.readingAvailable) return "ERROR NoObservation\n";
      const Occupancy bit = Occupancy{1} << ((arg[1] - '1') * 8 + arg[0] - 'a');
      value = verb == "lift" ? attempt.currentOccupancy & ~bit : attempt.currentOccupancy | bit;
    } else { inputLost(); return "ERROR Observation\n"; }
    if (game_.state() == GameState::Finished) return "ERROR GameFinished\n";
    game_.observe(value, now);
    return "OK observe\n";
  }
  inputLost();
  return "ERROR Command\n";
}

LineReceiver::Result LineReceiver::push(char ch) {
  if (ch == '\n') {
    data_[size_] = '\0';
    const auto result = invalid_ ? Result::Invalid : Result::Line;
    size_ = 0;
    invalid_ = false;
    cr_ = false;
    return result;
  }
  if (invalid_) return Result::Pending;
  if (ch == '\r' && !cr_) { cr_ = true; return Result::Pending; }
  if (cr_ || ch < 32 || ch > 126 || size_ == capacity) { invalid_ = true; return Result::Pending; }
  data_[size_++] = ch;
  return Result::Pending;
}

}  // namespace principal
