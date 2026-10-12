#include <chess.hpp>

unsigned int countLegalMoves() {
    chess::Board board;
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    return static_cast<unsigned int>(moves.size());
}
