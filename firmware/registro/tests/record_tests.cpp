#include "record_tests.h"
#include "GameRecord.h"

#include <cstdio>
#include <cstring>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
  std::printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (false)

namespace {
using namespace chessboard;

const char* defaultTags =
  "[Event \"?\"]\n[Site \"?\"]\n[Date \"????.??.??\"]\n[Round \"?\"]\n"
  "[White \"?\"]\n[Black \"?\"]\n[Result \"*\"]\n\n";

Square square(const char* name) {
  return static_cast<Square>((name[1] - '1') * 8 + name[0] - 'a');
}

Occupancy bit(const char* name) {
  return Occupancy{1} << square(name);
}

Move move(const char* from, const char* to, Promotion promotion = Promotion::None) {
  return {square(from), square(to), promotion};
}

bool append(GameRecord& record, ChessAdapter& board, const Move& candidate) {
  const ChessAdapter before = board;
  CHECK(board.apply(candidate));
  CHECK(record.record(before, {ConfirmationStatus::Accepted, candidate}, board) == RecordStatus::Ok);
  return true;
}

bool play(GameRecord& record, GameController& game, const char* from, const char* to,
          const char* captured = nullptr) {
  const ChessAdapter before = game.position();
  Occupancy physical = before.occupancy() & ~bit(from);
  if (captured) physical &= ~bit(captured);
  game.observe(physical, 1);
  game.observe(physical | bit(to), 2);
  const auto confirmation = game.confirm();
  CHECK(confirmation.status == ConfirmationStatus::Accepted);
  CHECK(record.record(before, confirmation, game.position()) == RecordStatus::Ok);
  return true;
}

bool result(const GameRecord& record, const char* token) {
  const auto pgn = record.pgn();
  CHECK(pgn.find(std::string("[Result \"") + token + "\"]\n") != std::string::npos);
  const std::string tail = std::string(token) + "\n\n";
  CHECK(pgn.size() >= tail.size());
  CHECK(pgn.compare(pgn.size() - tail.size(), tail.size(), tail) == 0);
  return true;
}

bool emptyStart() {
  GameRecord record;
  ChessAdapter board;
  CHECK(!record.started());
  CHECK(!record.finished());
  CHECK(record.plyCount() == 0);
  CHECK(record.movetext().empty());
  CHECK(record.pgn().empty());
  CHECK(record.record(board, {ConfirmationStatus::Accepted, move("e2", "e4")}, board)
        == RecordStatus::NotStarted);
  CHECK(record.finish(board, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::NotStarted);
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(record.started());
  CHECK(!record.finished());
  CHECK(record.pgn() == std::string(defaultTags) + "*\n\n");
  CHECK(record.pgn().find("[FEN ") == std::string::npos);
  CHECK(record.pgn().find("[SetUp ") == std::string::npos);
  return true;
}

bool ordinaryPipeline() {
  GameController game;
  GameRecord record;
  CHECK(record.begin(game.position()) == RecordStatus::Ok);
  game.observe(game.position().occupancy(), 0);
  CHECK(play(record, game, "e2", "e4"));
  CHECK(play(record, game, "d7", "d5"));
  CHECK(play(record, game, "e4", "d5", "d5"));
  CHECK(play(record, game, "d8", "d5", "d5"));
  CHECK(play(record, game, "b1", "c3"));
  CHECK(record.plyCount() == 5);
  CHECK(record.movetext() == "1. e4 d5 2. exd5 Qxd5 3. Nc3");
  CHECK(record.pgn() == std::string(defaultTags) + record.movetext() + " *\n\n");
  CHECK(!record.finished());
  return true;
}

bool ignoredConfirmations() {
  const ConfirmationStatus statuses[] = {
    ConfirmationStatus::NoMove, ConfirmationStatus::NoLegalMatch,
    ConfirmationStatus::Ambiguous, ConfirmationStatus::PromotionRequired,
    ConfirmationStatus::UnreliableInput, ConfirmationStatus::PositionMismatch,
    ConfirmationStatus::ApplyFailed, ConfirmationStatus::GameFinished
  };
  ChessAdapter board;
  GameRecord record;
  for (int phase = 0; phase < 3; ++phase) {
    if (phase == 1) CHECK(record.begin(board) == RecordStatus::Ok);
    if (phase == 2) CHECK(record.finish(board, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::Ok);
    const auto pgn = record.pgn();
    for (const auto status : statuses) {
      CHECK(record.record(board, {status, move("e2", "e4")}, board) == RecordStatus::Ignored);
      CHECK(record.pgn() == pgn);
      CHECK(record.plyCount() == 0);
    }
  }
  return true;
}

bool rejectedPipeline() {
  GameController game;
  GameRecord record;
  CHECK(record.begin(game.position()) == RecordStatus::Ok);
  game.observe(game.position().occupancy(), 0);
  CHECK(record.record(game.position(), game.confirm(), game.position()) == RecordStatus::Ignored);
  const auto pgn = record.pgn();
  const auto physical = game.position().occupancy();
  game.observe(physical & ~bit("e2"), 1);
  game.observe((physical & ~bit("e2")) | bit("e5"), 2);
  const auto rejected = game.confirm();
  CHECK(rejected.status == ConfirmationStatus::NoLegalMatch);
  CHECK(record.record(game.position(), rejected, game.position()) == RecordStatus::Ignored);
  CHECK(record.pgn() == pgn);
  game.observe(physical, 3);
  CHECK(play(record, game, "e2", "e4"));
  CHECK(record.record(game.position(), game.confirm(), game.position()) == RecordStatus::Ignored);
  CHECK(record.plyCount() == 1);
  CHECK(record.movetext() == "1. e4");
  return true;
}

bool promotionPipeline() {
  ChessAdapter initial;
  CHECK(initial.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
  GameController game(initial);
  GameRecord record;
  CHECK(record.begin(game.position()) == RecordStatus::Ok);
  const auto pgn = record.pgn();
  game.observe(initial.occupancy(), 0);
  game.observe(initial.occupancy() & ~bit("a7"), 1);
  game.observe((initial.occupancy() & ~bit("a7")) | bit("a8"), 2);
  const auto pending = game.confirm();
  CHECK(pending.status == ConfirmationStatus::PromotionRequired);
  CHECK(record.record(initial, pending, game.position()) == RecordStatus::Ignored);
  CHECK(game.selectPromotion(Promotion::Knight));
  CHECK(record.pgn() == pgn);
  CHECK(game.position().fen() == initial.fen());
  const auto accepted = game.confirm();
  CHECK(accepted.status == ConfirmationStatus::Accepted);
  CHECK(record.record(initial, accepted, game.position()) == RecordStatus::Ok);
  CHECK(record.movetext() == "1. a8=N");
  CHECK(record.plyCount() == 1);
  CHECK(game.state() == GameState::Finished);
  CHECK(record.finish(game.position(), game.ending()) == RecordStatus::Ok);
  CHECK(result(record, "1/2-1/2"));
  return true;
}

bool foolsMate() {
  GameController game;
  GameRecord record;
  CHECK(record.begin(game.position()) == RecordStatus::Ok);
  game.observe(game.position().occupancy(), 0);
  CHECK(play(record, game, "f2", "f3"));
  CHECK(play(record, game, "e7", "e5"));
  CHECK(play(record, game, "g2", "g4"));
  CHECK(play(record, game, "d8", "h4"));
  CHECK(game.state() == GameState::Finished);
  CHECK(record.movetext() == "1. f3 e5 2. g4 Qh4#");
  CHECK(record.finish(game.position(), game.ending()) == RecordStatus::Ok);
  CHECK(record.finished());
  CHECK(result(record, "0-1"));
  CHECK(record.plyCount() == 4);
  return true;
}

bool scholarsMate() {
  GameController game;
  GameRecord record;
  CHECK(record.begin(game.position()) == RecordStatus::Ok);
  game.observe(game.position().occupancy(), 0);
  CHECK(play(record, game, "e2", "e4"));
  CHECK(play(record, game, "e7", "e5"));
  CHECK(play(record, game, "f1", "c4"));
  CHECK(play(record, game, "b8", "c6"));
  CHECK(play(record, game, "d1", "h5"));
  CHECK(play(record, game, "g8", "f6"));
  CHECK(play(record, game, "h5", "f7", "f7"));
  CHECK(record.movetext() == "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7#");
  CHECK(record.finish(game.position(), game.ending()) == RecordStatus::Ok);
  CHECK(result(record, "1-0"));
  CHECK(record.plyCount() == 7);
  return true;
}

bool castling() {
  struct Case { const char* fen; const char* from; const char* to; const char* expected; };
  const Case cases[] = {
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "g1", "1. O-O"},
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1", "c1", "1. O-O-O"},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "g8", "1... O-O"},
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8", "c8", "1... O-O-O"}
  };
  for (const auto& test : cases) {
    ChessAdapter board;
    GameRecord record;
    CHECK(board.loadFen(test.fen));
    CHECK(record.begin(board) == RecordStatus::Ok);
    CHECK(append(record, board, move(test.from, test.to)));
    CHECK(record.movetext() == test.expected);
    CHECK(record.plyCount() == 1);
  }
  return true;
}

bool enPassant() {
  ChessAdapter board;
  GameRecord record;
  CHECK(board.loadFen("k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e5", "d6")));
  CHECK(record.movetext() == "1. exd6");
  CHECK(board.loadFen("4k3/8/8/8/3Pp3/8/8/K7 b - d3 0 1"));
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e4", "d3")));
  CHECK(record.movetext() == "1... exd3");
  return true;
}

bool promotionChoices() {
  const Promotion choices[] = {Promotion::Queen, Promotion::Rook, Promotion::Bishop, Promotion::Knight};
  const char* white[] = {"1. a8=Q+", "1. a8=R+", "1. a8=B", "1. a8=N"};
  const char* black[] = {"1... a1=Q+", "1... a1=R+", "1... a1=B", "1... a1=N"};
  for (int i = 0; i < 4; ++i) {
    ChessAdapter board;
    GameRecord record;
    CHECK(board.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(record.begin(board) == RecordStatus::Ok);
    CHECK(append(record, board, move("a7", "a8", choices[i])));
    CHECK(record.movetext() == white[i]);
    CHECK(board.loadFen("4k3/8/8/8/8/8/p7/7K b - - 0 1"));
    CHECK(record.begin(board) == RecordStatus::Ok);
    CHECK(append(record, board, move("a2", "a1", choices[i])));
    CHECK(record.movetext() == black[i]);
  }
  return true;
}

bool promotionCaptures() {
  const Promotion choices[] = {Promotion::Queen, Promotion::Rook, Promotion::Bishop, Promotion::Knight};
  const char* white[] = {"1. axb8=Q+", "1. axb8=R+", "1. axb8=B", "1. axb8=N"};
  const char* black[] = {"1... axb1=Q+", "1... axb1=R+", "1... axb1=B", "1... axb1=N"};
  for (int i = 0; i < 4; ++i) {
    ChessAdapter board;
    GameRecord record;
    CHECK(board.loadFen("1r5k/P7/8/8/8/8/8/4K3 w - - 0 1"));
    CHECK(record.begin(board) == RecordStatus::Ok);
    CHECK(append(record, board, move("a7", "b8", choices[i])));
    CHECK(record.movetext() == white[i]);
    CHECK(board.loadFen("4k3/8/8/8/8/8/p7/1R5K b - - 0 1"));
    CHECK(record.begin(board) == RecordStatus::Ok);
    CHECK(append(record, board, move("a2", "b1", choices[i])));
    CHECK(record.movetext() == black[i]);
  }
  return true;
}

bool sanDisambiguation() {
  struct Case { const char* fen; const char* from; const char* to; const char* expected; };
  const Case cases[] = {
    {"4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1", "b1", "d2", "1. Nbd2"},
    {"7k/8/8/8/8/R7/8/R6K w - - 0 1", "a1", "a2", "1. R1a2"},
    {"4k3/8/8/8/8/1N6/8/1N2KN2 w - - 0 1", "b1", "d2", "1. Nb1d2"},
    {"4k3/8/8/8/8/8/R7/6K1 w - - 0 1", "a2", "e2", "1. Re2+"}
  };
  for (const auto& test : cases) {
    ChessAdapter board;
    GameRecord record;
    CHECK(board.loadFen(test.fen));
    CHECK(record.begin(board) == RecordStatus::Ok);
    CHECK(append(record, board, move(test.from, test.to)));
    CHECK(record.movetext() == test.expected);
  }
  return true;
}

bool customNumbering() {
  ChessAdapter board;
  CHECK(board.loadFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 42"));
  const auto initial = board.fen();
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e7", "e5")));
  CHECK(append(record, board, move("e2", "e4")));
  CHECK(append(record, board, move("g8", "f6")));
  CHECK(record.movetext() == "42... e5 43. e4 Nf6");
  CHECK(record.pgn().find("[FEN \"" + initial + "\"]\n[SetUp \"1\"]\n\n") != std::string::npos);
  CHECK(board.loadFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 42"));
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e2", "e4")));
  CHECK(record.movetext() == "42. e4");
  return true;
}

bool contextGuard() {
  ChessAdapter initial;
  ChessAdapter after = initial;
  CHECK(after.apply(move("e2", "e4")));
  GameRecord record;
  CHECK(record.begin(initial) == RecordStatus::Ok);
  const auto empty = record.pgn();
  const ConfirmationResult accepted{ConfirmationStatus::Accepted, move("e2", "e4")};
  CHECK(record.record(initial, accepted, initial) == RecordStatus::InvalidContext);
  CHECK(record.pgn() == empty);
  ChessAdapter other;
  CHECK(other.apply(move("d2", "d4")));
  CHECK(record.record(initial, accepted, other) == RecordStatus::InvalidContext);
  CHECK(record.pgn() == empty);
  CHECK(record.record(initial, accepted, after) == RecordStatus::Ok);
  const auto saved = record.pgn();
  CHECK(record.record(initial, accepted, after) == RecordStatus::InvalidContext);
  CHECK(record.pgn() == saved);
  ChessAdapter skipped = after;
  CHECK(skipped.apply(move("e7", "e5")));
  ChessAdapter further = skipped;
  CHECK(further.apply(move("g1", "f3")));
  CHECK(record.record(skipped, {ConfirmationStatus::Accepted, move("g1", "f3")}, further)
        == RecordStatus::InvalidContext);
  CHECK(record.pgn() == saved);
  CHECK(record.record(after, {ConfirmationStatus::Accepted, move("e7", "e5")}, skipped) == RecordStatus::Ok);
  CHECK(record.movetext() == "1. e4 e5");
  return true;
}

bool invalidMoves() {
  ChessAdapter board;
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  const auto before = record.pgn();
  const Move invalid[] = {Move{}, move("e2", "e5"), move("e7", "e5"),
    move("e2", "e4", Promotion::Queen), move("e2", "e4", static_cast<Promotion>(255))};
  for (const auto& candidate : invalid) {
    CHECK(record.record(board, {ConfirmationStatus::Accepted, candidate}, board) == RecordStatus::InvalidMove);
    CHECK(record.pgn() == before);
    CHECK(record.plyCount() == 0);
  }
  CHECK(append(record, board, move("e2", "e4")));
  return true;
}

bool finishResults() {
  for (int mode = 0; mode < 3; ++mode) {
    GameController game;
    GameRecord record;
    CHECK(record.begin(game.position()) == RecordStatus::Ok);
    game.observe(game.position().occupancy(), 0);
    CHECK(play(record, game, "e2", "e4"));
    const auto text = record.movetext();
    if (mode == 0) CHECK(game.finish());
    else CHECK(game.resign(mode == 1 ? Color::White : Color::Black));
    CHECK(record.finish(game.position(), game.ending()) == RecordStatus::Ok);
    CHECK(record.finished());
    CHECK(record.movetext() == text);
    CHECK(record.plyCount() == 1);
    CHECK(result(record, mode == 0 ? "*" : mode == 1 ? "0-1" : "1-0"));
  }
  for (int mode = 0; mode < 3; ++mode) {
    ChessAdapter initial;
    CHECK(initial.loadFen("7k/P7/8/8/8/8/8/4K3 w - - 0 1"));
    GameController game(initial);
    GameRecord record;
    CHECK(record.begin(game.position()) == RecordStatus::Ok);
    game.observe(initial.occupancy(), 0);
    game.observe(initial.occupancy() & ~bit("a7"), 1);
    game.observe((initial.occupancy() & ~bit("a7")) | bit("a8"), 2);
    const auto pending = game.confirm();
    CHECK(pending.status == ConfirmationStatus::PromotionRequired);
    CHECK(record.record(initial, pending, game.position()) == RecordStatus::Ignored);
    CHECK(game.selectPromotion(Promotion::Queen));
    if (mode == 0) CHECK(game.finish());
    else CHECK(game.resign(mode == 1 ? Color::White : Color::Black));
    CHECK(record.finish(game.position(), game.ending()) == RecordStatus::Ok);
    CHECK(record.plyCount() == 0);
    CHECK(record.movetext().empty());
    CHECK(game.position().fen() == initial.fen());
    CHECK(result(record, mode == 0 ? "*" : mode == 1 ? "0-1" : "1-0"));
  }
  return true;
}

bool terminalPositions() {
  struct Case { const char* fen; const char* expected; };
  const Case cases[] = {
    {"7k/5K2/6Q1/8/8/8/8/8 b - - 0 1", "1/2-1/2"},
    {"4k3/8/8/8/8/8/8/4K3 w - - 0 1", "1/2-1/2"},
    {"7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", "1-0"},
    {"8/8/8/8/8/5k2/6q1/7K w - - 0 1", "0-1"}
  };
  for (const auto& test : cases) {
    ChessAdapter initial;
    CHECK(initial.loadFen(test.fen));
    GameController game(initial);
    GameRecord record;
    CHECK(game.state() == GameState::Finished);
    CHECK(record.begin(game.position()) == RecordStatus::Ok);
    CHECK(record.finish(game.position(), game.ending()) == RecordStatus::Ok);
    CHECK(record.plyCount() == 0);
    CHECK(record.movetext().empty());
    CHECK(result(record, test.expected));
  }
  return true;
}

bool finishValidation() {
  ChessAdapter board;
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  const auto saved = record.pgn();
  const GameEnd invalid[] = {
    {EndReason::None, GameResult::Ongoing}, {EndReason::Manual, GameResult::Draw},
    {EndReason::Resignation, GameResult::Draw}, {EndReason::Checkmate, GameResult::WhiteWin},
    {EndReason::Stalemate, GameResult::Draw}, {EndReason::InsufficientMaterial, GameResult::Draw},
    {EndReason::Manual, static_cast<GameResult>(255)},
    {static_cast<EndReason>(255), GameResult::Unfinished}
  };
  for (const auto ending : invalid) {
    CHECK(record.finish(board, ending) == RecordStatus::InvalidResult);
    CHECK(!record.finished());
    CHECK(record.pgn() == saved);
  }
  ChessAdapter different = board;
  CHECK(different.apply(move("e2", "e4")));
  CHECK(record.finish(different, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::InvalidContext);
  CHECK(record.pgn() == saved);
  CHECK(board.loadFen("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1"));
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(record.finish(board, {EndReason::Checkmate, GameResult::BlackWin}) == RecordStatus::InvalidResult);
  CHECK(!record.finished());
  CHECK(record.finish(board, {EndReason::Checkmate, GameResult::WhiteWin}) == RecordStatus::Ok);
  return true;
}

bool finishIdempotence() {
  ChessAdapter board;
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e2", "e4")));
  const GameEnd ending{EndReason::Manual, GameResult::Unfinished};
  CHECK(record.finish(board, ending) == RecordStatus::Ok);
  const auto saved = record.pgn();
  for (int i = 0; i < 3; ++i) CHECK(record.finish(board, ending) == RecordStatus::Ok);
  CHECK(record.finish(board, {EndReason::Resignation, GameResult::WhiteWin}) == RecordStatus::AlreadyFinished);
  ChessAdapter after = board;
  CHECK(after.apply(move("e7", "e5")));
  CHECK(record.record(board, {ConfirmationStatus::Accepted, move("e7", "e5")}, after) == RecordStatus::AlreadyFinished);
  CHECK(record.finish(after, ending) == RecordStatus::AlreadyFinished);
  CHECK(record.pgn() == saved);
  CHECK(record.plyCount() == 1);
  return true;
}

bool reset() {
  ChessAdapter board;
  GameRecord record;
  GameMetadata metadata;
  metadata.event = "First game";
  CHECK(record.begin(board, metadata) == RecordStatus::Ok);
  CHECK(append(record, board, move("e2", "e4")));
  CHECK(record.finish(board, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::Ok);
  board.reset();
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(record.started());
  CHECK(!record.finished());
  CHECK(record.plyCount() == 0);
  CHECK(record.movetext().empty());
  CHECK(record.pgn() == std::string(defaultTags) + "*\n\n");
  CHECK(append(record, board, move("d2", "d4")));
  CHECK(record.movetext() == "1. d4");
  return true;
}

bool metadata() {
  ChessAdapter board;
  GameRecord record;
  GameMetadata values;
  values.event = "Clube \"A\" \\ final";
  values.site = "UTFPR";
  values.date = "2026.10.13";
  values.round = "2";
  values.white = "Jo\xc3\xa3o";
  values.black = "Maria";
  CHECK(record.begin(board, values) == RecordStatus::Ok);
  const std::string expected =
    "[Event \"Clube \\\"A\\\" \\\\ final\"]\n[Site \"UTFPR\"]\n[Date \"2026.10.13\"]\n"
    "[Round \"2\"]\n[White \"Jo\xc3\xa3o\"]\n[Black \"Maria\"]\n[Result \"*\"]\n\n*\n\n";
  CHECK(record.pgn() == expected);
  values.event.clear(); values.site.clear(); values.round.clear();
  values.white.clear(); values.black.clear();
  values.date.clear();
  CHECK(record.begin(board, values) == RecordStatus::Ok);
  CHECK(record.pgn() == std::string(defaultTags) + "*\n\n");
  values.event = std::string(kMetadataValueCapacity, 'E');
  CHECK(record.begin(board, values) == RecordStatus::Ok);
  CHECK(record.pgn().find("[Event \"" + values.event + "\"]") == 0);
  return true;
}

bool metadataInvalid() {
  ChessAdapter board;
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e2", "e4")));
  const auto saved = record.pgn();
  const std::string invalid[] = {"bad\nline", "bad\rline", "bad\tline", std::string("a\0b", 3),
    std::string(1, '\x7f'), std::string(kMetadataValueCapacity + 1, 'X'),
    std::string(kMetadataValueCapacity, '"'), std::string(kMetadataValueCapacity, '\\')};
  for (const auto& value : invalid) {
    for (int field = 0; field < 5; ++field) {
      GameMetadata values;
      std::string* fields[] = {&values.event, &values.site, &values.round, &values.white, &values.black};
      *fields[field] = value;
      CHECK(record.begin(board, values) == RecordStatus::InvalidMetadata);
      CHECK(record.pgn() == saved);
      CHECK(record.plyCount() == 1);
    }
  }
  CHECK(append(record, board, move("e7", "e5")));
  CHECK(record.movetext() == "1. e4 e5");
  return true;
}

bool dateValidation() {
  ChessAdapter board;
  GameRecord record;
  GameMetadata values;
  const char* valid[] = {"????.??.??", "2026.??.??", "2026.10.??", "2026.??.13", "2026.01.01", "2026.12.31"};
  for (const auto date : valid) {
    values.date = date;
    CHECK(record.begin(board, values) == RecordStatus::Ok);
    CHECK(record.pgn().find(std::string("[Date \"") + date + "\"]") != std::string::npos);
  }
  const auto saved = record.pgn();
  const char* invalid[] = {"2026-10-13", "2026.1.13", "26.10.13", "202?.10.13", "2026.0?.13",
    "2026.10.?3", "2026.00.13", "2026.13.13", "2026.10.00", "2026.10.32", "2026.10.13\n"};
  for (const auto date : invalid) {
    values.date = date;
    CHECK(record.begin(board, values) == RecordStatus::InvalidMetadata);
    CHECK(record.pgn() == saved);
  }
  return true;
}

bool wrapping() {
  ChessAdapter board;
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  std::string expected;
  for (int i = 0; i < 30; ++i) {
    CHECK(append(record, board, move("g1", "f3")));
    CHECK(append(record, board, move("g8", "f6")));
    CHECK(append(record, board, move("f3", "g1")));
    CHECK(append(record, board, move("f6", "g8")));
    if (i) expected += ' ';
    expected += std::to_string(2 * i + 1) + ". Nf3 Nf6 " + std::to_string(2 * i + 2) + ". Ng1 Ng8";
  }
  CHECK(record.movetext() == expected);
  const auto pgn = record.pgn();
  CHECK(pgn.find(defaultTags) == 0);
  std::string body = pgn.substr(std::strlen(defaultTags));
  CHECK(body.size() > 79);
  CHECK(body.substr(body.size() - 2) == "\n\n");
  body.resize(body.size() - 2);
  std::size_t lineStart = 0;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    if (i != body.size() && body[i] != '\n') continue;
    CHECK(i - lineStart <= 79);
    CHECK(i > lineStart);
    CHECK(body[lineStart] != ' ' && body[i - 1] != ' ');
    lineStart = i + 1;
  }
  for (auto& character : body) if (character == '\n') character = ' ';
  CHECK(body == expected + " *");
  return true;
}

bool capacity() {
  ChessAdapter board;
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  const Move cycle[] = {move("g1", "f3"), move("g8", "f6"), move("f3", "g1"), move("f6", "g8")};
  bool full = false;
  for (std::size_t i = 0; i < kMovetextCapacity; ++i) {
    const ChessAdapter before = board;
    const Move candidate = cycle[i % 4];
    CHECK(board.apply(candidate));
    const auto text = record.movetext();
    const auto pgn = record.pgn();
    const auto count = record.plyCount();
    const auto status = record.record(before, {ConfirmationStatus::Accepted, candidate}, board);
    if (status == RecordStatus::CapacityExceeded) {
      CHECK(record.movetext() == text);
      CHECK(record.pgn() == pgn);
      CHECK(record.plyCount() == count);
      CHECK(record.movetext().size() <= kMovetextCapacity);
      CHECK(kMovetextCapacity - record.movetext().size() < 16);
      CHECK(record.record(before, {ConfirmationStatus::Accepted, candidate}, board) == RecordStatus::CapacityExceeded);
      CHECK(record.finish(board, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::InvalidContext);
      CHECK(record.finish(before, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::Ok);
      full = true;
      break;
    }
    CHECK(status == RecordStatus::Ok);
    CHECK(record.plyCount() == count + 1);
  }
  CHECK(full);
  board.reset();
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(append(record, board, move("e2", "e4")));
  CHECK(record.movetext() == "1. e4");
  return true;
}

bool historyPreservation() {
  ChessAdapter board;
  const Move cycle[] = {move("g1", "f3"), move("g8", "f6"), move("f3", "g1"), move("f6", "g8")};
  for (int i = 0; i < 2; ++i) for (const auto& candidate : cycle) CHECK(board.apply(candidate));
  CHECK(board.status().threefoldRepetitionClaim);
  const auto initial = board.fen();
  GameRecord record;
  CHECK(record.begin(board) == RecordStatus::Ok);
  CHECK(board.fen() == initial);
  CHECK(board.status().threefoldRepetitionClaim);
  for (const auto& candidate : cycle) {
    const ChessAdapter before = board;
    const auto beforeFen = before.fen();
    const auto beforeStatus = before.status();
    CHECK(board.apply(candidate));
    const auto afterFen = board.fen();
    const auto afterStatus = board.status();
    CHECK(record.record(before, {ConfirmationStatus::Accepted, candidate}, board) == RecordStatus::Ok);
    CHECK(before.fen() == beforeFen);
    CHECK(before.status().threefoldRepetitionClaim == beforeStatus.threefoldRepetitionClaim);
    CHECK(board.fen() == afterFen);
    CHECK(board.status().threefoldRepetitionClaim == afterStatus.threefoldRepetitionClaim);
  }
  CHECK(board.status().threefoldRepetitionClaim);
  CHECK(record.movetext() == "5. Nf3 Nf6 6. Ng1 Ng8");
  const auto finalFen = board.fen();
  CHECK(record.finish(board, {EndReason::Manual, GameResult::Unfinished}) == RecordStatus::Ok);
  CHECK(board.fen() == finalFen);
  CHECK(board.status().threefoldRepetitionClaim);
  return true;
}
}  // namespace

int runRecordTests(const char* selected, void (*report)(const char*, bool)) {
  struct Case { const char* name; bool (*run)(); };
  const Case cases[] = {
    {"empty_start", emptyStart}, {"ordinary_pipeline", ordinaryPipeline},
    {"ignored_confirmations", ignoredConfirmations}, {"rejected_pipeline", rejectedPipeline},
    {"promotion_pipeline", promotionPipeline}, {"fools_mate", foolsMate},
    {"scholars_mate", scholarsMate}, {"castling", castling},
    {"en_passant", enPassant}, {"promotion_choices", promotionChoices},
    {"promotion_captures", promotionCaptures}, {"san_disambiguation", sanDisambiguation},
    {"custom_numbering", customNumbering}, {"context_guard", contextGuard},
    {"invalid_moves", invalidMoves}, {"finish_results", finishResults},
    {"terminal_positions", terminalPositions}, {"finish_validation", finishValidation},
    {"finish_idempotence", finishIdempotence}, {"reset", reset},
    {"metadata", metadata}, {"metadata_invalid", metadataInvalid},
    {"date_validation", dateValidation}, {"wrapping", wrapping},
    {"capacity", capacity}, {"history_preservation", historyPreservation}
  };
  int failures = 0;
  int executed = 0;
  for (const auto& test : cases) {
    if (selected && std::strcmp(selected, test.name)) continue;
    const bool passed = test.run();
    if (report) report(test.name, passed);
    if (!passed) ++failures;
    ++executed;
  }
  return executed ? failures : -1;
}
