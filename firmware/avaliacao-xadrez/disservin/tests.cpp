#include "chess.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int passed = 0;
void check(bool condition, const char* label) {
    if (!condition) throw std::runtime_error(label);
    ++passed;
    std::cout << "PASS " << label << '\n';
}

bool legal(const chess::Board& board, const chess::Move& candidate) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto& move : moves) if (move == candidate) return true;
    return false;
}

void roundTrip(const char* fen, const char* uci, const char* san, const char* after) {
    chess::Board board(fen);
    const auto before = board;
    const auto move = chess::uci::uciToMove(board, uci);
    check(legal(board, move), uci);
    check(chess::uci::moveToSan(board, move) == san, san);
    check(board.fullyEquals(before), "SAN preserves complete source board");
    board.makeMove(move);
    check(board.getFen() == after, "expected position after makeMove");
    check(before.getFen() == fen, "independent copy preserves position");
    board.unmakeMove(move);
    check(board.fullyEquals(before), "unmakeMove restores complete state");
}

void terminal(const char* fen, chess::GameResultReason reason) {
    const chess::Board board(fen);
    check(board.isGameOver().first == reason, fen);
}
}

int main() {
    try {
        chess::Board start;
        chess::Movelist moves;
        chess::movegen::legalmoves(moves, start);
        check(moves.size() == 20, "20 initial legal moves");
        check(start.occ().getBits() == 0xffff00000000ffffULL, "occupancy a1=0 / h8=63");
        check(!legal(start, chess::uci::uciToMove(start, "e2e5")), "reject illegal e2e5");
        roundTrip("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4", "e4",
                  "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
        roundTrip("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", "exd5",
                  "4k3/8/8/3P4/8/8/8/4K3 b - - 0 1");
        roundTrip("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "O-O",
                  "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
        roundTrip("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "O-O-O",
                  "r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1");
        roundTrip("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6", "exd6",
                  "4k3/8/3P4/8/8/8/8/4K3 b - - 0 1");
        const char* promotions[] = {"a7a8q", "a7a8r", "a7a8b", "a7a8n"};
        const char* sans[] = {"a8=Q+", "a8=R+", "a8=B", "a8=N"};
        const char* results[] = {"Q3k3/8/8/8/8/8/8/4K3 b - - 0 1", "R3k3/8/8/8/8/8/8/4K3 b - - 0 1",
                                 "B3k3/8/8/8/8/8/8/4K3 b - - 0 1", "N3k3/8/8/8/8/8/8/4K3 b - - 0 1"};
        for (int i = 0; i < 4; ++i) roundTrip("4k3/P7/8/8/8/8/8/4K3 w - - 0 1", promotions[i], sans[i], results[i]);
        terminal("7k/6Q1/5K2/8/8/8/8/8 b - - 0 1", chess::GameResultReason::CHECKMATE);
        terminal("7k/5K2/6Q1/8/8/8/8/8 b - - 0 1", chess::GameResultReason::STALEMATE);
        terminal("4k3/8/8/8/8/8/8/4K3 w - - 0 1", chess::GameResultReason::INSUFFICIENT_MATERIAL);
        terminal("4k3/8/8/8/8/8/8/R3K3 w - - 100 51", chess::GameResultReason::FIFTY_MOVE_RULE);
        for (const auto* uci : {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"}) {
            const auto move = chess::uci::uciToMove(start, uci);
            if (!legal(start, move)) throw std::runtime_error("invalid repetition test sequence");
            start.makeMove(move);
        }
        check(start.isGameOver().first == chess::GameResultReason::THREEFOLD_REPETITION, "threefold repetition with history");
        std::cout << "PASS_TOTAL " << passed << '\n';
        std::cout << "SIZEOF_BOARD " << sizeof(chess::Board) << " SIZEOF_MOVELIST " << sizeof(chess::Movelist) << '\n';
        std::cout << "SLIDING_ATTACK_TABLE_BYTES " << ((0x19000ULL + 0x1480ULL) * sizeof(chess::Bitboard)) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << passed << " checks: " << error.what() << '\n';
        return 1;
    }
}
