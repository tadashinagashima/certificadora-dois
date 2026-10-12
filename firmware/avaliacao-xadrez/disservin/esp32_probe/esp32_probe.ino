#include <chess.hpp>

// Keep the legal-move generator and its tables in the link. No peripheral access.
volatile unsigned int legalMoveCount = 0;

void setup() {
    chess::Board board;
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    legalMoveCount = static_cast<unsigned int>(moves.size());
}

void loop() {}
