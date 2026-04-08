#ifndef PIG_H
#define PIG_H

#include "board_game.h"

#define PIG_NUM_PLAYERS 2
#define PIG_TARGET      100

/**
 * State layout:
 * 	[0]:        the player whose turn it currently is
 * 	[1]:        "awaiting roll?" flag
 * 	[2]:        turn total
 * 	[3..3+N-1]: per-player banked scores
 */
#define PIG_STATE_SIZE (3 + PIG_NUM_PLAYERS)

/**
 * Max across all state types:
 * 	- Decision nodes: 2 actions (HOLD, ROLL)
 * 	- Chance nodes:   6 actions (die faces)
 */
#define PIG_MAX_NUM_ACTIONS 6

/**
 * Observation: flat uint8 vector in the form
 * 	[my_score, next_player_score, ..., last_player_score, turn_total]
 */
#define PIG_OBS_NDIMS 1
#define PIG_OBS_SIZE  (PIG_NUM_PLAYERS + 1)

/**
 * Features: same shape as observation, but normalised by `PIG_TARGET`
 * so values lie approximately in [0, 1] for neural network consumption.
 * Values may slightly exceed 1.0 at or near terminal states (a banked
 * score can reach PIG_TARGET + 5 when a player holds on a winning roll).
 */
#define PIG_FEATURES_NDIMS 1
#define PIG_FEATURES_SIZE  PIG_OBS_SIZE

/**
 * String buffer size: one header line + one line per player + one line
 * for turn total + null terminator. Generous upper bound.
 */
#define PIG_STRING_BUF_SIZE (96 + 32 * PIG_NUM_PLAYERS)

extern const Game pig;

#endif
