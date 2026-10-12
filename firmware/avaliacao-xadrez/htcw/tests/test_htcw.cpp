#include <chess.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static bool expect(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    return condition;
}
static chess_index_t square(const char* name) {
    return static_cast<chess_index_t>((name[1] - '1') * 8 + name[0] - 'a');
}
static chess_index_t move(chess_game_t& game, const char* from, const char* to) {
    return chess_move(&game, square(from), square(to));
}
static bool piece(const chess_game_t& game, const char* at, chess_team_t team, chess_type_t type) {
    return chess_index_to_id(&game, square(at)) == CHESS_ID(team, type);
}
static void put(chess_game_t& game, const char* at, chess_team_t team, chess_type_t type) {
    game.board[square(at)] = static_cast<chess_id_t>(CHESS_ID(team, type));
    if (type == CHESS_KING) game.kings[team] = square(at);
}
static chess_game_t empty(chess_team_t turn) {
    // Upstream has no FEN API. Synthetic positions use the published struct.
    chess_game_t game{};
    chess_init(&game);
    for (auto& id : game.board) id = CHESS_NONE;
    game.turn = turn;
    game.no_castle[0] = game.no_castle[1] = true;
    return game;
}
static bool initial_moves() {
    chess_game_t game{};
    chess_init(&game);
    const chess_game_t before = game;
    std::size_t count = 0;
    chess_index_t moves[64];
    for (chess_index_t i = 0; i < 64; ++i) count += chess_compute_moves(&game, i, moves);
    std::printf("sizeof(chess_game_t)=%zu; legal_moves=%zu\n", sizeof(game), count);
    return expect(count == 20, "20 initial moves") &&
           expect(std::memcmp(&before, &game, sizeof(game)) == 0, "query preserves position") &&
           expect(piece(game, "a1", CHESS_WHITE, CHESS_ROOK) && piece(game, "h8", CHESS_BLACK, CHESS_ROOK), "a1=0 h8=63");
}
static bool basic_and_rejection() {
    chess_game_t game{};
    chess_init(&game);
    const chess_game_t before = game;
    return expect(move(game, "e2", "e5") == -2, "reject pawn three-square move") &&
           expect(std::memcmp(&before, &game, sizeof(game)) == 0, "rejection preserves position") &&
           expect(move(game, "e2", "e4") == CHESS_NONE && chess_turn(&game) == CHESS_BLACK, "e4 advances turn");
}
static bool capture() {
    chess_game_t game{};
    chess_init(&game);
    return expect(move(game, "e2", "e4") == CHESS_NONE && move(game, "d7", "d5") == CHESS_NONE, "capture preparation") &&
           expect(move(game, "e4", "d5") == square("d5") && piece(game, "d5", CHESS_WHITE, CHESS_PAWN) && game.board[square("e4")] == CHESS_NONE, "exd5 reports and removes captured pawn");
}
static bool prepare_ep(chess_game_t& game) {
    chess_init(&game);
    return move(game, "e2", "e4") == CHESS_NONE && move(game, "a7", "a6") == CHESS_NONE &&
           move(game, "e4", "e5") == CHESS_NONE && move(game, "d7", "d5") == CHESS_NONE;
}
static bool en_passant() {
    chess_game_t game{};
    return expect(prepare_ep(game), "en passant preparation") &&
           expect(move(game, "e5", "d6") == square("d5") && game.board[square("d5")] == CHESS_NONE && piece(game, "d6", CHESS_WHITE, CHESS_PAWN), "immediate en passant removes d5 pawn");
}
static bool en_passant_expiry() {
    chess_game_t game{};
    if (!expect(prepare_ep(game) && move(game, "g1", "f3") == CHESS_NONE && move(game, "a6", "a5") == CHESS_NONE, "1.e4 a6 2.e5 d5 3.Nf3 a5")) return false;
    const int result = move(game, "e5", "d6");
    std::printf("expired en passant returned %d (expected -2)\n", result);
    return expect(result == -2, "en passant expires after an intervening move");
}
static bool castling() {
    chess_game_t game{};
    chess_init(&game);
    if (!expect(move(game, "e2", "e4") == CHESS_NONE && move(game, "e7", "e5") == CHESS_NONE &&
                move(game, "g1", "f3") == CHESS_NONE && move(game, "b8", "c6") == CHESS_NONE &&
                move(game, "f1", "c4") == CHESS_NONE && move(game, "g8", "f6") == CHESS_NONE,
                "1.e4 e5 2.Nf3 Nc6 3.Bc4 Nf6")) return false;
    // Upstream represents castling as king -> rook, so test that API encoding.
    const int result = move(game, "e1", "h1");
    std::printf("castle result=%d; king=%d (expected 6); e1=%d f1=%d g1=%d h1=%d; turn=%d (expected 1)\n",
                result, game.kings[0], game.board[4], game.board[5], game.board[6], game.board[7], chess_turn(&game));
    return expect(result == CHESS_NONE && piece(game, "g1", CHESS_WHITE, CHESS_KING) &&
                  piece(game, "f1", CHESS_WHITE, CHESS_ROOK) && game.board[4] == CHESS_NONE &&
                  game.board[7] == CHESS_NONE && chess_turn(&game) == CHESS_BLACK,
                  "castling places king g1 and rook f1, then changes turn");
}
static bool castling_through_check() {
    auto game = empty(CHESS_WHITE);
    put(game, "e1", CHESS_WHITE, CHESS_KING); put(game, "h1", CHESS_WHITE, CHESS_ROOK);
    put(game, "a8", CHESS_BLACK, CHESS_KING); put(game, "f8", CHESS_BLACK, CHESS_ROOK);
    game.no_castle[CHESS_WHITE] = false;
    return expect(move(game, "e1", "h1") == -2, "reject castling across attacked f1");
}
static bool pinned_piece() {
    auto game = empty(CHESS_WHITE);
    put(game, "e1", CHESS_WHITE, CHESS_KING); put(game, "e2", CHESS_WHITE, CHESS_ROOK);
    put(game, "a8", CHESS_BLACK, CHESS_KING); put(game, "e8", CHESS_BLACK, CHESS_ROOK);
    const int result = move(game, "e2", "a2");
    std::printf("pinned rook e2-a2 returned %d (expected -2)\n", result);
    return expect(result == -2, "reject exposing own king to rook e8");
}
static bool promotions() {
    for (const auto type : { CHESS_QUEEN, CHESS_ROOK, CHESS_BISHOP, CHESS_KNIGHT }) {
        auto game = empty(CHESS_WHITE);
        put(game, "e1", CHESS_WHITE, CHESS_KING); put(game, "h8", CHESS_BLACK, CHESS_KING);
        put(game, "a7", CHESS_WHITE, CHESS_PAWN);
        if (!expect(move(game, "a7", "a8") == CHESS_NONE && chess_promote_pawn(&game, square("a8"), type) == CHESS_SUCCESS && piece(game, "a8", CHESS_WHITE, type), "promotion selected type applied")) return false;
    }
    return true;
}
static bool checkmate() {
    chess_game_t game{};
    chess_init(&game);
    if (!expect(move(game, "f2", "f3") == CHESS_NONE && move(game, "e7", "e5") == CHESS_NONE &&
                move(game, "g2", "g4") == CHESS_NONE && move(game, "d8", "h4") == CHESS_NONE,
                "Fool's Mate preparation")) return false;
    chess_status_t white{}, black{};
    const bool ongoing = chess_status(&game, &white, &black);
    std::printf("ongoing=%d white=%d black=%d (expected 0,2,0)\n", ongoing, white, black);
    return expect(!ongoing && white == CHESS_CHECKMATE && black == CHESS_NORMAL, "white checkmated after 1.f3 e5 2.g4 Qh4#");
}
static bool stalemate() {
    auto game = empty(CHESS_BLACK);
    put(game, "c6", CHESS_WHITE, CHESS_KING); put(game, "b6", CHESS_WHITE, CHESS_QUEEN);
    put(game, "a8", CHESS_BLACK, CHESS_KING);
    chess_status_t white{}, black{};
    const bool ongoing = chess_status(&game, &white, &black);
    std::printf("ongoing=%d white=%d black=%d (black stalemate expected)\n", ongoing, white, black);
    return expect(!ongoing && black == CHESS_STALEMATE, "black stalemated: Ka8 against Kc6 Qb6");
}
int main(int argc, char** argv) {
    struct Case { const char* name; bool (*run)(); };
    const Case cases[] = {
        {"initial_moves", initial_moves}, {"basic_and_rejection", basic_and_rejection},
        {"capture", capture}, {"en_passant", en_passant}, {"en_passant_expiry", en_passant_expiry},
        {"castling", castling}, {"castling_through_check", castling_through_check},
        {"pinned_piece", pinned_piece}, {"promotions", promotions},
        {"checkmate", checkmate}, {"stalemate", stalemate}
    };
    if (argc != 2) return 2;
    for (const auto& item : cases) if (std::strcmp(item.name, argv[1]) == 0) return item.run() ? 0 : 1;
    return 2;
}
