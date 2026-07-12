#ifndef CONNECT4_H
#define CONNECT4_H

#include "board_game.h"

#define C4_NUM_PLAYERS 2
#define C4_NUM_ROWS    6
#define C4_NUM_COLS    7

/**
 * State layout:
 * 	[0]: current player
 * 	[1]: player 0 bitboard
 * 	[2]: player 1 bitboard
 */
#define C4_STATE_SIZE 3

/*
 * Actions are column indices (0..6, left to right).
 * A column is a valid action while its top playable cell (row 5) is empty.
 * Connect-4 is deterministic: every action is a decision action.
 */
#define C4_MAX_NUM_DECISION_ACTIONS C4_NUM_COLS
#define C4_MAX_NUM_CHANCE_ACTIONS   0
#define C4_MAX_NUM_ACTIONS          C4_MAX_NUM_DECISION_ACTIONS

/**
 * Observation: two 6x7 binary planes, from the observing player's
 * perspective, row-major with row 0 (the bottom row) first:
 * 	plane 0: the observing player's stones
 * 	plane 1: the opponent's stones
 */
#define C4_OBS_NDIMS 3
#define C4_OBS_SIZE  (C4_NUM_PLAYERS * C4_NUM_ROWS * C4_NUM_COLS)

// Features: identical shape to the observation, as 0.0/1.0 floats
#define C4_FEATURES_NDIMS C4_OBS_NDIMS
#define C4_FEATURES_SIZE  C4_OBS_SIZE

/**
 * String buffer size:
 *         turn line + 6 board rows + separators + column footer + null
 * terminator.
 * Generous upper bound.
 */
#define C4_STRING_BUF_SIZE 512

extern const Game connect4;

#endif
