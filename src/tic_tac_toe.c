#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "tic_tac_toe.h"
// Auto-generated from `generate_has_win_bit_array.c`
#include "ttt_has_win_bit_array.h"

#define NUM_ROWS    3
#define NUM_COLS    3
#define BOARD_SIZE  (NUM_ROWS * NUM_COLS)
#define NUM_PLAYERS 2

#define STATE_OFFSET_CURRENT_PLAYER 0
#define STATE_OFFSET_BOARD          1
#define STATE_SIZE                  (STATE_OFFSET_BOARD + BOARD_SIZE)

/**
 * Note `observation` shape is the same as the `features` shape for this
 * implementation
 */
#define OBS_NDIMS 3
#define OBS_SIZE  (NUM_PLAYERS * NUM_ROWS * NUM_COLS)

#define EMPTY_PIECE    0
#define PLAYER_1_PIECE 1
#define PLAYER_2_PIECE 2

/**
 * `state` shape:
 *
 * index:         0         1 ... 9
 *        +----------------+-------+
 *        | current player | board |
 *        +----------------+-------+
 *
 * [0]:    current player (zero-based)
 * 	0 = player 1, 1 = player 2
 * [1..9]: flattened 1D board,
 *         starting at bottom-left, going across each row
 * 	0 = empty square, 1 = player 1, 2 = player 2
 *
 * 	          col 0  col 1  col 2
 * 	        +------+------+------+
 * 	row 2   | s[7] | s[8] | s[9] |
 * 	        +------+------+------+
 * 	row 1   | s[4] | s[5] | s[6] |
 * 	        +------+------+------+
 * 	row 0   | s[1] | s[2] | s[3] |
 * 	        +------+------+------+
 */

#define TURN_STR_MAX 32
#define ROW_STR_LEN  (2 + (NUM_COLS - 1) * 4)
#define SEP_STR_LEN  (3 + (NUM_COLS - 1) * 4)
#define TTT_STRING_BUF_SIZE (            \
    TURN_STR_MAX +                       \
    NUM_ROWS * (ROW_STR_LEN + 1) +       \
    (NUM_ROWS - 1) * (SEP_STR_LEN + 1) + \
    1                                    \
)

static const char player_to_piece[] = { ' ', 'O', 'X' };

static void ttt_init(const void* config, uint64_t state[]) {
	(void)config; // Unused

	// Initialise state
	state[STATE_OFFSET_CURRENT_PLAYER] = 0;

	for (size_t i = STATE_OFFSET_BOARD; i < STATE_OFFSET_BOARD + BOARD_SIZE;
	     i++)
		state[i] = EMPTY_PIECE;
}

static uint64_t ttt_get_current_player(const uint64_t state[]) {
	return state[STATE_OFFSET_CURRENT_PLAYER];
}

static uint64_t ttt_get_valid_actions(const uint64_t state[],
                                      uint64_t actions_out[]) {
	size_t action_index = 0;

	for (size_t i = 0; i < BOARD_SIZE; i++)
		if (state[STATE_OFFSET_BOARD + i] == EMPTY_PIECE)
			actions_out[action_index++] = i;

	return action_index;
}


static void ttt_apply_action(uint64_t state[], uint64_t action) {
	assert(__builtin_popcountll(action) == 1);
	assert(state[action] == EMPTY_PIECE);
	assert(action < BOARD_SIZE);

	state[STATE_OFFSET_BOARD + action] =
	        state[STATE_OFFSET_CURRENT_PLAYER] + 1;
	state[STATE_OFFSET_CURRENT_PLAYER] =
	        (state[STATE_OFFSET_CURRENT_PLAYER] + 1) % NUM_PLAYERS;
}


static void ttt_state_to_bitboards(const uint64_t state[],
                                   ttt_bitboard bitboards_out[]) {
	memset(bitboards_out, 0, sizeof(ttt_bitboard) * NUM_PLAYERS);

	size_t bit_pos = 0;

	for (size_t i = STATE_OFFSET_BOARD; i < STATE_SIZE; i++, bit_pos++) {
		const uint64_t piece = state[i];

		if (piece != EMPTY_PIECE)
			bitboards_out[piece - 1] |= (ttt_bitboard)1 << bit_pos;
	}
}

