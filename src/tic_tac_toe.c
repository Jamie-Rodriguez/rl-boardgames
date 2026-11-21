#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "tic_tac_toe.h"
// Auto-generated from `generate_has_win_bit_array.c`
#include "ttt_has_win_bit_array.h"

/**
 * `state` shape:
 *
 * index:      0         1          2
 *        +---------+----------+----------+
 *        | current |    p1    |    p2    |
 *        | player  | bitboard | bitboard |
 *        +---------+----------+----------+
 *
 * [0]:    current player (zero-based)
 * 	       0 = player 1, 1 = player 2
 * [1..2]: per-player bitboards (bits 0..8),
 *
 * 	          col 0  col 1  col 2
 * 	        +------+------+------+
 * 	row 2   |  b6  |  b7  |  b8  |
 * 	        +------+------+------+
 * 	row 1   |  b3  |  b4  |  b5  |
 * 	        +------+------+------+
 * 	row 0   |  b0  |  b1  |  b2  |
 * 	        +------+------+------+
 */
#define STATE_OFFSET_CURRENT_PLAYER 0
#define STATE_OFFSET_BOARD          1

#define FULL_BOARD ((1U << TTT_BOARD_SIZE) - 1U)

static const char player_to_piece[] = { ' ', 'O', 'X' };

static void ttt_init(const void* config, uint64_t state[]) {
	(void)config; // Unused

	// Initialise state
	state[STATE_OFFSET_CURRENT_PLAYER] = 0;

	for (size_t i = STATE_OFFSET_BOARD;
	     i < STATE_OFFSET_BOARD + TTT_NUM_PLAYERS; i++)
		state[i] = 0; // Empty bitboard
}

static uint64_t ttt_get_current_player(const uint64_t state[]) {
	return state[STATE_OFFSET_CURRENT_PLAYER];
}

static uint64_t ttt_get_valid_actions(const uint64_t state[],
                                      uint64_t actions_out[]) {
	ttt_bitboard occupied = 0;

	for (size_t p = 0; p < TTT_NUM_PLAYERS; p++)
		occupied |= (ttt_bitboard)state[STATE_OFFSET_BOARD + p];

	const ttt_bitboard empty = (ttt_bitboard)~occupied & FULL_BOARD;

	size_t action_count = 0;

	for (size_t i = 0; i < TTT_BOARD_SIZE; i++)
		if (empty & ((ttt_bitboard)1 << i))
			actions_out[action_count++] = i;

	return action_count;
}

static void ttt_apply_action(uint64_t state[], uint64_t action) {
	assert(action < TTT_BOARD_SIZE);

	ttt_bitboard occupied = 0;

	for (size_t p = 0; p < TTT_NUM_PLAYERS; p++)
		occupied |= (ttt_bitboard)state[STATE_OFFSET_BOARD + p];

	assert(!(occupied & ((ttt_bitboard)1 << action)));
	(void)occupied;

	const uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];
	state[STATE_OFFSET_BOARD + player] |= (ttt_bitboard)1 << action;
	state[STATE_OFFSET_CURRENT_PLAYER] = (player + 1) % TTT_NUM_PLAYERS;
}

static inline bool ttt_check_win(ttt_bitboard b) {
	return (has_win_bits[b >> 6] >> (b & 63)) & 1;
}

static bool ttt_is_terminal(const uint64_t state[]) {
	// Check for a winning streak
	for (size_t player = 0; player < TTT_NUM_PLAYERS; player++)
		if (ttt_check_win(
		            (ttt_bitboard)state[STATE_OFFSET_BOARD + player]))
			return true;

	// No wins found, but the game could still be over due to the board
	// being full
	ttt_bitboard occupied = 0;

	for (size_t player = 0; player < TTT_NUM_PLAYERS; player++)
		occupied |= (ttt_bitboard)state[STATE_OFFSET_BOARD + player];

	if (occupied == FULL_BOARD)
		return true;

	return false;
}


