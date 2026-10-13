#include <Arduino.h>
#include <MoveInterpreter.h>
#include "interpreter_tests.h"
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

enum class Sequence { Ordinary, Capture, Castle, EnPassant, NoMove, Ambiguous, FullHistory, Interrupted };

struct Fixture {
  const char* name;
  const char* fen;
  Move move;
  Sequence sequence;
  InterpretationStatus expected;
  Promotion selection;
  Square extraFrom;
  Square extraTo;
  size_t legalCount;
};

constexpr char initialFen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr char whiteCastleFen[] = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
constexpr char blackCastleFen[] = "r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1";
constexpr char whitePromotionFen[] = "7k/P7/8/8/8/8/8/4K3 w - - 0 1";
constexpr char promotionCaptureFen[] = "1r5k/P7/8/8/8/8/8/4K3 w - - 0 1";

const Fixture fixtures[] = {
  {"initial_e2e4", initialFen, {12, 28, Promotion::None}, Sequence::Ordinary,
   InterpretationStatus::Identified, Promotion::None, 64, 64, 20},
  {"kiwipete_castle", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
   {4, 6, Promotion::None}, Sequence::Castle, InterpretationStatus::Identified, Promotion::None, 7, 5, 48},
  {"capture_e4d5", "k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1",
   {28, 35, Promotion::None}, Sequence::Capture, InterpretationStatus::Identified, Promotion::None, 64, 64, 7},
  {"castle_white_kingside", whiteCastleFen, {4, 6, Promotion::None}, Sequence::Castle,
   InterpretationStatus::Identified, Promotion::None, 7, 5, 26},
  {"castle_white_queenside", whiteCastleFen, {4, 2, Promotion::None}, Sequence::Castle,
   InterpretationStatus::Identified, Promotion::None, 0, 3, 26},
  {"castle_black_kingside", blackCastleFen, {60, 62, Promotion::None}, Sequence::Castle,
   InterpretationStatus::Identified, Promotion::None, 63, 61, 26},
  {"castle_black_queenside", blackCastleFen, {60, 58, Promotion::None}, Sequence::Castle,
   InterpretationStatus::Identified, Promotion::None, 56, 59, 26},
  {"en_passant_white", "k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
   {36, 43, Promotion::None}, Sequence::EnPassant, InterpretationStatus::Identified, Promotion::None, 35, 64, 7},
  {"en_passant_black", "4k3/8/8/8/3pP3/8/8/K7 b - e3 0 1",
   {27, 20, Promotion::None}, Sequence::EnPassant, InterpretationStatus::Identified, Promotion::None, 28, 64, 7},
  {"promotion_required", whitePromotionFen, {48, 56, Promotion::Queen}, Sequence::Ordinary,
   InterpretationStatus::PromotionRequired, Promotion::None, 64, 64, 9},
  {"promotion_queen", whitePromotionFen, {48, 56, Promotion::Queen}, Sequence::Ordinary,
   InterpretationStatus::Identified, Promotion::Queen, 64, 64, 9},
  {"promotion_black_queen", "4k3/8/8/8/8/8/p7/7K b - - 0 1",
   {8, 0, Promotion::Queen}, Sequence::Ordinary, InterpretationStatus::Identified, Promotion::Queen, 64, 64, 9},
  {"promotion_capture_required", promotionCaptureFen, {48, 57, Promotion::Queen}, Sequence::Capture,
   InterpretationStatus::PromotionRequired, Promotion::None, 64, 64, 13},
  {"promotion_capture_queen", promotionCaptureFen, {48, 57, Promotion::Queen}, Sequence::Capture,
   InterpretationStatus::Identified, Promotion::Queen, 64, 64, 13},
  {"no_move", initialFen, {12, 12, Promotion::None}, Sequence::NoMove,
   InterpretationStatus::NoMove, Promotion::None, 64, 64, 20},
  {"illegal_e2e5", initialFen, {12, 36, Promotion::None}, Sequence::Ordinary,
   InterpretationStatus::NoLegalMatch, Promotion::None, 64, 64, 20},
  {"ambiguous_capture", "k7/8/8/2p1p3/3B4/8/8/4K3 w - - 0 1",
   {27, 34, Promotion::None}, Sequence::Ambiguous, InterpretationStatus::Ambiguous, Promotion::None, 36, 64, 13},
  {"history_64_frames", initialFen, {12, 28, Promotion::None}, Sequence::FullHistory,
   InterpretationStatus::Identified, Promotion::None, 64, 64, 20},
  {"interrupted_input", initialFen, {12, 28, Promotion::None}, Sequence::Interrupted,
   InterpretationStatus::UnreliableInput, Promotion::None, 64, 64, 20},
  {"synthetic_high_mobility", "7k/8/8/8/2Q5/8/Q4Q2/4K3 w - - 0 1",
   {26, 27, Promotion::None}, Sequence::Ordinary, InterpretationStatus::Identified, Promotion::None, 64, 64, 62}
};

