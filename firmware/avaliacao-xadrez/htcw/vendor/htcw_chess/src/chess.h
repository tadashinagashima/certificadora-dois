// A simple chess engine in C
// copyright (c) 2025 by honey the codewitch
// MIT license
#ifndef CHESS_H
#define CHESS_H
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef signed char chess_value_t;

/// @brief The type of chess piece
typedef enum {
    CHESS_PAWN = 0,
    CHESS_BISHOP = 1,
    CHESS_ROOK = 2,
    CHESS_KNIGHT = 3,
    CHESS_QUEEN = 4,
    CHESS_KING = 5
} chess_type_t;

/// @brief The board status
typedef enum {
    /// @brief Normal play state
    CHESS_NORMAL = 0,
    /// @brief A king is in check
    CHESS_CHECK = 1,
    /// @brief A king is in checkmate
    CHESS_CHECKMATE  = 2,
    /// @brief The game is a stalemate
    CHESS_STALEMATE = 3
} chess_status_t;

/// @brief A result code
typedef enum {
    /// @brief An invalid argument was passed
    CHESS_INVALID = -1,
    /// @brief The operation completed successfully
    CHESS_SUCCESS = 0
} chess_result_t;

/// @brief The chess team
typedef enum {
    CHESS_WHITE = 0,
    CHESS_BLACK = 1
} chess_team_t;

/// @brief An index into the chess board
typedef chess_value_t chess_index_t;
/// @brief The id (team and type) of a piece
typedef chess_value_t chess_id_t;
/// @brief A chess score value
typedef unsigned int chess_score_t;

/// @brief The state for the chess game (effectively private)
typedef struct {
    /// @brief The board, each containing an id
    chess_id_t board[64];
    /// @brief The location of each king
    chess_index_t kings[2];
    /// @brief Targets for possible en passant captures
    chess_index_t en_passant_targets[16];
    /// @brief Which turn it is
    chess_team_t turn;
    /// @brief Indicates that no castling can take place
    bool no_castle[2];
    /// @brief Indicates the current scores
    chess_score_t score[2];
} chess_game_t;

/// @brief Initializes a new chess game
/// @param out_game The structure holding the game
void chess_init(chess_game_t* out_game);
/// @brief Moves a piece from one position to another
/// @param game the chess game  
/// @param index_from The index to move from
/// @param index_to The index to move to.
/// @return The index of the capture victim if successful. -1/CHESS_NONE if no capture. -2 on illegal move or invalid arguments
chess_index_t chess_move(chess_game_t* game, chess_index_t index_from, chess_index_t index_to);
/// @brief Computes the available moves for a specified piece on the board
/// @param game The chess game
/// @param index The index on the board for the piece to compute
/// @param out_moves The moves array to write to (should be at least 64 length)
/// @return The count of moves written
size_t chess_compute_moves(const chess_game_t* game, chess_index_t index, chess_index_t* out_moves);
/// @brief Indicates whether an array of move destinations contains the specified index
/// @param moves The moves array
/// @param moves_size The size of the moves array
/// @param index The index to compare
/// @return true if the move was present, otherwise false
bool chess_contains_move(const chess_index_t* moves, size_t moves_size, chess_index_t index);
/// @brief Promotes a pawn that has reached the end of the board
/// @param game The chess game
/// @param index The index of the pawn
/// @param new_type The new chess piece type
/// @return CHESS_SUCCESS if the promotion was successful, otherwise CHESS_INVALID
chess_result_t chess_promote_pawn(chess_game_t* game, chess_index_t index, chess_type_t new_type);
/// @brief Indicates the status of the game
/// @param game The game
/// @param out_white_status The white status
/// @param out_black_status The black status
/// @return True if the game can continue, otherwise false
bool chess_status(const chess_game_t* game,  chess_status_t* out_white_status, chess_status_t* out_black_status);
/// @brief Indicates which player's turn it is
/// @param game The game
/// @return a chess_team_t indicating the team that is up
chess_team_t chess_turn(const chess_game_t* game);
/// @brief Gets the piece id at the board index
/// @param game The game
/// @param index The index of the piece to retrieve
/// @return the id or -1 if invalid or no piece present
chess_id_t chess_index_to_id(const chess_game_t* game, chess_index_t index);
/// @brief Returns the canonical name for the board index, such as "b7"
/// @param index The board index
/// @param out_buffer A string buffer of at least 3 characters
/// @return CHESS_SUCCESS if the operation was successful, otherwise CHESS_INVALID if invalid argument
chess_result_t chess_index_name(chess_index_t index, char* out_buffer);
/// @brief Indicates the score of a given team
/// @param game The game
/// @param team The team to return the score for
/// @return The score for the team
chess_score_t chess_score(const chess_game_t* game, chess_team_t team);

/// @brief Indicates whether or not a team's king can castle
/// @param game The game
/// @param team The team to return the castle status for
/// @return True if the team's king can castle, otherwise false
bool chess_can_castle(const chess_game_t* game, chess_team_t team);
#ifdef __cplusplus
}
#endif

// Retrieves the chess team from a chess id
#define CHESS_TEAM(id) ((chess_team_t)(!!(((chess_value_t)id) & (1 << 3))))
// Retrieves the chess piece type from a chess id
#define CHESS_TYPE(id) ((chess_type_t)(id & 7))
// Crates a chess id from a chess team and piece type
#define CHESS_ID(team, type) (((chess_value_t)(team) ? (1 << 3) : (0 << 3)) | (int)type)
// alias for a non-value
#define CHESS_NONE ((chess_value_t)-1)

#endif // CHESS_H