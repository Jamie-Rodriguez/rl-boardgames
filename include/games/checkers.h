#ifndef CHECKERS_H
#define CHECKERS_H

#include "games/board_game.h"

/**
 * English draughts/American checkers, classically on an 8×8 board:
 *
 * 	- men move one square diagonally-forward; kings move one square
 * 	  diagonally in any direction
 * 	- captures jump over an adjacent enemy piece onto the empty square
 * 	  beyond, and are MANDATORY: if any capture exists, *only captures are
 * 	  legal*
 * 	- multi-jumps are modeled as sequential actions: after a jump that
 * 	  can be continued by the same piece, the same player moves again and
 * 	  get_valid_actions() returns only that piece's follow-up jump(s)
 * 	  (see `state[5]`)
 * 	- a man reaching the far row promotes to king; promotion ends the move,
 * 	  even mid-jump-sequence as per the classic English rule
 * 	- a player with no legal move loses
 * 	- 80 consecutive plies/moves without a capture or a man move ends the
 * 	  game as a draw (bounds episode length; see CHECKERS_DRAW_PLIES and
 * 	  CHECKERS_MAX_TURNS below)
 * 	- state, actions, observations and features all use the side to move's
 * 	  perspective; only to_string() shows a fixed board, player 1's frame.
 */

#define CHECKERS_NUM_PLAYERS 2

/**
 * If CHECKERS_NUM_ROWS × CHECKERS_NUM_COLS > 64, the uint64_t bitboards
 * (and with them the state layout) will have to be re-worked.
 */
#define CHECKERS_NUM_ROWS 8
#define CHECKERS_NUM_COLS 8

#define CHECKERS_NUM_SQUARES (CHECKERS_NUM_ROWS * CHECKERS_NUM_COLS)

/**
 * State layout:
 * 	[0]: current player
 * 	[1]: side-to-move piece bitboard (men and kings)
 * 	[2]: opponent piece bitboard (men and kings)
 * 	[3]: king bitboard
 * 	     (both sides; a piece is a king iff its bit is set here AND in its
 * 	     owner's board)
 * 	[4]: quiet-ply counter (plies since the last capture or man move)
 * 	[5]: continuation bit: the single set bit of a piece that must keep
 * 	     jumping (multi-jump in progress)
 *
 * The board is stored from the side to move's point of view i.e. the mover
 * always advances towards the top row.
 * When the turn passes, every bitboard is rotated 180 degrees and the two piece
 * slots swap.
 *
 * Bitboard layout: bit index = row × 8 + column, row 0 at the bottom of the
 * mover's view.
 */
#define CHECKERS_STATE_SIZE (CHECKERS_NUM_PLAYERS + 4)

/**
 * Actions are expressed in the side to move's (canonical) frame and encode
 * (from-square, direction) as:
 *
 * 	action = from_square × CHECKERS_NUM_DIRS + direction
 *
 * where from_square is the piece's bit index in the canonical bitboards and
 * direction is:
 *
 * 	0: up-left  (bit index + 7)
 * 	1: up-right (bit index + 9)
 * 	2: down-left  (bit index - 9)
 * 	3: down-right (bit index - 7)
 */
#define CHECKERS_NUM_DIRS 4

#define CHECKERS_MEN_ROWS ((CHECKERS_NUM_ROWS - 2) / 2)
#define CHECKERS_NUM_MEN                               \
	(CHECKERS_MEN_ROWS * (CHECKERS_NUM_COLS / 2) + \
	 (CHECKERS_MEN_ROWS + 1) / 2 * (CHECKERS_NUM_COLS % 2))

// At most every man × every direction can be simultaneously legal
#define CHECKERS_MAX_NUM_DECISION_ACTIONS (CHECKERS_NUM_MEN * CHECKERS_NUM_DIRS)
#define CHECKERS_MAX_NUM_CHANCE_ACTIONS   0
#define CHECKERS_MAX_NUM_ACTIONS          CHECKERS_MAX_NUM_DECISION_ACTIONS

/**
 * Actions are expressed as from_square × NUM_DIRS + direction over every board
 * square - so the decision-action ID space is larger than the count of
 * simultaneously-legal moves; IDs that never occur (light squares) are dead
 * indices
 */
#define CHECKERS_ACTION_SPACE_SIZE (CHECKERS_NUM_SQUARES * CHECKERS_NUM_DIRS)

// Plies without a capture or a man move before the game is drawn
#define CHECKERS_DRAW_PLIES 80

/**
 * Loose episode bound via the draw rule: every ply either advances a
 * man (each of the 2 × NUM_MEN men has at most NUM_ROWS - 1 forward
 * moves), captures (at most 2 × NUM_MEN pieces), or counts towards
 * the CHECKERS_DRAW_PLIES quiet-ply budget that ends the game.
 */
#define CHECKERS_MAX_IRREVERSIBLE_PLIES \
	(2 * CHECKERS_NUM_MEN * (CHECKERS_NUM_ROWS - 1) + 2 * CHECKERS_NUM_MEN)
#define CHECKERS_MAX_TURNS                                             \
	((CHECKERS_MAX_IRREVERSIBLE_PLIES + 1) * CHECKERS_DRAW_PLIES + \
	 CHECKERS_MAX_IRREVERSIBLE_PLIES)

/**
 * 8×8 draughts has ~5 × 10^20 reachable positions (Schaeffer et al.'s
 * enumeration), which is beyond what a uint64 can count, so this saturates
 * per the convention in board_game.h.
 */
#define CHECKERS_STATE_SPACE_SIZE UINT64_MAX

/**
 * Observation: two row-major, flattened board lists from the mover's
 * perspective (mover-first), followed by two scalar fields.
 *
 * 	[0..63]:    mover's board: 0 = empty, 1 = man, 2 = king
 * 	[64..127]:  opponent's board, with the same piece codes
 * 	[128]:      square that must continue jumping, or 64 when none
 * 	[129]:      raw quiet-ply counter
 */
#define CHECKERS_OBS_EMPTY           0
#define CHECKERS_OBS_MAN             1
#define CHECKERS_OBS_KING            2
#define CHECKERS_OBS_NO_CONTINUATION CHECKERS_NUM_SQUARES

#define CHECKERS_OBS_CONTINUATION_OFFSET \
	(CHECKERS_NUM_PLAYERS * CHECKERS_NUM_SQUARES)
#define CHECKERS_OBS_QUIET_PLIES_OFFSET (CHECKERS_OBS_CONTINUATION_OFFSET + 1)

#define CHECKERS_OBS_NDIMS 1
#define CHECKERS_OBS_SIZE  (CHECKERS_OBS_QUIET_PLIES_OFFSET + 1)

/**
 * Features: six planes in the same mover-relative coordinates:
 * 	0: mover's men; 1: mover's kings
 * 	2: opponent's men; 3: opponent's kings
 * 	4: continuation square (one-hot; all zero when none)
 * 	5: quiet-ply counter / CHECKERS_DRAW_PLIES, broadcast to every cell
 */
#define CHECKERS_FEATURES_PLANES (2 * CHECKERS_NUM_PLAYERS + 2)

#define CHECKERS_FEATURES_NDIMS 3
#define CHECKERS_FEATURES_SIZE (CHECKERS_FEATURES_PLANES * CHECKERS_NUM_SQUARES)

/**
 * String buffer size: turn/status lines + board rows + column footer +
 * null terminator. Generous upper bound (512 on the classic board).
 */
#define CHECKERS_STRING_BUF_SIZE \
	(192 + (CHECKERS_NUM_ROWS + 2) * (3 * CHECKERS_NUM_COLS + 8))

extern const Game checkers;

#endif