constexpr unsigned fixtureCount = sizeof(fixtures) / sizeof(fixtures[0]);

const char* statusName(InterpretationStatus status) {
  switch (status) {
    case InterpretationStatus::Identified: return "Identified";
    case InterpretationStatus::NoMove: return "NoMove";
    case InterpretationStatus::NoLegalMatch: return "NoLegalMatch";
    case InterpretationStatus::Ambiguous: return "Ambiguous";
    case InterpretationStatus::PromotionRequired: return "PromotionRequired";
    case InterpretationStatus::UnreliableInput: return "UnreliableInput";
    case InterpretationStatus::PositionMismatch: return "PositionMismatch";
  }
  return "Unknown";
}

Occupancy squareMask(Square square) { return Occupancy{1} << square; }

bool prepare(const Fixture& fixture, ChessAdapter& board, AttemptSnapshot& attempt) {
  if (!board.loadFen(fixture.fen) || board.legalMoves().count != fixture.legalCount) return false;
  if ((fixture.expected == InterpretationStatus::Identified ||
       fixture.expected == InterpretationStatus::PromotionRequired) && !board.isLegal(fixture.move)) return false;
  const Occupancy before = board.occupancy();
  Occupancy current = before;
  MoveTracker tracker;
  uint32_t time = 0;
  tracker.observe(current, time++);
  if (fixture.sequence == Sequence::FullHistory) {
    for (unsigned i = 0; i < 62; ++i) {
      tracker.observe(i % 2 ? before : before & ~squareMask(6), time++);
    }
  }
  if (fixture.sequence == Sequence::Ambiguous) {
    current &= ~squareMask(fixture.extraFrom);
    tracker.observe(current, time++);
    current |= squareMask(fixture.extraFrom);
    tracker.observe(current, time++);
  }
  current &= ~squareMask(fixture.move.from);
  if (fixture.sequence == Sequence::Capture || fixture.sequence == Sequence::Ambiguous) {
    current &= ~squareMask(fixture.move.to);
  }
  tracker.observe(current, time++);
  current |= squareMask(fixture.move.to);
  tracker.observe(current, time++);
  if (fixture.sequence == Sequence::Castle) {
    current = (current & ~squareMask(fixture.extraFrom)) | squareMask(fixture.extraTo);
    tracker.observe(current, time++);
  }
  if (fixture.sequence == Sequence::EnPassant) {
    current &= ~squareMask(fixture.extraFrom);
    tracker.observe(current, time++);
  }
  if (fixture.sequence == Sequence::Interrupted) tracker.markInputInterrupted();
  attempt = tracker.snapshot();
  return fixture.sequence != Sequence::FullHistory || attempt.frameCount == kHistoryCapacity;
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
  const int result = runInterpreterTests(nullptr, reportTest);
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

bool measure(const Fixture& fixture, const ChessAdapter& board, const AttemptSnapshot& attempt,
             uint32_t& elapsed) {
  const int64_t start = esp_timer_get_time();
  const auto result = interpretMove(attempt, board, fixture.selection);
  elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
  const Move expected = fixture.expected == InterpretationStatus::Identified ? fixture.move : Move{};
  return result.status == fixture.expected && result.move.from == expected.from &&
         result.move.to == expected.to && result.move.promotion == expected.promotion;
}

void benchmarks() {
  for (const auto& fixture : fixtures) {
    ChessAdapter board;
    AttemptSnapshot attempt;
    if (!prepare(fixture, board, attempt)) {
      ++failures;
      Serial.printf("@{\"type\":\"fixture_error\",\"fixture\":\"%s\"}\n", fixture.name);
      continue;
    }
    const unsigned beforeFailures = failures;
    uint32_t elapsed = 0;
    for (unsigned i = 0; i < warmupCount; ++i) {
      if (!measure(fixture, board, attempt, elapsed)) ++failures;
    }
    Serial.flush();
    const auto beforeMonitor = heap_caps_get_free_size(heapCaps);
    const bool monitoring = monitorStart();
    const auto before = heap_caps_get_free_size(heapCaps);
    uint64_t total = 0;
    for (unsigned i = 0; i < sampleCount; ++i) {
      if (!measure(fixture, board, attempt, samples[i])) ++failures;
      total += samples[i];
      if ((i % 20) == 0) { feedLoopWDT(); delay(1); }
    }
    const auto minimum = heap_caps_get_minimum_free_size(heapCaps);
    const auto after = heap_caps_get_free_size(heapCaps);
    const auto stack = uxTaskGetStackHighWaterMark(nullptr);
    monitorStop(monitoring);
    const auto afterMonitor = heap_caps_get_free_size(heapCaps);
    std::sort(samples, samples + sampleCount);
    Serial.printf("@{\"type\":\"benchmark\",\"fixture\":\"%s\",\"operation\":\"interpret\","
                  "\"legal_moves\":%u,\"frames\":%u,\"expected_status\":\"%s\",\"selection\":%u,"
                  "\"warmups\":%u,\"samples\":%u,\"min_us\":%u,\"median_us\":%.1f,\"mean_us\":%.2f,\"max_us\":%u,"
                  "\"heap_before_bytes\":%u,\"heap_after_bytes\":%u,\"heap_min_free_bytes\":%u,"
                  "\"heap_before_monitor_bytes\":%u,\"heap_after_monitor_bytes\":%u,"
                  "\"stack_min_free_bytes\":%u,\"failures\":%u}\n",
                  fixture.name, unsigned(fixture.legalCount), unsigned(attempt.frameCount),
                  statusName(fixture.expected), unsigned(fixture.selection), warmupCount, sampleCount,
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
      AttemptSnapshot attempt;
      if (!prepare(fixture, board, attempt)) { ++failures; break; }
      uint32_t elapsed;
      if (!measure(fixture, board, attempt, elapsed)) ++failures;
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
                "\"operations\":%u,\"additional_suites\":%u,\"heap_before_monitor_bytes\":%u,"
                "\"heap_after_monitor_bytes\":%u,\"failures\":%u}\n",
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
                "\"adapter_bytes\":%u,\"legal_moves_bytes\":%u,\"snapshot_bytes\":%u,"
                "\"tracker_bytes\":%u,\"result_bytes\":%u,\"fixture_count\":%u,\"loop_watchdog_active\":%s}\n",
                id, ESP.getChipModel(), unsigned(ESP.getChipRevision()), unsigned(ESP.getChipCores()), unsigned(ESP.getCpuFreqMHz()),
                unsigned(ESP.getFlashChipSize()), unsigned(ESP.getPsramSize()), esp_get_idf_version(), unsigned(esp_reset_reason()),
                xPortGetCoreID(), unsigned(getArduinoLoopTaskStackSize()), unsigned(sizeof(ChessAdapter)),
                unsigned(sizeof(LegalMoves)), unsigned(sizeof(AttemptSnapshot)), unsigned(sizeof(MoveTracker)),
                unsigned(sizeof(InterpretationResult)), fixtureCount, watchdog ? "true" : "false");
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