static inline bool ttt_check_win(ttt_bitboard b) {
	return (has_win_bits[b >> 6] >> (b & 63)) & 1;
}

static bool ttt_is_terminal(const uint64_t state[]) {
	ttt_bitboard bitboards[NUM_PLAYERS] = { 0 };
	ttt_state_to_bitboards(state, bitboards);

	// Check for a winning streak
	for (size_t player = 0; player < NUM_PLAYERS; player++)
		if (ttt_check_win(bitboards[player]))
			return true;

	// No wins found, but the game could still be over due to the board
	// being full
	ttt_bitboard occupied = 0;

	for (size_t player = 0; player < NUM_PLAYERS; player++)
		occupied |= bitboards[player];

	const ttt_bitboard full = 0x1FF; // 0b111111111

	if (occupied == full)
		return true;

	return false;
}


static void ttt_get_outcome(const uint64_t state[], int64_t scores_out[]) {
	ttt_bitboard bitboards[NUM_PLAYERS] = { 0 };
	ttt_state_to_bitboards(state, bitboards);

	for (size_t player = 0; player < NUM_PLAYERS; player++) {
		if (ttt_check_win(bitboards[player])) {
			// Tic-Tac-Toe is a zero-sum game
			// i.e. For a win, there's only one winner - the
			// rest are losers
			for (size_t p = 0; p < NUM_PLAYERS; p++)
				scores_out[p] = (p == player) ? 1 : -1;
			return;
		}
	}

	// No winner/draw
	for (size_t p = 0; p < NUM_PLAYERS; p++)
		scores_out[p] = 0;
}


static void ttt_get_observation(const uint64_t state[], uint64_t player,
                                uint8_t* obs_out) {
	for (size_t p = 0; p < NUM_PLAYERS; p++) {
		uint64_t piece = ((player + p) % NUM_PLAYERS) + 1;

		for (size_t i = 0; i < BOARD_SIZE; i++)
			obs_out[p * BOARD_SIZE + i] =
			        (state[STATE_OFFSET_BOARD + i] == piece) ? 1
			                                                 : 0;
	}
}

static void ttt_get_features(const uint64_t state[], uint64_t player,
                             float* features_out) {
	uint8_t obs[OBS_SIZE];
	ttt_get_observation(state, player, obs);

	for (size_t i = 0; i < OBS_SIZE; i++)
		features_out[i] = (float)obs[i];
}

static uint64_t ttt_to_string(const uint64_t state[], uint64_t buf_size,
                              char* buf) {
	assert(buf_size >= TTT_STRING_BUF_SIZE);

	char* start     = buf;
	uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];

	buf += snprintf(buf, buf_size, "Player %u (%c) to move\n",
	                (unsigned)(player + 1), player_to_piece[player + 1]);

	for (size_t row = NUM_ROWS; row-- > 0;) {
		if (row < NUM_ROWS - 1) {
			for (size_t col = 0; col < NUM_COLS; col++) {
				if (col)
					*buf++ = '+';

				*buf++ = '-';
				*buf++ = '-';
				*buf++ = '-';
			}

			*buf++ = '\n';
		}

		for (size_t col = 0; col < NUM_COLS; col++) {
			if (col) {
				*buf++ = ' ';
				*buf++ = '|';
			}

			*buf++ = ' ';
			*buf++ = player_to_piece[state[STATE_OFFSET_BOARD +
			                               row * NUM_COLS + col]];
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
	.num_players        = NUM_PLAYERS,
	.state_size         = STATE_SIZE,
	.max_actions        = BOARD_SIZE,
	.string_buf_size    = TTT_STRING_BUF_SIZE,
	.obs_ndims          = OBS_NDIMS,
	.obs_dims           = { NUM_PLAYERS, NUM_ROWS, NUM_COLS },
	.obs_size           = OBS_SIZE,
	.features_ndims     = OBS_NDIMS,
	.features_dims      = { NUM_PLAYERS, NUM_ROWS, NUM_COLS },
	.features_size      = OBS_SIZE
};

