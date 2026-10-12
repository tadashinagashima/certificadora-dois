#include "chess.h"

#include <memory.h>
#include <stdio.h>
#ifndef NULL
#define NULL 0
#endif
static chess_value_t scoring[] = {
    1,
    3,
    5,
    3,
    9,
    200
};

static void move_until_obstacle(chess_value_t (*index_fn)(chess_value_t team, chess_value_t index), chess_value_t team, chess_value_t index, const chess_value_t* game_board, chess_value_t* out_moves, chess_value_t* out_size) {
    chess_value_t i = index;
    while (1) {
        i = index_fn(team, i);
        if (i != CHESS_NONE) {
            if (game_board[i] != CHESS_NONE) {
                if (CHESS_TEAM(game_board[i]) == team) {
                    return;
                }
                out_moves[(*out_size)++] = i;
                return;
            } else {
                out_moves[(*out_size)++] = i;
            }
        } else {
            break;
        }
    }
}

// With canonical indexing:
// White is at bottom (indices 0-15), moves "up" the board (increasing indices, +8)
// Black is at top (indices 48-63), moves "down" the board (decreasing indices, -8)
static chess_value_t index_advance(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    if (team == CHESS_WHITE) {  // white moves up (increasing rank)
        index += 8;
    } else if (team == CHESS_BLACK) {  // black moves down (decreasing rank)
        index -= 8;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_advance_left(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    const chess_value_t x = index % 8;
    if (team == CHESS_WHITE) {  // white advances up-left
        if (x == 0 || index + 7 > 63) {
            return CHESS_NONE;
        }
        index += 7;
    } else if (team == CHESS_BLACK) {  // black advances down-left (from black's view)
        if (x == 0 || index - 9 < 0) {
            return CHESS_NONE;
        }
        index -= 9;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_advance_right(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    const chess_value_t x = index % 8;
    if (team == CHESS_WHITE) {  // white advances up-right
        if (x == 7 || index + 9 > 63) {
            return CHESS_NONE;
        }
        index += 9;
    } else if (team == CHESS_BLACK) {  // black advances down-right (from black's view)
        if (x == 7 || index - 7 < 0) {
            return CHESS_NONE;
        }
        index -= 7;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_retreat_left(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    const chess_value_t x = index % 8;
    if (team == CHESS_WHITE) {  // white retreats down-left
        if (x == 0 || index - 9 < 0) {
            return CHESS_NONE;
        }
        index -= 9;
    } else if (team == CHESS_BLACK) {  // black retreats up-left (from black's view)
        if (x == 0 || index + 7 > 63) {
            return CHESS_NONE;
        }
        index += 7;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_retreat_right(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    const chess_value_t x = index % 8;
    if (team == CHESS_WHITE) {  // white retreats down-right
        if (x == 7 || index - 7 < 0) {
            return CHESS_NONE;
        }
        index -= 7;
    } else if (team == CHESS_BLACK) {  // black retreats up-right (from black's view)
        if (x == 7 || index + 9 > 63) {
            return CHESS_NONE;
        }
        index += 9;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_retreat(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    if (team == CHESS_WHITE) {  // white retreats down (decreasing rank)
        index -= 8;
    } else if (team == CHESS_BLACK) {  // black retreats up (increasing rank)
        index += 8;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_left(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    const chess_value_t x = index % 8;
    if (team == CHESS_WHITE) {  // white's left is decreasing file
        if (x == 0) {
            return CHESS_NONE;
        }
        index -= 1;
    } else if (team == CHESS_BLACK) {  // black's left is increasing file (from black's perspective)
        if (x == 7) {
            return CHESS_NONE;
        }
        index += 1;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static chess_value_t index_right(chess_value_t team, chess_value_t index) {
    if (index < 0) return CHESS_NONE;
    const chess_value_t x = index % 8;
    if (team == CHESS_WHITE) {  // white's right is increasing file
        if (x == 7) {
            return CHESS_NONE;
        }
        index += 1;
    } else if (team == CHESS_BLACK) {  // black's right is decreasing file (from black's perspective)
        if (x == 0) {
            return CHESS_NONE;
        }
        index -= 1;
    }
    if (index < 0 || index > 63) return CHESS_NONE;
    return index;
}

static void add_en_passant_target(chess_game_t* game, chess_value_t index) {
    for (int i = 0; i < 16; ++i) {
        if (game->en_passant_targets[i] == CHESS_NONE) {
            game->en_passant_targets[i] = index;
            return;
        }
    }
}

static void clear_en_passant_target(chess_game_t* game, chess_value_t index) {
    for (int i = 0; i < 16; ++i) {
        if (game->en_passant_targets[i] == index) {
            game->en_passant_targets[i] = CHESS_NONE;
            return;
        }
    }
}

static chess_value_t is_en_passant_target(const chess_game_t* game, chess_value_t index) {
    for (int i = 0; i < 16; ++i) {
        if (game->en_passant_targets[i] == index) {
            return 1;
        }
    }
    return 0;
}

static chess_value_t en_passant_target_from_move(const chess_game_t* game, chess_value_t index_from, chess_value_t index_to, const chess_value_t* game_board) {
    if (game == NULL || index_from < 0 || index_from > 63 || index_to < 0 || index_to > 63 || game_board[index_from] == CHESS_NONE) {
        return CHESS_NONE;
    }
    const chess_value_t id = game_board[index_from];
    const chess_value_t team = CHESS_TEAM(id);
    chess_value_t tmp = index_advance_left(team, index_from);
    if (tmp != index_to) {
        tmp = index_advance_right(team, index_from);
        if (tmp != index_to) {
            return CHESS_NONE;
        }
    }
    tmp = index_retreat(team, tmp);
    if (tmp == CHESS_NONE) {
        return CHESS_NONE;
    }  // shouldn't happen
    const chess_value_t id_cmp = game_board[tmp];
    if (id_cmp == CHESS_NONE) return CHESS_NONE;
    const chess_type_t type_cmp = CHESS_TYPE(id_cmp);
    const chess_value_t team_cmp = CHESS_TEAM(id_cmp);
    if (type_cmp != CHESS_PAWN || team_cmp == team) {
        return CHESS_NONE;
    }
    return is_en_passant_target(game, tmp) ? tmp : CHESS_NONE;
}

static size_t compute_moves(const chess_game_t* game, chess_index_t index, chess_index_t* out_moves, const chess_id_t* game_board) {
    const chess_value_t id = game_board[index];
    if (id == CHESS_NONE) {
        return 0;
    }
    const chess_value_t type = CHESS_TYPE(id);
    const chess_value_t team = CHESS_TEAM(id);
    chess_value_t result = 0;
    switch (type) {
        case CHESS_PAWN:
            // White pawns start at indices 8-15 (rank 2), black pawns start at indices 48-55 (rank 7)
            if ((team == CHESS_WHITE && index >= 8 && index < 16) || (team == CHESS_BLACK && index >= 48 && index < 56)) {  // the pawn is on its first move
                chess_value_t tmp = index_advance(team, index);
                if (tmp != CHESS_NONE) {
                    if (game_board[tmp] == CHESS_NONE) {
                        out_moves[result++] = tmp;
                    }
                    chess_value_t attack = index_advance_left(team, index);
                    if (attack != CHESS_NONE && game_board[attack] != CHESS_NONE && CHESS_TEAM(game_board[attack]) != team) {
                        out_moves[result++] = attack;
                    }
                    attack = index_advance_right(team, index);
                    if (attack != CHESS_NONE && game_board[attack] != CHESS_NONE && CHESS_TEAM(game_board[attack]) != team) {
                        out_moves[result++] = attack;
                    }
                    // en passant
                    attack = en_passant_target_from_move(game, index, index_advance_left(team, index), game_board);
                    if (attack != CHESS_NONE) {
                        out_moves[result++] = index_advance_left(team, index);
                    }
                    attack = en_passant_target_from_move(game, index, index_advance_right(team, index), game_board);
                    if (attack != CHESS_NONE) {
                        out_moves[result++] = index_advance_right(team, index);
                    }
                    if (game_board[tmp] == CHESS_NONE) {
                        tmp = index_advance(team, tmp);
                        if (tmp != CHESS_NONE && game_board[tmp] == CHESS_NONE) {
                            out_moves[result++] = tmp;
                        }
                    }
                }
            } else {
                chess_value_t tmp = index_advance(team, index);
                if (tmp != CHESS_NONE) {
                    if (game_board[tmp] == CHESS_NONE) {
                        out_moves[result++] = tmp;
                    }
                    chess_value_t attack = index_advance_left(team, index);
                    if (attack != CHESS_NONE && game_board[attack] != CHESS_NONE && CHESS_TEAM(game_board[attack]) != team) {
                        out_moves[result++] = attack;
                    }
                    attack = index_advance_right(team, index);
                    if (attack != CHESS_NONE && game_board[attack] != CHESS_NONE && CHESS_TEAM(game_board[attack]) != team) {
                        out_moves[result++] = attack;
                    }
                    // en passant
                    attack = en_passant_target_from_move(game, index, index_advance_left(team, index), game_board);
                    if (attack != CHESS_NONE) {
                        out_moves[result++] = index_advance_left(team, index);
                    }
                    attack = en_passant_target_from_move(game, index, index_advance_right(team, index), game_board);
                    if (attack != CHESS_NONE) {
                        out_moves[result++] = index_advance_right(team, index);
                    }
                }
            }
            break;
        case CHESS_BISHOP:
            move_until_obstacle(index_advance_left, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_advance_right, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_retreat_left, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_retreat_right, team, index, game_board, out_moves, &result);
            break;
        case CHESS_ROOK:
            move_until_obstacle(index_advance, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_retreat, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_left, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_right, team, index, game_board, out_moves, &result);
            break;
        case CHESS_KNIGHT: {
            chess_value_t tmp = index_advance(team, index);
            if (tmp != CHESS_NONE) {
                tmp = index_advance(team, tmp);
                if (tmp != CHESS_NONE) {
                    chess_value_t tmp2 = index_left(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                    tmp2 = index_right(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                }
            }
            tmp = index_retreat(team, index);
            if (tmp != CHESS_NONE) {
                tmp = index_retreat(team, tmp);
                if (tmp != CHESS_NONE) {
                    chess_value_t tmp2 = index_left(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                    tmp2 = index_right(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                }
            }
            tmp = index_left(team, index);
            if (tmp != CHESS_NONE) {
                tmp = index_left(team, tmp);
                if (tmp != CHESS_NONE) {
                    chess_value_t tmp2 = index_advance(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                    tmp2 = index_retreat(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                }
            }
            tmp = index_right(team, index);
            if (tmp != CHESS_NONE) {
                tmp = index_right(team, tmp);
                if (tmp != CHESS_NONE) {
                    chess_value_t tmp2 = index_advance(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                    tmp2 = index_retreat(team, tmp);
                    if (tmp2 != CHESS_NONE) {
                        if (game_board[tmp2] == CHESS_NONE || CHESS_TEAM(game_board[tmp2]) != team) {
                            out_moves[result++] = tmp2;
                        }
                    }
                }
            }
        } break;
        case CHESS_QUEEN:
            move_until_obstacle(index_advance_left, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_advance_right, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_retreat_left, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_retreat_right, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_advance, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_retreat, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_left, team, index, game_board, out_moves, &result);
            move_until_obstacle(index_right, team, index, game_board, out_moves, &result);
            break;
        case CHESS_KING: {
            chess_value_t tmp = index_advance(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_retreat(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_left(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_right(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_advance_left(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_advance_right(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_retreat_left(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
            tmp = index_retreat_right(team, index);
            if (tmp != CHESS_NONE) {
                if (game_board[tmp] == CHESS_NONE || CHESS_TEAM(game_board[tmp]) != team) {
                    out_moves[result++] = tmp;
                }
            }
        } break;
        default:
            return 0;
    }
    return result;
}

bool chess_contains_move(const chess_index_t* moves, size_t moves_size, chess_index_t index) {
    for (int i = 0; i < moves_size; ++i) {
        if (moves[i] == index) return true;
    }
    return false;
}

static chess_value_t is_checked_king(const chess_game_t* game, chess_value_t king_index, const chess_value_t* game_board) {
    const chess_value_t id = game_board[king_index];
    const chess_value_t team = CHESS_TEAM(id);
    chess_value_t tmp_moves[64];
    chess_value_t tmp_moves_size;
    for (int i = 0; i < 64; ++i) {
        const chess_value_t tmp_id = game_board[i];
        if (tmp_id != CHESS_NONE) {
            const chess_value_t tmp_team = CHESS_TEAM(tmp_id);
            if (tmp_team != team) {
                tmp_moves_size = compute_moves(game, i, tmp_moves, game_board);
                if (chess_contains_move(tmp_moves, tmp_moves_size, king_index)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static chess_value_t compute_check_moves(const chess_game_t* game, chess_value_t index, chess_value_t king_index, const chess_value_t* game_board, chess_value_t* out_moves) {
    const chess_value_t id = game_board[index];
    const chess_value_t king_id = game_board[king_index];
    chess_value_t result = 0;
    if (king_id == CHESS_NONE || CHESS_TYPE(king_id) != CHESS_KING) {
        return 0;  // shouldn't happen
    }
    if (id == CHESS_NONE) {
        return 0;
    }
    const chess_value_t team = CHESS_TEAM(id);
    if (team != CHESS_TEAM(king_id)) {
        return 0;
    }
    chess_value_t tmp_moves[64];
    chess_value_t tmp_moves_size;
    chess_value_t tmp_board[64];
    memcpy(tmp_board, game_board, sizeof(tmp_board));
    tmp_moves_size = compute_moves(game, index, tmp_moves, game_board);
    for (int i = 0; i < tmp_moves_size; ++i) {
        const chess_value_t to_index = tmp_moves[i];
        // commit to temp
        const chess_value_t victim = tmp_board[to_index];
        tmp_board[to_index] = tmp_board[index];
        tmp_board[index] = CHESS_NONE;
        chess_value_t test_king = king_index;
        if (index == king_index) {
            test_king = to_index;
        }
        const char checked = is_checked_king(game, test_king, tmp_board);
        // rollback
        tmp_board[index] = tmp_board[to_index];
        tmp_board[to_index] = victim;
        if (!checked) {
            out_moves[result++] = to_index;
        }
    }
    return result;
}

static void eliminate_checked_moves(const chess_game_t* game, chess_value_t index, chess_value_t* in_out_moves, chess_value_t* in_out_moves_size) {
    const chess_value_t id = game->board[index];
    const chess_value_t team = CHESS_TEAM(id);
    const chess_value_t king_index = game->kings[team];
    chess_value_t tmp_board[64];
    memcpy(tmp_board, game->board, sizeof(tmp_board));
    for (int i = 0; i < *in_out_moves_size; ++i) {
        const chess_value_t to_index = in_out_moves[i];
        // commit to temp
        const chess_value_t victim = tmp_board[to_index];
        tmp_board[to_index] = tmp_board[index];
        tmp_board[index] = CHESS_NONE;
        chess_value_t test_king = king_index;
        if (index == king_index) {
            test_king = to_index;
        }
        const char checked = is_checked_king(game, test_king, tmp_board);
        // rollback
        tmp_board[index] = tmp_board[to_index];
        tmp_board[to_index] = victim;
        if (checked) {
            if (i < *in_out_moves_size - 1) {
                for (int j = i + 1; j < *in_out_moves_size; ++j) {
                    in_out_moves[j - 1] = in_out_moves[j];
                }
                --(*in_out_moves_size);
                --i;
            }
        }
    }
}

void chess_init(chess_game_t* out_game) {
    out_game->turn = 0;
    out_game->score[0] = 0;
    out_game->score[1] = 0;
    out_game->no_castle[0] = 0;
    out_game->no_castle[1] = 0;
    
    for (int i = 0; i < 16; ++i) {
        out_game->en_passant_targets[i] = CHESS_NONE;
    }
    for (int i = 0; i < 64; ++i) {
        out_game->board[i] = CHESS_NONE;
    }
    
    // Canonical chess layout: White at bottom (rank 1-2), Black at top (rank 7-8)
    // Rank 2 - White pawns (indices 8CHESS_NONE5)
    for (int i = 0; i < 8; ++i) {
        out_game->board[8 + i] = CHESS_ID(0, CHESS_PAWN);
    }
    // Rank 1 - White pieces (indices 0-7)
    out_game->board[0] = CHESS_ID(0, CHESS_ROOK);      // a1
    out_game->board[1] = CHESS_ID(0, CHESS_KNIGHT);    // b1
    out_game->board[2] = CHESS_ID(0, CHESS_BISHOP);    // c1
    out_game->board[3] = CHESS_ID(0, CHESS_QUEEN);     // d1
    out_game->board[4] = CHESS_ID(0, CHESS_KING);      // e1
    out_game->board[5] = CHESS_ID(0, CHESS_BISHOP);    // f1
    out_game->board[6] = CHESS_ID(0, CHESS_KNIGHT);    // g1
    out_game->board[7] = CHESS_ID(0, CHESS_ROOK);      // h1
    out_game->kings[0] = 4;  // e1

    // Rank 7 - Black pawns (indices 48-55)
    for (int i = 0; i < 8; ++i) {
        out_game->board[48 + i] = CHESS_ID(1, CHESS_PAWN);
    }
    // Rank 8 - Black pieces (indices 56-63)
    out_game->board[56] = CHESS_ID(1, CHESS_ROOK);     // a8
    out_game->board[57] = CHESS_ID(1, CHESS_KNIGHT);   // b8
    out_game->board[58] = CHESS_ID(1, CHESS_BISHOP);   // c8
    out_game->board[59] = CHESS_ID(1, CHESS_QUEEN);    // d8
    out_game->board[60] = CHESS_ID(1, CHESS_KING);     // e8
    out_game->board[61] = CHESS_ID(1, CHESS_BISHOP);   // f8
    out_game->board[62] = CHESS_ID(1, CHESS_KNIGHT);   // g8
    out_game->board[63] = CHESS_ID(1, CHESS_ROOK);     // h8
    out_game->kings[1] = 60;  // e8
}

static chess_value_t compute_castling(const chess_game_t* game, chess_value_t index, chess_value_t queen_side) {
    // do not call if if not your turn.
    const chess_value_t id = game->board[index];
    const chess_type_t type = CHESS_TYPE(id);
    const chess_value_t team = CHESS_TEAM(id);
    chess_value_t index_other = CHESS_NONE;
    if (type != CHESS_KING && type != CHESS_ROOK) {
        return CHESS_NONE;
    }

    if (game->no_castle[team]) {
        return CHESS_NONE;
    }
    
    if (team == CHESS_WHITE) {  // White castling (rank 1, indices 0-7)
        if (type == CHESS_KING) {
            if (queen_side) {
                index_other = 0;  // Queen-side rook at a1
                // no pieces between king and rook?
                for (int i = index_other + 1; i < index; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            } else {
                index_other = 7;  // King-side rook at h1
                // no pieces between king and rook?
                for (int i = index + 1; i < index_other; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            }
        } else {
            queen_side = index == 0;  // Queen-side rook
            index_other = 4;  // White king at e1
            if (queen_side) {
                for (int i = index + 1; i < index_other; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            } else {
                for (int i = index_other + 1; i < index; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            }
        }
    } else {  // Black castling (rank 8, indices 56-63)
        if (type == CHESS_KING) {
            if (queen_side) {
                index_other = 56;  // Queen-side rook at a8
                // no pieces between king and rook?
                for (int i = index_other + 1; i < index; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            } else {
                index_other = 63;  // King-side rook at h8
                // no pieces between king and rook?
                for (int i = index + 1; i < index_other; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            }
        } else {
            queen_side = index == 56;  // Queen-side rook
            index_other = 60;  // Black king at e8
            if (queen_side) {
                for (int i = index + 1; i < index_other; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            } else {
                for (int i = index_other + 1; i < index; ++i) {
                    if (game->board[i] != CHESS_NONE) {
                        return CHESS_NONE;
                    }
                }
            }
        }
    }
    
    // now we have to see if a piece is attacking any square between this one and the other index, inclusive
    chess_value_t tmp_moves[64];
    chess_value_t tmp_moves_size;
    if (index_other > index) {
        for (int i = index; i <= index_other; ++i) {
            for (int j = 0; j < 64; ++j) {
                const chess_value_t cmp_id = game->board[j];
                if (CHESS_NONE != cmp_id && CHESS_TEAM(cmp_id) != team) {
                    // opposing piece
                    tmp_moves_size = compute_moves(game, j, tmp_moves, game->board);
                    if (chess_contains_move(tmp_moves, tmp_moves_size, i)) {
                        return CHESS_NONE;
                    }
                }
            }
        }
    } else {
        for (int i = index_other; i <= index; ++i) {
            for (int j = 0; j < 64; ++j) {
                const chess_value_t cmp_id = game->board[j];
                if (CHESS_NONE != cmp_id && CHESS_TEAM(cmp_id) != team) {
                    // opposing piece
                    tmp_moves_size = compute_moves(game, j, tmp_moves, game->board);
                    if (chess_contains_move(tmp_moves, tmp_moves_size, i)) {
                        return CHESS_NONE;
                    }
                }
            }
        }
    }

    return index_other;
}
chess_value_t chess_move(chess_game_t* game, chess_index_t index_from, chess_index_t index_to) {
    if (game == NULL || index_from < 0 || index_from > 63 || index_to < 0 || index_to > 63 || index_from == index_to) {
        return -2;
    }

    const chess_value_t id = game->board[index_from];
    const chess_type_t type = CHESS_TYPE(id);
    const chess_value_t team = CHESS_TEAM(id);
    if (game->turn != team) {
        return -2;
    }
    chess_value_t tmp_moves[64];
    chess_value_t tmp_moves_size = 0;
    chess_value_t index_other = CHESS_NONE;
    const chess_value_t king_index = game->kings[team];
    if (is_checked_king(game, king_index, game->board)) {
        tmp_moves_size = compute_check_moves(game, index_from, king_index, game->board, tmp_moves);
    } else {
        tmp_moves_size = compute_moves(game, index_from, tmp_moves, game->board);
        // castle if possible
        index_other = compute_castling(game, index_from, 0);
        if (index_other != index_to) {
            index_other = compute_castling(game, index_from, 1);
        }
        if (index_other == index_to) {
            game->no_castle[team] = 1;
            const chess_value_t other_id = game->board[index_other];
            game->board[index_to] = game->board[index_from];
            game->board[index_from] = other_id;
            if (type == CHESS_KING) {
                game->kings[team] = index_to;
                
            }
            // still our turn
            return CHESS_NONE;
        }
    }
    if (chess_contains_move(tmp_moves, tmp_moves_size, index_to)) {
        char added = 0;
        chess_value_t result = CHESS_NONE;
        chess_score_t score = 0;
        if (type == CHESS_PAWN) {
            clear_en_passant_target(game, index_from);
            // check for en passant target eligibility.
            // we can tell if it's the first move advanced by two
            // simply by checking the index for a difference of 16
            // the only way it gets that is moving two and the only
            // time that can happen is first move
            if (team == CHESS_WHITE) {
                if (index_from == index_to - 16) {  // White moved up two squares
                    add_en_passant_target(game, index_to);
                    added = 1;
                }
            } else if (team == CHESS_BLACK) {
                if (index_from == index_to + 16) {  // Black moved down two squares
                    add_en_passant_target(game, index_to);
                    added = 1;
                }
            }
            const chess_value_t attack_index = en_passant_target_from_move(game, index_from, index_to, game->board);
            if (attack_index != CHESS_NONE) {
                chess_id_t target_id = CHESS_TYPE(game->board[attack_index]);
                score = scoring[target_id]; 
                game->board[attack_index] = CHESS_NONE;
                result = attack_index;
                if(target_id==CHESS_KING) {
                    game->kings[1-team] = CHESS_NONE;
                }
            }
        }
        if (game->board[index_to] != CHESS_NONE && CHESS_TEAM(game->board[index_to]) != team) {
            result = index_to;
        }
        if(result!=CHESS_NONE) {
            if(score==0 && game->board[result]!=CHESS_NONE) {
                score = scoring[CHESS_TYPE(game->board[result])];
            }
            chess_id_t target_id = CHESS_TYPE(game->board[result]);
            game->score[team] += score;
        }
        game->board[index_to] = game->board[index_from];
        if (!added && game->board[index_to] != CHESS_NONE) {
            clear_en_passant_target(game, index_to);
        }
        if (++game->turn > 1) {
            game->turn = 0;
        }
        if (CHESS_TYPE(id) == CHESS_KING) {
            game->kings[team] = index_to;
        }
        game->board[index_from] = CHESS_NONE;
        return result;
    }
    return -2;
}

size_t chess_compute_moves(const chess_game_t* game, chess_index_t index, chess_index_t* out_moves) {
    if (game == NULL || index < 0 || index > 63) {
        return 0;
    }
    const chess_value_t id = game->board[index];
    if (game->turn != CHESS_TEAM(id)) {
        return 0;
    }
    chess_value_t result = 0;
    const chess_value_t king_index = game->kings[CHESS_TEAM(id)];
    if (is_checked_king(game, king_index, game->board)) {
        result = compute_check_moves(game, index, king_index, game->board, out_moves);
    } else {
        result = compute_moves(game, index, out_moves, game->board);
        eliminate_checked_moves(game, index, out_moves, &result);
        chess_value_t index_other = compute_castling(game, index, 0);
        if (index_other != CHESS_NONE) {
            out_moves[result++] = index_other;
        }
        index_other = compute_castling(game, index, 1);
        if (index_other != CHESS_NONE) {
            out_moves[result++] = index_other;
        }
    }
    return result;
}

chess_team_t chess_turn(const chess_game_t* game) {
    return game->turn;
}

chess_id_t chess_index_to_id(const chess_game_t* game, chess_index_t index) {
    if (game == NULL || index < 0 || index > 63) {
        return CHESS_NONE;
    }
    return game->board[index];
}

bool chess_status(const chess_game_t* game, chess_status_t* out_white_status, chess_status_t* out_black_status) {
    if(game==NULL) return false;
    chess_value_t index = game->kings[(int)CHESS_WHITE];
    if(index==CHESS_NONE) {
        return CHESS_CHECKMATE;
    }
    index = game->kings[(int)CHESS_BLACK];
    if(index==CHESS_NONE) {
        return CHESS_CHECKMATE;
    }
    if(out_white_status!=NULL) {
        *out_white_status = CHESS_NORMAL;
    }
    if(out_black_status!=NULL) {
        *out_black_status = CHESS_NORMAL;
    }
    chess_value_t moves[64]; 
    bool has_move = false;
    bool set_white = false;
    bool set_black = false;
    bool can_continue =true;
    for(int i = 0;i<64;++i) {
        if(game->board[i]!=CHESS_NONE) {
            if(CHESS_KING==CHESS_TYPE(game->board[i])) {
                if (is_checked_king(game, i, game->board)) {
                    if (0 == chess_compute_moves(game, i, moves)) {
                        if(CHESS_TEAM(game->board[i]==CHESS_WHITE)) {
                            if(out_white_status!=NULL) {
                                *out_white_status = CHESS_CHECKMATE;
                            }
                            can_continue = false;
                            set_white = true;
                        } else {
                            if(out_black_status!=NULL) {
                                *out_black_status = CHESS_CHECKMATE;
                            }
                            can_continue = false;
                            set_black = true;
                        }
                    } else {
                        if(CHESS_TEAM(game->board[i]==CHESS_WHITE)) {
                            if(out_white_status!=NULL) {
                                *out_white_status = CHESS_CHECK;
                            }
                            set_white = true;
                        } else {
                            if(out_black_status!=NULL) {
                                *out_black_status = CHESS_CHECK;
                            }
                            set_black = true;
                        }
                    }
                }
            }
            if(can_continue && 0!=chess_compute_moves(game,i,moves)) {
                has_move = true;
            }
        }
    }
    
    if(!has_move && !set_white && !set_black) {
        can_continue = false;
        if(out_white_status!=NULL) {
            *out_white_status = CHESS_STALEMATE;
        }
        if(out_black_status!=NULL) {
            *out_black_status = CHESS_STALEMATE;
        }
    }
    return can_continue;
}

chess_result_t chess_promote_pawn(chess_game_t* game, chess_index_t index, chess_type_t new_type) {
    if (game == NULL || index < 0 || index > 63 || new_type == CHESS_PAWN) {
        return CHESS_INVALID;
    }
    const chess_value_t id = game->board[index];
    const chess_value_t team = CHESS_TEAM(id);
    if (CHESS_TYPE(id) != CHESS_PAWN) {
        // printf\("DEBUG: INVALID TYPE. Id: %d, Team: %d, Type: %d, Index: %d\n",(int)id,(int)team,CHESS_TYPE(id), (int)index);
        return CHESS_INVALID;
    }
    // White promotes at rank 8 (indices 56-63), Black promotes at rank 1 (indices 0-7)
    if (team == CHESS_WHITE) {
        if (index < 56) {  // Not at rank 8
            // puts\("DEBUG: INVALID SQUARE BOTTOM");
            return CHESS_INVALID;
        }
    } else {  // BLACK
        if (index >= 8) {  // Not at rank 1
            // puts\("DEBUG: INVALID SQUARE TOP");
            return CHESS_INVALID;
        }
    }
    clear_en_passant_target(game, index);
    game->board[index] = CHESS_ID(team, new_type);
    // puts\("DEBUG: PROMOTE SUCCESS");
    return CHESS_SUCCESS;
}

chess_result_t chess_index_name(chess_index_t index, char* out_buffer) {
    if (index < 0 || index > 63 || out_buffer == NULL) {
        return CHESS_INVALID;
    }
    const chess_value_t x = index % 8;  // file (0-7 maps to a-h)
    const chess_value_t y = (index / 8) + 1;  // rank (0-7 maps to 1-8)
    sprintf(out_buffer, "%c%d", x + 'a', y);
    return CHESS_SUCCESS;
}

chess_score_t chess_score(const chess_game_t* game, chess_team_t team) {
    if(team<0||team>1) {
        return 0;
    }
    return game->score[team];
}
bool chess_can_castle(const chess_game_t* game, chess_team_t team) {
    if(game==NULL || team<0 || team>1) return false;
    return !game->no_castle[team];
}