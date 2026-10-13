#include <Arduino.h>
#include <GameController.h>
#include "controller_tests.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>

namespace {
using namespace chessboard;

constexpr uint32_t heapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr unsigned sampleCount = 200;
constexpr unsigned warmupCount = 10;
uint32_t samples[sampleCount];
unsigned failures = 0;
unsigned reportedCases = 0;
bool reportCases = false;

enum class Operation { Confirm, Flow };
enum class Sequence { Ordinary, Capture, Castle, EnPassant, Ambiguous, FullHistory,
                      Promotion, Duplicate, Reselection, IllegalRearm, InterruptedRearm,
                      ManualPending, ResignationPending };

struct Fixture {
  const char* name;
  Operation operation;
  const char* fen;
  Move move;
  Sequence sequence;
  Promotion selection;
  Square extraFrom;
  Square extraTo;
  size_t legalCount;
  unsigned frames;
  ConfirmationStatus expected;
  GameState state;
  EndReason reason;
  GameResult result;
};

constexpr char initialFen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr char castleFen[] = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
constexpr char captureFen[] = "k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1";
constexpr char epFen[] = "k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1";
constexpr char promotionFen[] = "7k/P7/8/8/8/8/8/4K3 w - - 0 1";

const Fixture fixtures[] = {
  {"confirm_initial_e2e4", Operation::Confirm, initialFen, {12,28,Promotion::None}, Sequence::Ordinary,
   Promotion::None,64,64,20,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_kiwipete_castle", Operation::Confirm, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
   {4,6,Promotion::None},Sequence::Castle,Promotion::None,7,5,48,3,
   ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_high_mobility", Operation::Confirm, "7k/8/8/8/2Q5/8/Q4Q2/4K3 w - - 0 1",
   {26,27,Promotion::None},Sequence::Ordinary,Promotion::None,64,64,62,2,
   ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_capture_e4d5",Operation::Confirm,captureFen,{28,35,Promotion::None},Sequence::Capture,
   Promotion::None,64,64,7,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_castle_white",Operation::Confirm,castleFen,{4,6,Promotion::None},Sequence::Castle,
   Promotion::None,7,5,26,3,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_en_passant_white",Operation::Confirm,epFen,{36,43,Promotion::None},Sequence::EnPassant,
   Promotion::None,35,64,7,3,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_promotion_required",Operation::Confirm,promotionFen,{48,56,Promotion::Queen},Sequence::Promotion,
   Promotion::None,64,64,9,2,ConfirmationStatus::PromotionRequired,GameState::AwaitingPromotion,EndReason::None,GameResult::Ongoing},
  {"confirm_promotion_queen",Operation::Confirm,promotionFen,{48,56,Promotion::Queen},Sequence::Promotion,
   Promotion::Queen,64,64,9,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_promotion_knight",Operation::Confirm,promotionFen,{48,56,Promotion::Knight},Sequence::Promotion,
   Promotion::Knight,64,64,9,2,ConfirmationStatus::Accepted,GameState::Finished,EndReason::InsufficientMaterial,GameResult::Draw},
  {"confirm_ambiguous_capture",Operation::Confirm,"k7/8/8/2p1p3/3B4/8/8/4K3 w - - 0 1",
   {27,34,Promotion::None},Sequence::Ambiguous,Promotion::None,36,64,13,4,
   ConfirmationStatus::Ambiguous,GameState::AwaitingPosition,EndReason::None,GameResult::Ongoing},
  {"confirm_history_64_frames",Operation::Confirm,initialFen,{12,28,Promotion::None},Sequence::FullHistory,
   Promotion::None,64,64,20,64,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_duplicate_confirmation",Operation::Confirm,initialFen,{12,28,Promotion::None},Sequence::Duplicate,
   Promotion::None,64,64,20,0,ConfirmationStatus::NoMove,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"confirm_checkmate",Operation::Confirm,"rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq g3 0 2",
   {59,31,Promotion::None},Sequence::Ordinary,Promotion::None,64,64,30,2,
   ConfirmationStatus::Accepted,GameState::Finished,EndReason::Checkmate,GameResult::BlackWin},
  {"confirm_stalemate",Operation::Confirm,"7k/5K2/8/6Q1/8/8/8/8 w - - 0 1",
   {38,46,Promotion::None},Sequence::Ordinary,Promotion::None,64,64,29,2,
   ConfirmationStatus::Accepted,GameState::Finished,EndReason::Stalemate,GameResult::Draw},
  {"confirm_insufficient_material",Operation::Confirm,"4k3/8/8/8/8/8/3n4/2B1K3 w - - 0 1",
   {2,11,Promotion::None},Sequence::Capture,Promotion::None,64,64,7,2,
   ConfirmationStatus::Accepted,GameState::Finished,EndReason::InsufficientMaterial,GameResult::Draw},
  {"flow_initial_e2e4",Operation::Flow,initialFen,{12,28,Promotion::None},Sequence::Ordinary,
   Promotion::None,64,64,20,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_capture_e4d5",Operation::Flow,captureFen,{28,35,Promotion::None},Sequence::Capture,
   Promotion::None,64,64,7,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_castle_white",Operation::Flow,castleFen,{4,6,Promotion::None},Sequence::Castle,
   Promotion::None,7,5,26,3,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_en_passant_white",Operation::Flow,epFen,{36,43,Promotion::None},Sequence::EnPassant,
   Promotion::None,35,64,7,3,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_promotion_queen",Operation::Flow,promotionFen,{48,56,Promotion::Queen},Sequence::Promotion,
   Promotion::Queen,64,64,9,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_promotion_knight",Operation::Flow,promotionFen,{48,56,Promotion::Knight},Sequence::Promotion,
   Promotion::Knight,64,64,9,2,ConfirmationStatus::Accepted,GameState::Finished,EndReason::InsufficientMaterial,GameResult::Draw},
  {"flow_promotion_reselection",Operation::Flow,promotionFen,{48,56,Promotion::Knight},Sequence::Reselection,
   Promotion::Knight,64,64,9,2,ConfirmationStatus::Accepted,GameState::Finished,EndReason::InsufficientMaterial,GameResult::Draw},
  {"flow_illegal_rearm",Operation::Flow,initialFen,{12,28,Promotion::None},Sequence::IllegalRearm,
   Promotion::None,64,64,20,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_interruption_rearm",Operation::Flow,initialFen,{12,28,Promotion::None},Sequence::InterruptedRearm,
   Promotion::None,64,64,20,2,ConfirmationStatus::Accepted,GameState::Tracking,EndReason::None,GameResult::Ongoing},
  {"flow_manual_pending",Operation::Flow,promotionFen,{48,56,Promotion::Queen},Sequence::ManualPending,
   Promotion::Queen,64,64,9,2,ConfirmationStatus::GameFinished,GameState::Finished,EndReason::Manual,GameResult::Unfinished},
  {"flow_resignation_pending",Operation::Flow,promotionFen,{48,56,Promotion::Queen},Sequence::ResignationPending,
   Promotion::Queen,64,64,9,2,ConfirmationStatus::GameFinished,GameState::Finished,EndReason::Resignation,GameResult::BlackWin}
};

constexpr unsigned fixtureCount = sizeof(fixtures) / sizeof(fixtures[0]);

struct Observations {
  Occupancy initial = 0;
  Occupancy frames[kHistoryCapacity]{};
  unsigned count = 0;
};

struct Trace {
  ConfirmationResult confirmations[3]{};
  GameState states[3]{};
  Occupancy officialOccupancies[3]{};
  Color sidesToMove[3]{};
  unsigned count = 0;
  bool selections[2]{};
  unsigned selectionCount = 0;
  bool ended = false;
  bool cancelled = false;
  bool rearmed = false;
};

struct ExpectedPosition {
  Piece pieces[64]{};
  Occupancy occupancy = 0;
  Color sideToMove = Color::White;
};

const char* statusName(ConfirmationStatus status) {
  switch (status) {
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

const char* stateName(GameState state) {
  switch (state) {
    case GameState::AwaitingPosition: return "AwaitingPosition";
    case GameState::Tracking: return "Tracking";
    case GameState::AwaitingPromotion: return "AwaitingPromotion";
    case GameState::Finished: return "Finished";
  }
  return "Unknown";
}

const char* reasonName(EndReason reason) {
  switch (reason) {
    case EndReason::None: return "None";
    case EndReason::Checkmate: return "Checkmate";
    case EndReason::Stalemate: return "Stalemate";
    case EndReason::InsufficientMaterial: return "InsufficientMaterial";
    case EndReason::Resignation: return "Resignation";
    case EndReason::Manual: return "Manual";
  }
  return "Unknown";
}

const char* resultName(GameResult result) {
  switch (result) {
    case GameResult::Ongoing: return "Ongoing";
    case GameResult::WhiteWin: return "WhiteWin";
    case GameResult::BlackWin: return "BlackWin";
    case GameResult::Draw: return "Draw";
    case GameResult::Unfinished: return "Unfinished";
  }
  return "Unknown";
}

Occupancy squareMask(Square square) { return Occupancy{1} << square; }

void replay(GameController& game, const Observations& observations) {
  for (unsigned i = 0; i < observations.count; ++i) game.observe(observations.frames[i], i + 1);
}

bool sameMove(const Move& a, const Move& b) {
  return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

bool confirms(const ConfirmationResult& actual, ConfirmationStatus expected, const Move& move = {}) {
  return actual.status == expected && sameMove(actual.move, move);
}

bool prepare(const Fixture& fixture, ChessAdapter& board, Observations& observations,
             GameController& base, ExpectedPosition& expected) {
  if (!board.loadFen(fixture.fen) || board.legalMoves().count != fixture.legalCount ||
      !board.isLegal(fixture.move)) return false;
  base = GameController(board);
  if (base.state() != GameState::AwaitingPosition) return false;
  observations.initial = board.occupancy();
  Occupancy current = observations.initial;
  observations.count = 0;
  if (fixture.sequence == Sequence::FullHistory) {
    for (unsigned i = 0; i < 62; ++i) {
      observations.frames[observations.count++] = i % 2 ? current : current & ~squareMask(6);
    }
  }
  if (fixture.sequence == Sequence::Ambiguous) {
    observations.frames[observations.count++] = current & ~squareMask(fixture.extraFrom);
    observations.frames[observations.count++] = current;
  }
  current &= ~squareMask(fixture.move.from);
  if (fixture.sequence == Sequence::Capture || fixture.sequence == Sequence::Ambiguous) {
    current &= ~squareMask(fixture.move.to);
  }
  observations.frames[observations.count++] = current;
  current |= squareMask(fixture.move.to);
  observations.frames[observations.count++] = current;
  if (fixture.sequence == Sequence::Castle) {
    current = (current & ~squareMask(fixture.extraFrom)) | squareMask(fixture.extraTo);
    observations.frames[observations.count++] = current;
  }
  if (fixture.sequence == Sequence::EnPassant) {
    current &= ~squareMask(fixture.extraFrom);
    observations.frames[observations.count++] = current;
  }
  if (fixture.sequence != Sequence::Duplicate && observations.count != fixture.frames) return false;

  expected.occupancy = observations.initial;
  expected.sideToMove = board.sideToMove();
  for (Square square = 0; square < 64; ++square) expected.pieces[square] = board.pieceAt(square);
  if (fixture.expected == ConfirmationStatus::Accepted || fixture.sequence == Sequence::Duplicate) {
    expected.occupancy = current;
    expected.sideToMove = board.sideToMove() == Color::White ? Color::Black : Color::White;
    expected.pieces[fixture.move.to] = expected.pieces[fixture.move.from];
    expected.pieces[fixture.move.from] = Piece::Empty;
    if (fixture.move.promotion == Promotion::Queen) expected.pieces[fixture.move.to] = Piece::WhiteQueen;
    if (fixture.move.promotion == Promotion::Knight) expected.pieces[fixture.move.to] = Piece::WhiteKnight;
    if (fixture.sequence == Sequence::Castle) {
      expected.pieces[fixture.extraTo] = expected.pieces[fixture.extraFrom];
      expected.pieces[fixture.extraFrom] = Piece::Empty;
    }
    if (fixture.sequence == Sequence::EnPassant) expected.pieces[fixture.extraFrom] = Piece::Empty;
  }

  if (fixture.operation == Operation::Confirm) {
    base.observe(observations.initial, 0);
    replay(base, observations);
    if (fixture.sequence == Sequence::Duplicate) {
      if (!confirms(base.confirm(), ConfirmationStatus::Accepted, fixture.move)) return false;
    } else if (fixture.selection != Promotion::None) {
      if (!confirms(base.confirm(), ConfirmationStatus::PromotionRequired) ||
          !base.selectPromotion(fixture.selection)) return false;
    }
    if (base.attempt().frameCount != fixture.frames ||
        base.position().legalMoves().count != fixture.legalCount) return false;
  }
  return true;
}

void recordConfirmation(GameController& game, Trace& trace) {
  trace.confirmations[trace.count] = game.confirm();
  trace.states[trace.count] = game.state();
  trace.officialOccupancies[trace.count] = game.position().occupancy();
  trace.sidesToMove[trace.count] = game.position().sideToMove();
  ++trace.count;
}

void flow(const Fixture& fixture, const Observations& observations, GameController& game, Trace& trace) {
  game.observe(observations.initial, 0);
  if (fixture.sequence == Sequence::IllegalRearm) {
    game.observe(observations.initial & ~squareMask(12), 1);
    game.observe((observations.initial & ~squareMask(12)) | squareMask(36), 2);
  } else {
    replay(game, observations);
  }
  if (fixture.sequence == Sequence::InterruptedRearm) game.markInputInterrupted();
  recordConfirmation(game, trace);
  if (fixture.sequence == Sequence::IllegalRearm || fixture.sequence == Sequence::InterruptedRearm) {
    game.observe(observations.initial, 3);
    trace.rearmed = game.state() == GameState::Tracking;
    replay(game, observations);
    recordConfirmation(game, trace);
  } else if (fixture.sequence == Sequence::Promotion || fixture.sequence == Sequence::Reselection ||
             fixture.sequence == Sequence::ManualPending || fixture.sequence == Sequence::ResignationPending) {
    const Promotion first = fixture.sequence == Sequence::Reselection ? Promotion::Queen : fixture.selection;
    trace.selections[trace.selectionCount++] = game.selectPromotion(first);
    if (fixture.sequence == Sequence::Reselection) {
      const Occupancy promoted = observations.frames[observations.count - 1];
      game.observe(promoted & ~squareMask(fixture.move.to), 3);
      trace.cancelled = game.selectedPromotion() == Promotion::None && game.state() == GameState::Tracking;
      game.observe(promoted, 4);
      recordConfirmation(game, trace);
      trace.selections[trace.selectionCount++] = game.selectPromotion(fixture.selection);
    }
    if (fixture.sequence == Sequence::ManualPending) trace.ended = game.finish();
    if (fixture.sequence == Sequence::ResignationPending) trace.ended = game.resign(Color::White);
    recordConfirmation(game, trace);
  }
}

bool validate(const Fixture& fixture, const Observations& observations, const ExpectedPosition& expected,
              GameController& game, const Trace& trace) {
  if (trace.count == 0 || trace.count > 3 ||
      !confirms(trace.confirmations[trace.count - 1], fixture.expected,
                fixture.expected == ConfirmationStatus::Accepted ? fixture.move : Move{}) ||
      game.state() != fixture.state || game.ending().reason != fixture.reason ||
      game.ending().result != fixture.result || game.position().occupancy() != expected.occupancy ||
      game.position().sideToMove() != expected.sideToMove || game.selectedPromotion() != Promotion::None) return false;
  for (Square square = 0; square < 64; ++square) {
    if (game.position().pieceAt(square) != expected.pieces[square]) return false;
  }
  const auto attempt = game.attempt();
  if (!attempt.reliable() || attempt.currentOccupancy != observations.frames[observations.count - 1]) return false;
  if (fixture.expected == ConfirmationStatus::Accepted || fixture.expected == ConfirmationStatus::NoMove) {
    if (attempt.frameCount != 0 || attempt.initialOccupancy != expected.occupancy) return false;
  } else if (attempt.frameCount != fixture.frames || attempt.initialOccupancy != observations.initial) {
    return false;
  }

  if (fixture.operation == Operation::Flow) {
    for (unsigned i = 0; i < trace.selectionCount; ++i) if (!trace.selections[i]) return false;
    for (unsigned i = 0; i + 1 < trace.count; ++i) {
      if (trace.officialOccupancies[i] != observations.initial ||
          trace.sidesToMove[i] != Color::White) return false;
    }
    if (fixture.sequence == Sequence::IllegalRearm || fixture.sequence == Sequence::InterruptedRearm) {
      const ConfirmationStatus rejected = fixture.sequence == Sequence::IllegalRearm
          ? ConfirmationStatus::NoLegalMatch : ConfirmationStatus::UnreliableInput;
      if (trace.count != 2 || !confirms(trace.confirmations[0], rejected) ||
          trace.states[0] != GameState::AwaitingPosition || !trace.rearmed) return false;
    } else if (fixture.selection != Promotion::None) {
      if (!confirms(trace.confirmations[0], ConfirmationStatus::PromotionRequired) ||
          trace.states[0] != GameState::AwaitingPromotion) return false;
      if (fixture.sequence == Sequence::Reselection) {
        if (trace.count != 3 || trace.selectionCount != 2 || !trace.cancelled ||
            !confirms(trace.confirmations[1], ConfirmationStatus::PromotionRequired) ||
            trace.states[1] != GameState::AwaitingPromotion) return false;
      } else if (trace.count != 2 || trace.selectionCount != 1) {
        return false;
      }
      if ((fixture.sequence == Sequence::ManualPending || fixture.sequence == Sequence::ResignationPending) &&
          !trace.ended) return false;
    } else if (trace.count != 1) {
      return false;
    }
  } else if (trace.count != 1) {
    return false;
  }
  if (fixture.state == GameState::Finished) {
    if (!confirms(game.confirm(), ConfirmationStatus::GameFinished) || game.finish() ||
        game.resign(Color::White) || game.selectPromotion(Promotion::Queen)) return false;
    game.observe(0, 100);
    game.markInputInterrupted();
    if (game.position().occupancy() != expected.occupancy ||
        game.attempt().currentOccupancy != attempt.currentOccupancy || !game.attempt().reliable()) return false;
  }
  return true;
}

bool measure(const Fixture& fixture, const Observations& observations, const GameController& base,
             const ExpectedPosition& expected, uint32_t& elapsed) {
  GameController game = base;
  Trace trace;
  if (fixture.operation == Operation::Confirm) {
    const int64_t start = esp_timer_get_time();
    const auto result = game.confirm();
    elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
    trace.confirmations[0] = result;
    trace.states[0] = game.state();
    trace.count = 1;
  } else {
    const int64_t start = esp_timer_get_time();
    flow(fixture, observations, game, trace);
    elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
  }
  return validate(fixture, observations, expected, game, trace);
}

void memory(const char* phase) {
  const auto free = heap_caps_get_free_size(heapCaps);
  const auto minimum = heap_caps_get_minimum_free_size(heapCaps);
  const auto largest = heap_caps_get_largest_free_block(heapCaps);
  const auto stack = uxTaskGetStackHighWaterMark(nullptr);
  const bool intact = heap_caps_check_integrity_all(true);
  if (!intact) ++failures;
  Serial.printf("@{\"type\":\"memory\",\"phase\":\"%s\",\"free_bytes\":%u,"
                "\"minimum_free_bytes\":%u,\"largest_block_bytes\":%u,"
                "\"stack_min_free_bytes\":%u,\"heap_intact\":%s}\n",
                phase, unsigned(free), unsigned(minimum), unsigned(largest),
                unsigned(stack), intact ? "true" : "false");
}

void reportTest(const char* name, bool passed) {
  feedLoopWDT();
  delay(1);
  if (!reportCases) return;
  ++reportedCases;
  Serial.printf("@{\"type\":\"case\",\"name\":\"%s\",\"passed\":%s}\n",
                name, passed ? "true" : "false");
}

void runSuite(bool report) {
  reportCases = report;
  const int result = runControllerTests(nullptr, reportTest);
  if (result != 0) failures += result > 0 ? unsigned(result) : 1;
}

bool monitorStart() {
  const bool ok = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
  if (!ok) ++failures;
  return ok;
}

void monitorStop(bool active) {
  if (active && heap_caps_monitor_local_minimum_free_size_stop() != ESP_OK) ++failures;
}

void benchmarks() {
  for (const auto& fixture : fixtures) {
    ChessAdapter board;
    GameController base;
    Observations observations;
    ExpectedPosition expected;
    if (!prepare(fixture, board, observations, base, expected)) {
      ++failures;
      Serial.printf("@{\"type\":\"fixture_error\",\"fixture\":\"%s\"}\n", fixture.name);
      continue;
    }
    const unsigned beforeFailures = failures;
    uint32_t elapsed = 0;
    for (unsigned i = 0; i < warmupCount; ++i) {
      if (!measure(fixture, observations, base, expected, elapsed)) ++failures;
    }
    Serial.flush();
    const auto beforeMonitor = heap_caps_get_free_size(heapCaps);
    const bool monitoring = monitorStart();
    const auto before = heap_caps_get_free_size(heapCaps);
    uint64_t total = 0;
    for (unsigned i = 0; i < sampleCount; ++i) {
      if (!measure(fixture, observations, base, expected, samples[i])) ++failures;
      total += samples[i];
      if ((i % 20) == 0) { feedLoopWDT(); delay(1); }
    }
    const auto minimum = heap_caps_get_minimum_free_size(heapCaps);
    const auto after = heap_caps_get_free_size(heapCaps);
    const auto stack = uxTaskGetStackHighWaterMark(nullptr);
    monitorStop(monitoring);
    const auto afterMonitor = heap_caps_get_free_size(heapCaps);
    std::sort(samples, samples + sampleCount);
    Serial.printf("@{\"type\":\"benchmark\",\"fixture\":\"%s\",\"operation\":\"%s\","
                  "\"timed_scope\":\"%s\",\"legal_moves\":%u,\"frames\":%u,\"expected_status\":\"%s\","
                  "\"expected_state\":\"%s\",\"expected_reason\":\"%s\",\"expected_result\":\"%s\",\"selection\":%u,"
                  "\"warmups\":%u,\"samples\":%u,\"min_us\":%u,\"median_us\":%.1f,\"mean_us\":%.2f,\"max_us\":%u,"
                  "\"heap_before_bytes\":%u,\"heap_after_bytes\":%u,\"heap_min_free_bytes\":%u,"
                  "\"heap_before_monitor_bytes\":%u,\"heap_after_monitor_bytes\":%u,"
                  "\"stack_min_free_bytes\":%u,\"failures\":%u}\n",
                  fixture.name, fixture.operation == Operation::Confirm ? "confirm" : "flow",
                  fixture.operation == Operation::Confirm ? "confirm_only" : "observations_commands_scalar_trace",
                  unsigned(fixture.legalCount), fixture.frames, statusName(fixture.expected), stateName(fixture.state),
                  reasonName(fixture.reason), resultName(fixture.result), unsigned(fixture.selection), warmupCount, sampleCount,
                  unsigned(samples[0]), (samples[sampleCount / 2 - 1] + samples[sampleCount / 2]) / 2.0,
                  double(total) / sampleCount, unsigned(samples[sampleCount - 1]),
                  unsigned(before), unsigned(after), unsigned(minimum), unsigned(beforeMonitor), unsigned(afterMonitor),
                  unsigned(stack), failures - beforeFailures);
  }
}

void stability(unsigned seconds) {
  Serial.flush();
  const unsigned beforeFailures = failures;
  const auto beforeMonitor = heap_caps_get_free_size(heapCaps);
  const bool monitoring = monitorStart();
  memory("stability_start");
  const int64_t start = esp_timer_get_time();
  int64_t nextReport = start + 10000000;
  unsigned batches = 0;
  unsigned operations = 0;
  unsigned suites = 0;
  while (esp_timer_get_time() - start < int64_t(seconds) * 1000000 && failures == beforeFailures) {
    for (const auto& fixture : fixtures) {
      ChessAdapter board;
      GameController base;
      Observations observations;
      ExpectedPosition expected;
      if (!prepare(fixture, board, observations, base, expected)) { ++failures; break; }
      uint32_t elapsed;
      if (!measure(fixture, observations, base, expected, elapsed)) ++failures;
      ++operations;
      feedLoopWDT();
    }
    ++batches;
    if (esp_timer_get_time() >= nextReport) {
      runSuite(false);
      ++suites;
      memory("stability_checkpoint");
      Serial.printf("@{\"type\":\"progress\",\"elapsed_ms\":%llu,\"batches\":%u,\"suites\":%u,\"failures\":%u}\n",
                    static_cast<unsigned long long>((esp_timer_get_time() - start) / 1000), batches, suites, failures);
      nextReport += 10000000;
    }
    delay(1);
  }
  const auto elapsedMs = static_cast<unsigned long long>((esp_timer_get_time() - start) / 1000);
  memory("stability_end");
  monitorStop(monitoring);
  const auto afterMonitor = heap_caps_get_free_size(heapCaps);
  Serial.printf("@{\"type\":\"stability\",\"elapsed_ms\":%llu,\"batches\":%u,"
                "\"operations\":%u,\"operation_unit\":\"fixture_runs\",\"additional_suites\":%u,"
                "\"heap_before_monitor_bytes\":%u,\"heap_after_monitor_bytes\":%u,\"failures\":%u}\n",
                elapsedMs, batches, operations, suites, unsigned(beforeMonitor), unsigned(afterMonitor), failures);
}

void run(const char* id, unsigned seconds) {
  failures = 0;
  reportedCases = 0;
  const bool watchdog = esp_task_wdt_status(nullptr) == ESP_OK;
  if (!watchdog) ++failures;
  Serial.printf("@{\"type\":\"begin\",\"id\":\"%s\",\"chip\":\"%s\",\"revision\":%u,"
                "\"cores\":%u,\"cpu_mhz\":%u,\"flash_bytes\":%u,\"psram_bytes\":%u,"
                "\"idf\":\"%s\",\"reset_reason\":%u,\"task_core\":%d,\"stack_allocated_bytes\":%u,"
                "\"adapter_bytes\":%u,\"legal_moves_bytes\":%u,\"snapshot_bytes\":%u,\"tracker_bytes\":%u,"
                "\"controller_bytes\":%u,\"result_bytes\":%u,\"fixture_count\":%u,\"suite_cases\":29,"
                "\"loop_watchdog_active\":%s}\n",
                id, ESP.getChipModel(), unsigned(ESP.getChipRevision()), unsigned(ESP.getChipCores()), unsigned(ESP.getCpuFreqMHz()),
                unsigned(ESP.getFlashChipSize()), unsigned(ESP.getPsramSize()), esp_get_idf_version(), unsigned(esp_reset_reason()),
                xPortGetCoreID(), unsigned(getArduinoLoopTaskStackSize()), unsigned(sizeof(ChessAdapter)),
                unsigned(sizeof(LegalMoves)), unsigned(sizeof(AttemptSnapshot)), unsigned(sizeof(MoveTracker)),
                unsigned(sizeof(GameController)), unsigned(sizeof(ConfirmationResult)), fixtureCount, watchdog ? "true" : "false");
  Serial.flush();
  memory("before_suite");
  const bool monitoring = monitorStart();
  memory("suite_start_local_minimum");
  runSuite(true);
  memory("after_suite_local_minimum");
  monitorStop(monitoring);
  memory("after_suite_monitor_stopped");
  benchmarks();
  stability(seconds);
  runSuite(true);
  memory("final");
  Serial.printf("@{\"type\":\"end\",\"id\":\"%s\",\"reported_cases\":%u,\"failures\":%u}\n",
                id, reportedCases, failures);
  Serial.flush();
}
}

void startProbe() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  enableLoopWDT();
}

void pollProbe() {
  static char command[80];
  static size_t length = 0;
  static uint32_t lastReady = 0;
  if (millis() - lastReady >= 1000) {
    Serial.println("@{\"type\":\"ready\",\"protocol\":1}");
    lastReady = millis();
  }
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      command[length] = '\0';
      char id[33] = {};
      unsigned seconds = 0;
      if (std::sscanf(command, "RUN %32[0-9a-f] %u", id, &seconds) == 2 && seconds >= 10 && seconds <= 3600) {
        run(id, seconds);
      }
      length = 0;
    } else if (c != '\r') {
      if (length < sizeof(command) - 1) command[length++] = c;
      else length = 0;
    }
  }
  delay(1);
}
