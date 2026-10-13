#include <Arduino.h>
#include <ChessAdapter.h>
#include "adapter_tests.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace {
using namespace chessboard;

constexpr uint32_t heapCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr unsigned sampleCount = 200;
constexpr unsigned warmupCount = 10;
uint32_t samples[sampleCount];
unsigned failures = 0;
unsigned reportedCases = 0;

struct Fixture {
  const char* name;
  const char* fen;
  Move move;
  const char* san;
  size_t legalCount;
};

const Fixture fixtures[] = {
  {"initial", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
   {12, 28, Promotion::None}, "e4", 20},
  {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
   {4, 6, Promotion::None}, "O-O", 48},
  {"en_passant", "k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
   {36, 43, Promotion::None}, "exd6", 7},
  {"promotion", "7k/P7/8/8/8/8/8/4K3 w - - 0 1",
   {48, 56, Promotion::Queen}, "a8=Q+", 9}
};

enum class Operation { LegalMoves, IsLegal, Reject, San, Apply, Status };
const char* operationNames[] = {"legal_moves", "is_legal", "reject", "san", "apply", "status"};

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
  ++reportedCases;
  Serial.printf("@{\"type\":\"case\",\"name\":\"%s\",\"passed\":%s}\n",
                name, passed ? "true" : "false");
}

