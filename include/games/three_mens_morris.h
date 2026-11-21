#ifndef THREE_MENS_MORRIS_H
#define THREE_MENS_MORRIS_H

#include "games/board_game.h"

#define TMM_NUM_PLAYERS 2
#define TMM_NUM_ROWS    3
#define TMM_NUM_COLS    3
#define TMM_BOARD_SIZE  (TMM_NUM_ROWS * TMM_NUM_COLS)

/* Phase, current player, then one bitboard per player */
#define TMM_STATE_SIZE (2 + TMM_NUM_PLAYERS)

/**
 * Placement has at most nine destinations. Movement has three pieces,
 * each with three empty destinations.
 */
#define TMM_MAX_NUM_DECISION_ACTIONS TMM_BOARD_SIZE
#define TMM_MAX_NUM_CHANCE_ACTIONS   0
#define TMM_MAX_NUM_ACTIONS          TMM_MAX_NUM_DECISION_ACTIONS

/**
 * Actions are from × 9 + to, with from == 9 representing the hand.
 * The ID space includes unused from == to actions.
 */
#define TMM_ACTION_SPACE_SIZE ((TMM_BOARD_SIZE + 1) * TMM_BOARD_SIZE)

/* Movement can repeat indefinitely; there is no repetition or turn limit. */
#define TMM_MAX_TURNS UINT64_MAX

/**
 * Upper bound: 1 + 9 + 72 + 252 + 756 + 1260 placement states (0..5 pieces),
 * plus 2 × C(9, 3) × C(6, 3) movement states, including either player to move.
 * This overcounts positions ruled out by earlier wins.
 */
#define TMM_STATE_SPACE_SIZE 5710

/**
 * Observation: a flat vector of shape { 20 }:
 *
 *   [0..8]:   the observing player's board (one 0/1 value per square)
 *   [9..17]:  the opponent's board (one 0/1 value per square)
 *   [18]:     phase: 0 = placement, 1 = movement
 *   [19]:     player to move: 0 = observer, 1 = opponent
 */
#define TMM_OBS_NDIMS 1
#define TMM_OBS_SIZE  (TMM_NUM_PLAYERS * TMM_BOARD_SIZE + 2)

/**
 * Features: four float planes of total shape { 4, 3, 3 }, using the same
 * square ordering as the observation. Each 3 × 3 plane contains:
 *
 *   0: the observing player's pieces
 *   1: the opponent's pieces
 *   2: phase: 0 = placement, 1 = movement (broadcast across all 9 slots)
 *   3: player to move: 0 = observer, 1 = opponent (broadcast across all 9
 *      slots)
 */
#define TMM_FEATURES_PLANES (TMM_NUM_PLAYERS + 2)
#define TMM_FEATURES_NDIMS  3
#define TMM_FEATURES_SIZE   (TMM_FEATURES_PLANES * TMM_BOARD_SIZE)

/**
 * Upper bound for the turn/phase line and null terminator (48), plus seven
 * board lines of 13 characters and a newline each.
 */
#define TMM_STRING_BUF_SIZE (48 + 7 * 14)

typedef uint16_t tmm_bitboard;

extern const Game three_mens_morris;

#endif /* THREE_MENS_MORRIS_H */
