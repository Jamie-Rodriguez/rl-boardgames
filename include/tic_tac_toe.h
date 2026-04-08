#ifndef TIC_TAC_TOE_H
#define TIC_TAC_TOE_H

#include "board_game.h"

#define TTT_NUM_PLAYERS 2
#define TTT_NUM_ROWS    3
#define TTT_NUM_COLS    3
#define TTT_BOARD_SIZE  (TTT_NUM_ROWS * TTT_NUM_COLS)

#define TTT_STATE_SIZE (1 + TTT_NUM_PLAYERS)
// Action: Which square to place piece on (0-8 for 3×3 grid)
#define TTT_MAX_NUM_ACTIONS TTT_BOARD_SIZE

/**
 * Note `observation` shape is the same as the `features` shape for this
 * tic-tac-toe implementation
 */
#define TTT_OBS_NDIMS      3
#define TTT_OBS_SIZE       (TTT_NUM_PLAYERS * TTT_NUM_ROWS * TTT_NUM_COLS)
#define TTT_FEATURES_NDIMS TTT_OBS_NDIMS
#define TTT_FEATURES_SIZE  TTT_OBS_SIZE

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