void runSuite(bool report) {
  const int result = runAdapterTests(nullptr, report ? reportTest : nullptr);
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

bool measure(const Fixture& fixture, const ChessAdapter& base, Operation operation, uint32_t& elapsed) {
  ChessAdapter board = base;
  LegalMoves legal;
  PositionStatus status;
  std::string san;
  bool accepted = false;
  const Move invalid{fixture.move.from, fixture.move.from, Promotion::None};
  const int64_t start = esp_timer_get_time();
  switch (operation) {
    case Operation::LegalMoves: legal = board.legalMoves(); break;
    case Operation::IsLegal: accepted = board.isLegal(fixture.move); break;
    case Operation::Reject: accepted = board.isLegal(invalid); break;
    case Operation::San: accepted = board.san(fixture.move, san); break;
    case Operation::Apply: accepted = board.apply(fixture.move); break;
    case Operation::Status: status = board.status(); break;
  }
  elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
  switch (operation) {
    case Operation::LegalMoves: return legal.count == fixture.legalCount;
    case Operation::IsLegal: return accepted;
    case Operation::Reject: return !accepted;
    case Operation::San: return accepted && san == fixture.san;
    case Operation::Apply:
      return accepted && board.sideToMove() == Color::Black &&
             board.pieceAt(fixture.move.from) == Piece::Empty &&
             board.pieceAt(fixture.move.to) != Piece::Empty;
    case Operation::Status:
      return status.sideToMove == Color::White && status.terminal == TerminalState::None && !status.inCheck;
  }
  return false;
}

void benchmarks() {
  for (const auto& fixture : fixtures) {
    ChessAdapter base;
    if (!base.loadFen(fixture.fen)) { ++failures; continue; }
    for (unsigned op = 0; op < 6; ++op) {
      uint32_t elapsed = 0;
      for (unsigned i = 0; i < warmupCount; ++i) {
        if (!measure(fixture, base, static_cast<Operation>(op), elapsed)) ++failures;
      }
      Serial.flush();
      const unsigned beforeFailures = failures;
      const auto beforeMonitor = heap_caps_get_free_size(heapCaps);
      const bool monitoring = monitorStart();
      const auto before = heap_caps_get_free_size(heapCaps);
      uint64_t total = 0;
      for (unsigned i = 0; i < sampleCount; ++i) {
        if (!measure(fixture, base, static_cast<Operation>(op), samples[i])) ++failures;
        total += samples[i];
        if ((i % 20) == 0) delay(1);
      }
      const auto minimum = heap_caps_get_minimum_free_size(heapCaps);
      const auto after = heap_caps_get_free_size(heapCaps);
      const auto stack = uxTaskGetStackHighWaterMark(nullptr);
      monitorStop(monitoring);
      const auto afterMonitor = heap_caps_get_free_size(heapCaps);
      std::sort(samples, samples + sampleCount);
      Serial.printf("@{\"type\":\"benchmark\",\"fixture\":\"%s\",\"operation\":\"%s\","
                    "\"samples\":%u,\"min_us\":%u,\"median_us\":%.1f,\"mean_us\":%.2f,\"max_us\":%u,"
                    "\"heap_before_bytes\":%u,\"heap_after_bytes\":%u,\"heap_min_free_bytes\":%u,"
                    "\"heap_before_monitor_bytes\":%u,\"heap_after_monitor_bytes\":%u,"
                    "\"stack_min_free_bytes\":%u,\"failures\":%u}\n",
                    fixture.name, operationNames[op], sampleCount, unsigned(samples[0]),
                    (samples[sampleCount / 2 - 1] + samples[sampleCount / 2]) / 2.0,
                    double(total) / sampleCount, unsigned(samples[sampleCount - 1]),
                    unsigned(before), unsigned(after), unsigned(minimum), unsigned(beforeMonitor), unsigned(afterMonitor),
                    unsigned(stack), failures - beforeFailures);
    }
  }
}

void stability(unsigned seconds) {
  const int64_t start = esp_timer_get_time();
  int64_t nextReport = start + 10000000;
  unsigned batches = 0;
  unsigned suites = 0;
  const bool monitoring = monitorStart();
  memory("stability_start");
  while (esp_timer_get_time() - start < int64_t(seconds) * 1000000 && failures == 0) {
    for (const auto& fixture : fixtures) {
      ChessAdapter base;
      if (!base.loadFen(fixture.fen)) { ++failures; break; }
      for (unsigned op = 0; op < 6; ++op) {
        uint32_t elapsed;
        if (!measure(fixture, base, static_cast<Operation>(op), elapsed)) ++failures;
      }
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
  memory("stability_end");
  monitorStop(monitoring);
  Serial.printf("@{\"type\":\"stability\",\"elapsed_ms\":%llu,\"batches\":%u,"
                "\"operations\":%u,\"additional_suites\":%u,\"failures\":%u}\n",
                static_cast<unsigned long long>((esp_timer_get_time() - start) / 1000),
                batches, batches * 24, suites, failures);
}

void run(const char* id, unsigned seconds) {
  failures = 0;
  reportedCases = 0;
  Serial.printf("@{\"type\":\"begin\",\"id\":\"%s\",\"chip\":\"%s\",\"revision\":%u,"
                "\"cores\":%u,\"cpu_mhz\":%u,\"flash_bytes\":%u,\"psram_bytes\":%u,"
                "\"idf\":\"%s\",\"reset_reason\":%u,\"task_core\":%d,\"stack_allocated_bytes\":%u,"
                "\"adapter_bytes\":%u,\"legal_moves_bytes\":%u,\"thc_rules_bytes\":%u,\"thc_move_bytes\":%u}\n",
                id, ESP.getChipModel(), unsigned(ESP.getChipRevision()), unsigned(ESP.getChipCores()), unsigned(ESP.getCpuFreqMHz()),
                unsigned(ESP.getFlashChipSize()), unsigned(ESP.getPsramSize()), esp_get_idf_version(), unsigned(esp_reset_reason()),
                xPortGetCoreID(), unsigned(getArduinoLoopTaskStackSize()), unsigned(sizeof(ChessAdapter)),
                unsigned(sizeof(LegalMoves)), unsigned(sizeof(thc::ChessRules)), unsigned(sizeof(thc::Move)));
  Serial.flush();
  memory("before_suite");
  const bool monitoring = monitorStart();
  memory("suite_start_local_minimum");
  runSuite(true);
  memory("after_suite_local_minimum");
  monitorStop(monitoring);
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
