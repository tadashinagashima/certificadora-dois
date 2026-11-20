#include "SerialGame.h"

#include <cstdio>

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

std::string SerialGame::fault(RecordStatus error) {
  return std::string("ERROR RecordFailure ") + name(error) + "\n";
}

std::string SerialGame::start(const std::string& scenario) {
  if (!simulation_ && scenario != "start") return "ERROR SimulationOnly\n";
  const Scenario* selected = nullptr;
  for (const auto& item : scenarios) if (scenario == item.id) selected = &item;
  if (!selected) return "ERROR Scenario\n";
  ChessAdapter initial;
  if (selected->fen && !initial.loadFen(selected->fen)) return "ERROR Fixture\n";
  if (!app_.start(initial)) return fault(app_.fault());
  simAvailable_ = false;
  simButtons_ = 0;
  return "OK new " + scenario + "\n";
}

std::string SerialGame::confirm() {
  const auto result = app_.confirm(now_);
  if (app_.fault() != RecordStatus::Ok) return fault(app_.fault());
  return std::string("CONFIRM ") + name(result) + "\n";
}

std::string SerialGame::end(bool resignation, Color player) {
  if (!app_.finish(resignation, player)) {
    if (app_.fault() != RecordStatus::Ok) return fault(app_.fault());
    return "ERROR GameFinished\n";
  }
  return "OK finish\n";
}

void SerialGame::inputLost() {
  // UART errors do not imply loss of independent physical sensor readings.
  if (!simulation_) return;
  app_.inputLost();
  simAvailable_ = false;
}

void SerialGame::advance(std::uint32_t duration) {
  while (duration) {
    const auto step = duration > 5 ? 5U : duration;
    simNow_ += step;
    duration -= step;
    if (simAvailable_ && simNow_ - simSampleAt_ >= board::SCAN_INTERVAL_MS) {
      simSampleAt_ = simNow_;
      app_.sample(simRaw_, simNow_);
    }
    app_.tick(simNow_, simButtons_);
  }
  now_ = simNow_;
}

std::string SerialGame::status() const {
  const auto attempt = app_.game().attempt();
  const auto position = app_.game().position().status();
  std::string output = std::string("STATE ") + name(app_.game().state()) + " fault=" + name(app_.fault()) +
      " plies=" + std::to_string(app_.record().plyCount()) + " promotion=" + name(app_.game().selectedPromotion()) +
      " record_finished=" + std::to_string(app_.record().finished()) +
      " frames=" + std::to_string(attempt.frameCount) + " reliable=" + std::to_string(attempt.reliable()) +
      " overflow=" + std::to_string(attempt.historyOverflow) + " interrupted=" + std::to_string(attempt.inputInterrupted) + "\n";
  output += "FEN " + app_.game().position().fen() + "\n";
  output += "OCC official=" + hex(app_.game().position().occupancy()) + " observed=" + hex(attempt.currentOccupancy) +
      " available=" + std::to_string(attempt.readingAvailable) + "\n";
  output += std::string("ENDING ") + name(app_.game().ending().reason) + " result=" + name(app_.game().ending().result) + "\n";
  output += "CLAIMS repetition=" + std::to_string(position.threefoldRepetitionClaim) +
      " fifty=" + std::to_string(position.fiftyMoveClaim) + "\n";
  const auto lights = app_.indicators(now_);
  output += "IO settled=" + std::to_string(app_.settled(now_)) + " green=" + std::to_string(lights.green) +
      " red=" + std::to_string(lights.red) + " buzzer=" + std::to_string(lights.buzzer) +
      " sd_error=" + std::to_string(app_.storageError()) + " saved=" +
      (app_.savedPath().empty() ? "-" : app_.savedPath()) + "\n";
  return output;
}

