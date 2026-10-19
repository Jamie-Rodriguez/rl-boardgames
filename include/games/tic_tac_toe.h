#ifndef TIC_TAC_TOE_H
#define TIC_TAC_TOE_H

#include "games/board_game.h"

#define TTT_NUM_PLAYERS 2
#define TTT_NUM_ROWS    3
#define TTT_NUM_COLS    3
#define TTT_BOARD_SIZE  (TTT_NUM_ROWS * TTT_NUM_COLS)

#define TTT_STATE_SIZE (1 + TTT_NUM_PLAYERS)

/**
 * Actions are board squares, addressed row-major with row 0 as the
 * bottom row (matching the bitboard layout and help_prompt()):
 *
 * 	 6 | 7 | 8
 * 	---+---+---
 * 	 3 | 4 | 5
 * 	---+---+---
 * 	 0 | 1 | 2
 */
#define TTT_ACTION(row, col) ((uint64_t)((row) * TTT_NUM_COLS + (col)))

#define TTT_MAX_NUM_DECISION_ACTIONS TTT_BOARD_SIZE
/* Deterministic: every action is a decision (one per empty square) */
#define TTT_MAX_NUM_CHANCE_ACTIONS 0
#define TTT_MAX_NUM_ACTIONS        TTT_MAX_NUM_DECISION_ACTIONS

/* Square IDs are dense, so the action-ID space is the board itself */
#define TTT_ACTION_SPACE_SIZE TTT_MAX_NUM_DECISION_ACTIONS

/* Every turn fills a square */
#define TTT_MAX_TURNS TTT_BOARD_SIZE

/**
 * The classic enumeration result for 3x3 tic-tac-toe: distinct
 * positions reachable in play, terminal positions included (the side
 * to move is implied by the piece counts).
 */
#define TTT_STATE_SPACE_SIZE 5478

/**
 * Observation: two row-major board blocks (observer first). Each square is
 * 0 = empty or 1 = occupied.
 */
#define TTT_OBS_NDIMS 1
#define TTT_OBS_SIZE  (TTT_NUM_PLAYERS * TTT_BOARD_SIZE)

/* Features: two binary occupancy planes, observer first, shape { 2, 3, 3 }. */
#define TTT_FEATURES_NDIMS 3
#define TTT_FEATURES_SIZE  (TTT_NUM_PLAYERS * TTT_BOARD_SIZE)

// Intermediate variables to make calculating `TTT_STRING_BUF_SIZE` easier
#define TURN_STR_MAX 32
#define ROW_STR_LEN  (2 + (TTT_NUM_COLS - 1) * 4)
#define SEP_STR_LEN  (3 + (TTT_NUM_COLS - 1) * 4)
#define TTT_STRING_BUF_SIZE (                \
    TURN_STR_MAX +                           \
    TTT_NUM_ROWS * (ROW_STR_LEN + 1) +       \
    (TTT_NUM_ROWS - 1) * (SEP_STR_LEN + 1) + \
    1                                        \
)

extern const Game tic_tac_toe;

typedef uint16_t ttt_bitboard;

#endif