static void ttt_get_outcome(const uint64_t state[], int64_t scores_out[]) {
	for (size_t player = 0; player < TTT_NUM_PLAYERS; player++) {
		if (ttt_check_win(
		            (ttt_bitboard)state[STATE_OFFSET_BOARD + player])) {
			// Tic-Tac-Toe is a zero-sum game
			// i.e. For a win, there's only one winner - the
			// rest are losers
			for (size_t p = 0; p < TTT_NUM_PLAYERS; p++)
				scores_out[p] = (p == player) ? 1 : -1;
			return;
		}
	}

	// No winner/draw
	for (size_t p = 0; p < TTT_NUM_PLAYERS; p++)
		scores_out[p] = 0;
}


static void ttt_get_observation(const uint64_t state[], uint64_t player,
                                uint8_t* obs_out) {
	for (size_t p = 0; p < TTT_NUM_PLAYERS; p++) {
		uint64_t board_player = (player + p) % TTT_NUM_PLAYERS;
		ttt_bitboard bb =
		        (ttt_bitboard)state[STATE_OFFSET_BOARD + board_player];

		for (size_t i = 0; i < TTT_BOARD_SIZE; i++)
			obs_out[p * TTT_BOARD_SIZE + i] = (bb >> i) & 1;
	}
}

static void ttt_get_features(const uint64_t state[], uint64_t player,
                             float* features_out) {
	uint8_t obs[TTT_OBS_SIZE];
	ttt_get_observation(state, player, obs);

	for (size_t i = 0; i < TTT_OBS_SIZE; i++)
		features_out[i] = (float)obs[i];
}

static inline char ttt_piece_at(const uint64_t state[], size_t square) {
	const ttt_bitboard square_bitmask = (ttt_bitboard)(1 << square);

	for (size_t player = 0; player < TTT_NUM_PLAYERS; player++)
		if (state[STATE_OFFSET_BOARD + player] & square_bitmask)
			return player_to_piece[player + 1];

	// Empty piece
	return player_to_piece[0];
}

static uint64_t ttt_to_string(const uint64_t state[], uint64_t buf_size,
                              char* buf) {
	assert(buf_size >= TTT_STRING_BUF_SIZE);

	char* start     = buf;
	uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];

	buf += snprintf(buf, buf_size, "Player %u (%c) to move\n",
	                (unsigned)(player + 1), player_to_piece[player + 1]);

	for (size_t row = TTT_NUM_ROWS; row-- > 0;) {
		if (row < TTT_NUM_ROWS - 1) {
			for (size_t col = 0; col < TTT_NUM_COLS; col++) {
				if (col)
					*buf++ = '+';

				*buf++ = '-';
				*buf++ = '-';
				*buf++ = '-';
			}

			*buf++ = '\n';
		}

		for (size_t col = 0; col < TTT_NUM_COLS; col++) {
			if (col) {
				*buf++ = ' ';
				*buf++ = '|';
			}

			*buf++ = ' ';
			*buf++ = ttt_piece_at(state, row * TTT_NUM_COLS + col);
		}

		*buf++ = '\n';
	}

	*buf = '\0';
	return (uint64_t)(buf - start);
}

static const char* ttt_help_prompt(void) {
	return "Tic-Tac-Toe\n"
	       "===========\n"
	       "\n"
	       "Move indexes:\n"
	       "\n"
	       " 6 | 7 | 8\n"
	       "---+---+---\n"
	       " 3 | 4 | 5\n"
	       "---+---+---\n"
	       " 0 | 1 | 2\n";
}

const Game tic_tac_toe = {
	.init               = ttt_init,
	.get_current_player = ttt_get_current_player,
	.get_valid_actions  = ttt_get_valid_actions,
	.apply_action       = ttt_apply_action,
	.is_terminal        = ttt_is_terminal,
	.get_outcome        = ttt_get_outcome,
	.get_observation    = ttt_get_observation,
	.get_features       = ttt_get_features,
	.to_string          = ttt_to_string,
	.help_prompt        = ttt_help_prompt,
	.obs_dims           = { TTT_NUM_PLAYERS, TTT_NUM_ROWS, TTT_NUM_COLS },
	.features_dims      = { TTT_NUM_PLAYERS, TTT_NUM_ROWS, TTT_NUM_COLS },
};
