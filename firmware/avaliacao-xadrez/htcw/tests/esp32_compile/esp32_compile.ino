#include <Arduino.h>
#include <chess.h>

chess_game_t game;
chess_index_t moves[64];
volatile size_t moveCount;
volatile int evaluationResult;

void setup() {
    Serial.begin(115200);
    chess_init(&game);
    moveCount = chess_compute_moves(&game, 12, moves);
    evaluationResult = chess_move(&game, 12, 28);
    chess_status_t white, black;
    evaluationResult += chess_status(&game, &white, &black);
    evaluationResult += chess_promote_pawn(&game, 28, CHESS_QUEEN);
    Serial.printf("htcw_chess 0.5.3 compile probe: game=%u bytes; moves=%u; result=%d\n",
                  static_cast<unsigned>(sizeof(game)), static_cast<unsigned>(moveCount), evaluationResult);
}
void loop() {}