std::string SerialGame::command(const std::string& line, std::uint32_t now) {
  now_ = simulation_ ? simNow_ : now;
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
    if (!simulation_)
      return "COMMANDS new, confirm, promote Q|R|B|N, finish, resign white|black, save, status, pgn, metrics, help\n";
    std::string text = "COMMANDS new [scenario], sync, observe HEX16, lift SQ, place SQ, confirm, promote Q|R|B|N, interrupt, finish, resign white|black, status, pgn, help\nSCENARIOS";
    for (const auto& item : scenarios) text += std::string(" ") + item.id;
    return text + "\nIO save, raw HEX16, wait MS, buttons MASK(0..7), press confirm|promote|end, hold confirm|end\n";
  }
  if (verb == "status" && bare) return status();
  if (verb == "pgn" && bare) return "PGN_BEGIN\n" + app_.record().pgn() + "PGN_END\n";
  if (verb == "new") return start(bare ? "start" : arg);
  if (verb == "save" && bare) return app_.requestSave() ? "OK save\n" : "ERROR NotFinished\n";
  if (verb == "raw" || verb == "wait" || verb == "buttons" || verb == "press" || verb == "hold") {
    if (!simulation_) return "ERROR SimulationOnly\n";
    if (verb == "raw") {
      Occupancy value;
      if (!occupancy(arg, value)) return "ERROR Observation\n";
      simRaw_ = value;
      simAvailable_ = true;
    } else if (verb == "wait" || verb == "buttons") {
      unsigned value = 0;
      if (arg.empty() || arg.size() > 5) return "ERROR Number\n";
      for (char ch : arg) {
        if (ch < '0' || ch > '9') return "ERROR Number\n";
        value = value * 10 + static_cast<unsigned>(ch - '0');
      }
      if (verb == "buttons") {
        if (value > 7) return "ERROR Number\n";
        simButtons_ = static_cast<std::uint8_t>(value);
      } else {
        if (value > 60000) return "ERROR Number\n";
        advance(value);
      }
    } else {
      if (arg != "confirm" && arg != "promote" && arg != "end") return "ERROR Button\n";
      simButtons_ = arg == "confirm" ? 1 : arg == "promote" ? 2 : 4;
      advance(verb == "hold" ? 2100 : 75);
      simButtons_ = 0;
      advance(75);
    }
    return "OK " + verb + "\n";
  }
  if (app_.fault() != RecordStatus::Ok) return std::string("ERROR FaultLatched ") + name(app_.fault()) + "\n";
  if (verb == "confirm" && bare) return confirm();
  if (verb == "finish" && bare) return end(false, Color::White);
  if (verb == "resign" && (arg == "white" || arg == "black")) return end(true, arg == "white" ? Color::White : Color::Black);
  if (verb == "interrupt" && bare) {
    if (!simulation_) return "ERROR SimulationOnly\n";
    inputLost();
    return "OK interrupt\n";
  }
  if (verb == "promote" && !bare) {
    Promotion selection = Promotion::None;
    if (arg == "Q") selection = Promotion::Queen;
    if (arg == "R") selection = Promotion::Rook;
    if (arg == "B") selection = Promotion::Bishop;
    if (arg == "N") selection = Promotion::Knight;
    return app_.promote(selection) ? "OK promote\n" : "ERROR Promotion\n";
  }
  if (verb == "sync" || verb == "observe" || verb == "lift" || verb == "place") {
    if (!simulation_) return "ERROR SimulationOnly\n";
    Occupancy value = 0;
    if (verb == "sync" && bare) value = app_.game().position().occupancy();
    else if (verb == "observe" && occupancy(arg, value)) { /* complete frame */ }
    else if ((verb == "lift" || verb == "place") && arg.size() == 2 && arg[0] >= 'a' && arg[0] <= 'h' && arg[1] >= '1' && arg[1] <= '8') {
      const auto attempt = app_.game().attempt();
      if (!attempt.readingAvailable) return "ERROR NoObservation\n";
      const Occupancy bit = Occupancy{1} << ((arg[1] - '1') * 8 + arg[0] - 'a');
      value = verb == "lift" ? attempt.currentOccupancy & ~bit : attempt.currentOccupancy | bit;
    } else { inputLost(); return "ERROR Observation\n"; }
    if (app_.game().state() == GameState::Finished) return "ERROR GameFinished\n";
    simRaw_ = value;
    simAvailable_ = true;
    advance(75);
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
