#include <Arduino.h>
#include <GameRecord.h>
#include "record_tests.h"
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
constexpr unsigned watchdogMs = 30000;
uint32_t samples[sampleCount];
unsigned failures = 0;
unsigned reportedCases = 0;
bool reportCases = false;
bool watchdogConfigured = false;

const Move cycle[] = {{6,21,Promotion::None}, {62,45,Promotion::None},
                      {21,6,Promotion::None}, {45,62,Promotion::None}};
const char* cycleSan[] = {"Nf3", "Nf6", "Ng1", "Ng8"};
constexpr char initialFen[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr char kiwipeteFen[] = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
constexpr char mobilityFen[] = "7k/8/8/8/2Q5/8/Q4Q2/4K3 w - - 0 1";

enum class Operation { Record, San, Pgn, Finish };
struct Fixture {
  const char* name;
  Operation operation;
  const char* fen;
  Move move;
  const char* san;
  unsigned plies;
  unsigned legalCount;
  bool fullMetadata;
  RecordStatus expected;
  GameEnd ending;
  const char* marker;
};

const Fixture fixtures[] = {
  {"record_initial",Operation::Record,initialFen,{12,28,Promotion::None},"e4",0,20,false,RecordStatus::Ok,{},"*"},
  {"record_kiwipete",Operation::Record,kiwipeteFen,{4,6,Promotion::None},"O-O",0,48,false,RecordStatus::Ok,{},"*"},
  {"record_mobility",Operation::Record,mobilityFen,{26,27,Promotion::None},"Qcd4+",0,62,false,RecordStatus::Ok,{},"*"},
  {"record_capture",Operation::Record,"k7/8/8/3p4/4P3/8/8/4K3 w - - 0 1",{28,35,Promotion::None},"exd5",0,7,false,RecordStatus::Ok,{},"*"},
  {"record_en_passant",Operation::Record,"k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1",{36,43,Promotion::None},"exd6",0,7,false,RecordStatus::Ok,{},"*"},
  {"record_promotion",Operation::Record,"7k/P7/8/8/8/8/8/4K3 w - - 0 1",{48,56,Promotion::Queen},"a8=Q+",0,9,false,RecordStatus::Ok,{},"*"},
  {"record_mate",Operation::Record,"rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq g3 0 2",{59,31,Promotion::None},"Qh4#",0,30,false,RecordStatus::Ok,{},"*"},
  {"record_near",Operation::Record,initialFen,{45,62,Promotion::None},"Ng8",1275,22,false,RecordStatus::Ok,{},"*"},
  {"record_overflow",Operation::Record,initialFen,{6,21,Promotion::None},"Nf3",1276,20,false,RecordStatus::CapacityExceeded,{},"*"},
  {"san_initial",Operation::San,initialFen,{12,28,Promotion::None},"e4",0,20,false,RecordStatus::Ok,{},"*"},
  {"san_kiwipete",Operation::San,kiwipeteFen,{4,6,Promotion::None},"O-O",0,48,false,RecordStatus::Ok,{},"*"},
  {"san_mobility",Operation::San,mobilityFen,{26,27,Promotion::None},"Qcd4+",0,62,false,RecordStatus::Ok,{},"*"},
  {"pgn_empty",Operation::Pgn,initialFen,{},"",0,20,false,RecordStatus::Ok,{},"*"},
  {"pgn_100",Operation::Pgn,initialFen,{},"",100,20,false,RecordStatus::Ok,{},"*"},
  {"pgn_600",Operation::Pgn,initialFen,{},"",600,20,false,RecordStatus::Ok,{},"*"},
  {"pgn_near_metadata",Operation::Pgn,initialFen,{},"",1276,20,true,RecordStatus::Ok,{},"*"},
  {"finish_manual",Operation::Finish,initialFen,{},"",100,20,false,RecordStatus::Ok,{EndReason::Manual,GameResult::Unfinished},"*"},
  {"finish_resignation",Operation::Finish,initialFen,{},"",100,20,false,RecordStatus::Ok,{EndReason::Resignation,GameResult::WhiteWin},"1-0"},
  {"finish_mate",Operation::Finish,"7k/6Q1/5K2/8/8/8/8/8 b - - 0 1",{},"",0,0,false,RecordStatus::Ok,{EndReason::Checkmate,GameResult::WhiteWin},"1-0"},
  {"finish_stalemate",Operation::Finish,"7k/5K2/6Q1/8/8/8/8/8 b - - 0 1",{},"",0,0,false,RecordStatus::Ok,{EndReason::Stalemate,GameResult::Draw},"1/2-1/2"},
  {"finish_material",Operation::Finish,"4k3/8/8/8/8/8/8/4K3 w - - 0 1",{},"",0,5,false,RecordStatus::Ok,{EndReason::InsufficientMaterial,GameResult::Draw},"1/2-1/2"}
};
constexpr unsigned fixtureCount = sizeof(fixtures) / sizeof(fixtures[0]);

const char* operationName(Operation operation) {
  switch (operation) {
    case Operation::Record: return "record";
    case Operation::San: return "san";
    case Operation::Pgn: return "pgn";
    case Operation::Finish: return "finish";
  }
  return "unknown";
}

GameMetadata metadata(bool full) {
  GameMetadata values;
  if (full) {
    values.event = std::string(kMetadataValueCapacity, 'E');
    values.site = std::string(kMetadataValueCapacity, 'S');
    values.round = std::string(kMetadataValueCapacity, 'R');
    values.white = std::string(kMetadataValueCapacity, 'W');
    values.black = std::string(kMetadataValueCapacity, 'B');
  }
  return values;
}

uint32_t digest(const std::string& text, uint32_t value = 2166136261u) {
  for (unsigned char ch : text) value = (value ^ ch) * 16777619u;
  return value;
}

bool validPgn(const GameRecord& record, const std::string& pgn, const char* marker) {
  const std::string tag = std::string("[Result \"") + marker + "\"]\n";
  if (pgn.find(tag) == std::string::npos) return false;
  const auto separator = pgn.find("\n\n");
  if (separator == std::string::npos) return false;
  size_t cursor = separator + 2;
  unsigned line = 0;
  for (size_t i = cursor; i < pgn.size(); ++i) {
    if (pgn[i] == '\n') line = 0;
    else if (++line > 79) return false;
  }
  for (char ch : record.movetext()) {
    if (cursor >= pgn.size() || (pgn[cursor] == '\n' ? ' ' : pgn[cursor]) != ch) return false;
    ++cursor;
  }
  if (!record.movetext().empty()) {
    if (cursor >= pgn.size() || (pgn[cursor] != ' ' && pgn[cursor] != '\n')) return false;
    ++cursor;
  }
  return pgn.substr(cursor) == std::string(marker) + "\n\n";
}

void memory(const char* phase) {
  const bool intact = heap_caps_check_integrity_all(true);
  if (!intact) ++failures;
  Serial.printf("@{\"type\":\"memory\",\"phase\":\"%s\",\"free_bytes\":%u,\"minimum_free_bytes\":%u,"
                "\"largest_block_bytes\":%u,\"stack_min_free_bytes\":%u,\"heap_intact\":%s}\n",
                phase, unsigned(heap_caps_get_free_size(heapCaps)), unsigned(heap_caps_get_minimum_free_size(heapCaps)),
                unsigned(heap_caps_get_largest_free_block(heapCaps)), unsigned(uxTaskGetStackHighWaterMark(nullptr)),
                intact ? "true" : "false");
}

bool monitorStart() {
  const bool active = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
  if (!active) ++failures;
  return active;
}
void monitorStop(bool active) {
  if (active && heap_caps_monitor_local_minimum_free_size_stop() != ESP_OK) ++failures;
}
void reportTest(const char* name, bool passed) {
  feedLoopWDT();
  delay(1);
  if (!reportCases) return;
  ++reportedCases;
  Serial.printf("@{\"type\":\"case\",\"name\":\"%s\",\"passed\":%s}\n", name, passed ? "true" : "false");
}
void runSuite(bool report) {
  reportCases = report;
  const int count = runRecordTests(nullptr, reportTest);
  if (count != 0) failures += count > 0 ? unsigned(count) : 1;
}

bool prepare(const Fixture& fixture, GameRecord& base, ChessAdapter& before, ChessAdapter& after) {
  if (!before.loadFen(fixture.fen) || base.begin(before, metadata(fixture.fullMetadata)) != RecordStatus::Ok) return false;
  for (unsigned i = 0; i < fixture.plies; ++i) {
    const ChessAdapter previous = before;
    if (!before.apply(cycle[i % 4]) ||
        base.record(previous, {ConfirmationStatus::Accepted,cycle[i % 4]}, before) != RecordStatus::Ok) return false;
    if (i % 32 == 0) { feedLoopWDT(); delay(1); }
  }
  if (before.legalMoves().count != fixture.legalCount) return false;
  after = before;
  if (fixture.operation == Operation::Record || fixture.operation == Operation::San) {
    if (!after.apply(fixture.move)) return false;
  }
  return true;
}

bool measure(const Fixture& fixture, const GameRecord& base, const ChessAdapter& before,
             const ChessAdapter& after, uint32_t& elapsed, size_t& outputBytes, size_t& outputCapacity) {
  if (fixture.operation == Operation::Pgn || fixture.operation == Operation::San) {
    std::string output;
    bool ok = true;
    const int64_t start = esp_timer_get_time();
    if (fixture.operation == Operation::Pgn) output = base.pgn();
    else ok = before.san(fixture.move, output);
    elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
    outputBytes = output.size();
    outputCapacity = output.capacity();
    return ok && (fixture.operation == Operation::Pgn ? validPgn(base,output,"*") : output == fixture.san);
  }
  GameRecord record = base;
  RecordStatus status;
  const int64_t start = esp_timer_get_time();
  if (fixture.operation == Operation::Record) status = record.record(before,{ConfirmationStatus::Accepted,fixture.move},after);
  else status = record.finish(before,fixture.ending);
  elapsed = static_cast<uint32_t>(esp_timer_get_time() - start);
  outputBytes = record.movetext().size();
  outputCapacity = record.movetext().capacity();
  if (status != fixture.expected) return false;
  if (fixture.operation == Operation::Finish) {
    return record.finished() && record.movetext() == base.movetext() && validPgn(record,record.pgn(),fixture.marker);
  }
  if (status == RecordStatus::CapacityExceeded) return record.movetext() == base.movetext() && record.plyCount() == base.plyCount();
  const std::string suffix = fixture.san;
  return record.plyCount() == base.plyCount() + 1 && outputBytes >= suffix.size() &&
         record.movetext().compare(outputBytes - suffix.size(),suffix.size(),suffix) == 0;
}

void benchmarks() {
  for (const auto& fixture : fixtures) {
    GameRecord base;
    ChessAdapter before, after;
    if (!prepare(fixture,base,before,after)) {
      ++failures;
      Serial.printf("@{\"type\":\"fixture_error\",\"fixture\":\"%s\"}\n",fixture.name);
      continue;
    }
    const unsigned previousFailures = failures;
    uint32_t elapsed = 0;
    size_t outputBytes = 0, outputCapacity = 0;
    for (unsigned i = 0; i < warmupCount; ++i) {
      if (!measure(fixture,base,before,after,elapsed,outputBytes,outputCapacity)) ++failures;
      feedLoopWDT();
    }
    Serial.flush();
    const auto beforeMonitor = heap_caps_get_free_size(heapCaps);
    const bool monitoring = monitorStart();
    const auto freeBefore = heap_caps_get_free_size(heapCaps);
    uint64_t total = 0;
    for (unsigned i = 0; i < sampleCount; ++i) {
      if (!measure(fixture,base,before,after,samples[i],outputBytes,outputCapacity)) ++failures;
      total += samples[i];
      if (i % 20 == 0) { feedLoopWDT(); delay(1); }
    }
    const auto minimum = heap_caps_get_minimum_free_size(heapCaps);
    const auto freeAfter = heap_caps_get_free_size(heapCaps);
    monitorStop(monitoring);
    std::sort(samples,samples + sampleCount);
    Serial.printf("@{\"type\":\"benchmark\",\"fixture\":\"%s\",\"operation\":\"%s\",\"legal_moves\":%u,"
                  "\"plies\":%u,\"movetext_bytes\":%u,\"base_capacity_bytes\":%u,\"full_metadata\":%s,"
                  "\"output_bytes\":%u,\"output_capacity_bytes\":%u,\"warmups\":%u,\"samples\":%u,"
                  "\"min_us\":%u,\"median_us\":%.1f,\"mean_us\":%.2f,\"max_us\":%u,"
                  "\"heap_before_bytes\":%u,\"heap_after_bytes\":%u,\"heap_min_free_bytes\":%u,"
                  "\"heap_before_monitor_bytes\":%u,\"heap_after_monitor_bytes\":%u,\"stack_min_free_bytes\":%u,\"failures\":%u}\n",
                  fixture.name,operationName(fixture.operation),fixture.legalCount,fixture.plies,
                  unsigned(base.movetext().size()),unsigned(base.movetext().capacity()),fixture.fullMetadata ? "true" : "false",
                  unsigned(outputBytes),unsigned(outputCapacity),warmupCount,sampleCount,
                  unsigned(samples[0]),(samples[99] + samples[100])/2.0,double(total)/sampleCount,unsigned(samples[199]),
                  unsigned(freeBefore),unsigned(freeAfter),unsigned(minimum),unsigned(beforeMonitor),
                  unsigned(heap_caps_get_free_size(heapCaps)),unsigned(uxTaskGetStackHighWaterMark(nullptr)),failures-previousFailures);
  }
}

struct LongStats {
  unsigned accepted = 0;
  unsigned exports = 0;
  uint32_t maxConfirmUs = 0;
  uint32_t maxRecordUs = 0;
  uint32_t maxPgnUs = 0;
  uint32_t maxFlowUs = 0;
};

bool longGame(GameController& game, GameRecord& record, bool fullMetadata, bool report, LongStats& stats) {
  game.reset();
  if (record.begin(game.position(),metadata(fullMetadata)) != RecordStatus::Ok) return false;
  game.observe(game.position().occupancy(),0);
  uint32_t expectedHash = 2166136261u;
  size_t expectedBytes = 0;
  for (unsigned i = 0; i < 1400; ++i) {
    const ChessAdapter before = game.position();
    const Move move = cycle[i % 4];
    const Occupancy lifted = before.occupancy() & ~(Occupancy{1} << move.from);
    const std::string addition = (i ? " " : "") + (i % 2 == 0 ? std::to_string(i/2+1) + ". " : "") + cycleSan[i%4];
    const bool overflow = expectedBytes + addition.size() > kMovetextCapacity;
    const int64_t flowStart = esp_timer_get_time();
    game.observe(lifted,i*2+1);
    game.observe(lifted | (Occupancy{1} << move.to),i*2+2);
    const int64_t confirmStart = esp_timer_get_time();
    const auto confirmation = game.confirm();
    const uint32_t confirmUs = static_cast<uint32_t>(esp_timer_get_time()-confirmStart);
    const int64_t recordStart = esp_timer_get_time();
    const auto status = record.record(before,confirmation,game.position());
    const uint32_t recordUs = static_cast<uint32_t>(esp_timer_get_time()-recordStart);
    const auto freeBeforePgn = heap_caps_get_free_size(heapCaps);
    size_t pgnBytes = 0, pgnCapacity = 0;
    uint32_t freeWithPgn = 0, pgnUs = 0, flowUs = 0;
    bool pgnOk = false;
    {
      const int64_t pgnStart = esp_timer_get_time();
      const std::string pgn = record.pgn();
      pgnUs = static_cast<uint32_t>(esp_timer_get_time()-pgnStart);
      flowUs = static_cast<uint32_t>(esp_timer_get_time()-flowStart);
      pgnBytes = pgn.size();
      pgnCapacity = pgn.capacity();
      freeWithPgn = heap_caps_get_free_size(heapCaps);
      pgnOk = validPgn(record,pgn,"*");
    }
    ++stats.exports;
    stats.maxConfirmUs = std::max(stats.maxConfirmUs,confirmUs);
    stats.maxRecordUs = std::max(stats.maxRecordUs,recordUs);
    stats.maxPgnUs = std::max(stats.maxPgnUs,pgnUs);
    stats.maxFlowUs = std::max(stats.maxFlowUs,flowUs);
    if (!pgnOk || confirmation.status != ConfirmationStatus::Accepted ||
        game.state() != GameState::Tracking ||
        status != (overflow ? RecordStatus::CapacityExceeded : RecordStatus::Ok)) return false;
    if (!overflow) {
      expectedHash = digest(addition,expectedHash);
      expectedBytes += addition.size();
      ++stats.accepted;
    }
    if (record.movetext().size() != expectedBytes || record.plyCount() != i + (overflow ? 0 : 1)) return false;
    if (report && ((i+1)%256 == 0 || expectedBytes >= 8170)) {
      Serial.printf("@{\"type\":\"long_checkpoint\",\"plies\":%u,\"movetext_bytes\":%u,\"capacity_bytes\":%u,"
                    "\"pgn_bytes\":%u,\"pgn_capacity_bytes\":%u,\"heap_before_pgn_bytes\":%u,\"heap_with_pgn_bytes\":%u,"
                    "\"heap_after_pgn_bytes\":%u,\"record_us\":%u,\"pgn_us\":%u,\"overflow\":%s}\n",
                    unsigned(record.plyCount()),unsigned(expectedBytes),unsigned(record.movetext().capacity()),
                    unsigned(pgnBytes),unsigned(pgnCapacity),unsigned(freeBeforePgn),unsigned(freeWithPgn),
                    unsigned(heap_caps_get_free_size(heapCaps)),unsigned(recordUs),unsigned(pgnUs),overflow ? "true":"false");
    }
    feedLoopWDT();
    if (i % 16 == 0) delay(1);
    if (overflow) {
      if (record.plyCount() != 1276 || expectedBytes != 8185 || digest(record.movetext()) != expectedHash ||
          record.record(before,confirmation,game.position()) != RecordStatus::CapacityExceeded ||
          !game.finish() || record.finish(game.position(),game.ending()) != RecordStatus::InvalidContext ||
          record.finished() || digest(record.movetext()) != expectedHash) return false;
      if (report) {
        Serial.printf("@{\"type\":\"long_game\",\"accepted\":%u,\"exports\":%u,\"movetext_bytes\":%u,"
                      "\"remaining_bytes\":%u,\"rejected_addition_bytes\":%u,\"digest\":%u,\"full_metadata\":%s,"
                      "\"max_confirm_us\":%u,\"max_record_us\":%u,\"max_pgn_us\":%u,\"max_flow_us\":%u,"
                      "\"overflow_preserved\":true,\"finish_rejected\":true}\n",
                      stats.accepted,stats.exports,unsigned(expectedBytes),unsigned(kMovetextCapacity-expectedBytes),
                      unsigned(addition.size()),unsigned(expectedHash),fullMetadata ? "true":"false",
                      unsigned(stats.maxConfirmUs),unsigned(stats.maxRecordUs),unsigned(stats.maxPgnUs),unsigned(stats.maxFlowUs));
        const std::string pgn = record.pgn();
        Serial.printf("@{\"type\":\"pgn_export\",\"name\":\"long_game\",\"bytes\":%u,\"digest\":%u}\n",
                      unsigned(pgn.size()),unsigned(digest(pgn)));
        Serial.println("#PGN_BEGIN");
        Serial.write(reinterpret_cast<const uint8_t*>(pgn.data()),pgn.size());
        Serial.println("#PGN_END");
      }
      return true;
    }
  }
  return false;
}

void prolonged() {
  memory("before_long_game");
  const bool monitoring = monitorStart();
  {
    GameController game;
    GameRecord record;
    LongStats stats;
    if (!longGame(game,record,true,true,stats)) ++failures;
    memory("long_game_live");
  }
  memory("long_game_destroyed");
  monitorStop(monitoring);
  memory("after_long_monitor_stopped");
}

void stability(unsigned seconds) {
  Serial.flush();
  const unsigned previousFailures = failures;
  const auto beforeMonitor = heap_caps_get_free_size(heapCaps);
  const bool monitoring = monitorStart();
  const int64_t start = esp_timer_get_time();
  int64_t nextSuite = start + 30000000;
  unsigned games = 0, suites = 0;
  LongStats stats;
  memory("stability_start");
  {
    GameController game;
    GameRecord record;
    while (esp_timer_get_time()-start < int64_t(seconds)*1000000 && failures == previousFailures) {
      if (!longGame(game,record,games%2==0,false,stats)) ++failures;
      ++games;
      memory("stability_game_end");
      Serial.printf("@{\"type\":\"progress\",\"elapsed_ms\":%llu,\"games\":%u,\"accepted\":%u,\"exports\":%u,\"failures\":%u}\n",
                    static_cast<unsigned long long>((esp_timer_get_time()-start)/1000),games,stats.accepted,stats.exports,failures);
      if (esp_timer_get_time() >= nextSuite) {
        runSuite(false);
        ++suites;
        nextSuite = esp_timer_get_time()+30000000;
      }
      delay(1);
    }
  }
  const auto elapsedMs = static_cast<unsigned long long>((esp_timer_get_time()-start)/1000);
  memory("stability_destroyed");
  monitorStop(monitoring);
  Serial.printf("@{\"type\":\"stability\",\"elapsed_ms\":%llu,\"games\":%u,\"accepted\":%u,\"exports\":%u,"
                "\"additional_suites\":%u,\"max_confirm_us\":%u,\"max_record_us\":%u,\"max_pgn_us\":%u,\"max_flow_us\":%u,"
                "\"heap_before_monitor_bytes\":%u,\"heap_after_monitor_bytes\":%u,\"failures\":%u}\n",
                elapsedMs,games,stats.accepted,stats.exports,suites,unsigned(stats.maxConfirmUs),unsigned(stats.maxRecordUs),
                unsigned(stats.maxPgnUs),unsigned(stats.maxFlowUs),unsigned(beforeMonitor),unsigned(heap_caps_get_free_size(heapCaps)),
                failures-previousFailures);
}

void run(const char* id, unsigned seconds) {
  failures = 0;
  reportedCases = 0;
  const bool watchdog = watchdogConfigured && esp_task_wdt_status(nullptr) == ESP_OK;
  if (!watchdog) ++failures;
  Serial.printf("@{\"type\":\"begin\",\"id\":\"%s\",\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,\"cpu_mhz\":%u,"
                "\"flash_bytes\":%u,\"psram_bytes\":%u,\"idf\":\"%s\",\"reset_reason\":%u,\"task_core\":%d,"
                "\"stack_allocated_bytes\":%u,\"record_bytes\":%u,\"controller_bytes\":%u,\"adapter_bytes\":%u,"
                "\"movetext_limit_bytes\":%u,\"fixture_count\":%u,\"suite_cases\":26,\"watchdog_ms\":%u,\"loop_watchdog_active\":%s}\n",
                id,ESP.getChipModel(),unsigned(ESP.getChipRevision()),unsigned(ESP.getChipCores()),unsigned(ESP.getCpuFreqMHz()),
                unsigned(ESP.getFlashChipSize()),unsigned(ESP.getPsramSize()),esp_get_idf_version(),unsigned(esp_reset_reason()),
                xPortGetCoreID(),unsigned(getArduinoLoopTaskStackSize()),unsigned(sizeof(GameRecord)),unsigned(sizeof(GameController)),
                unsigned(sizeof(ChessAdapter)),unsigned(kMovetextCapacity),fixtureCount,watchdogMs,watchdog ? "true":"false");
  Serial.flush();
  memory("before_suite");
  const bool monitoring = monitorStart();
  memory("suite_start_local_minimum");
  runSuite(true);
  memory("after_suite_local_minimum");
  monitorStop(monitoring);
  memory("after_suite_monitor_stopped");
  benchmarks();
  prolonged();
  stability(seconds);
  runSuite(true);
  memory("final");
  Serial.printf("@{\"type\":\"end\",\"id\":\"%s\",\"reported_cases\":%u,\"failures\":%u}\n",id,reportedCases,failures);
  Serial.flush();
}
}  // namespace

void startProbe() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  // The unchanged capacity test exports a growing PGN on every iteration.
  const esp_task_wdt_config_t config = {watchdogMs,(1u << portNUM_PROCESSORS)-1,true};
  watchdogConfigured = esp_task_wdt_reconfigure(&config) == ESP_OK;
  enableLoopWDT();
}

void pollProbe() {
  static char command[80];
  static size_t length = 0;
  static uint32_t lastReady = 0;
  if (millis()-lastReady >= 1000) {
    Serial.println("@{\"type\":\"ready\",\"protocol\":1}");
    lastReady = millis();
  }
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      command[length] = '\0';
      char id[33] = {};
      unsigned seconds = 0;
      if (std::sscanf(command,"RUN %32[0-9a-f] %u",id,&seconds) == 2 && seconds >= 10 && seconds <= 3600) run(id,seconds);
      length = 0;
    } else if (c != '\r') {
      if (length < sizeof(command)-1) command[length++] = c;
      else length = 0;
    }
  }
  delay(1);
}

