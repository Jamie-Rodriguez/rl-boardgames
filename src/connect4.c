#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "connect4.h"

/**
 * State layout:
 * 	[0]: current player
 * 	[1]: player 0 bitboard
 * 	[2]: player 1 bitboard
 *
 * Bitboard layout: 7 bits per column, bit index = column * 7 + row (with
 * row 0 at the bottom).
 * Rows 0..5 are playable - row 6 of every column is an always-empty sentinel.
 * The sentinel breaks bit runs between neighbouring columns, which is what
 * lets the win test below run as a handful of branchless shift/AND operations
 * with no wrap-around corrections.
 * Also makes dropping a stone a single add-and-mask: adding 1 at a column's
 * base bit carries up through the occupied cells to the first empty cell.
 */
#define STATE_OFFSET_CURRENT_PLAYER 0
#define STATE_OFFSET_BOARDS         1

// Add a sentinel row to make it possible to use addition to do some fast
// computations
#define COL_BITS (C4_NUM_ROWS + 1)

/**
 * 64-bit memory filled with ones
 * Will have to change this if using a different memory size
 */
#define ALL_ONES   (~0ULL >> (64 - C4_NUM_COLS * COL_BITS))
#define BOTTOM_ROW (ALL_ONES / ((1ULL << COL_BITS) - 1))
// i.e. the playable cells
#define BOARD_MASK (BOTTOM_ROW * ((1ULL << C4_NUM_ROWS) - 1))

#define COL_MASK(c)   (UINT64_C(0x3F) << (COL_BITS * (c)))
#define BOTTOM_BIT(c) (UINT64_C(1) << (COL_BITS * (c)))
#define TOP_BIT(c)    (UINT64_C(1) << (COL_BITS * (c) + C4_NUM_ROWS - 1))

static const char player_to_piece[] = { ' ', 'O', 'X' };

/**
 * Branchless four-in-a-row test. For each line direction, one AND of the
 * The sentinel row keeps runs from crossing between columns.
 *
 * Example:
 *            col6    col5    col4    col3    col2    col1    col0
 * b      = 0000000 0000000 0000010 0000100 0001000 0010000 0000000
 * b >> 6 = 0000000 0000000 0000000 0000100 0001000 0010000 0100000
 * m      = 0000000 0000000 0000000 0000100 0001000 0010000 0000000
 *
 *         . . . . . . .
 * row 5   * . . . . . .
 * row 4   . @ . . . . .        # = b          (the four stones)
 * row 3   . . @ . . . .        * = b >> 6     (same stones, slid one step
 * up-left) row 2   . . . @ . . .        @ = both  →  m row 1   . . . . # . .
 * row 0   . . . . . . .
 *         0 1 2 3 4 5 6
 *
 * So m masks every piece that has a down-right neighbour.
 *
 * Shifting by 2 * shifts[direction] again finds every piece that has a
 * down-right neighbour, that ALSO has such a pair, two steps down the
 * diagonal from it, thereby calculating a diagonal of length 4:
 *
 * m >> 12   = 0000000 0000000 0000000 0000000 0000000 0010000 0100000 bits 5,11
 * m & m>>12 = 0000000 0000000 0000000 0000000 0000000 0010000 0000000 bit 11 →
 * win
 */
static inline bool has_won(uint64_t board) {
	static const uint64_t shifts[] = {
		1,            // vertical
		COL_BITS,     // horizontal
		COL_BITS - 1, // falling / NW->SE diagonal
		COL_BITS + 1, // rising / SW->NE diagonal
	};

	for (size_t direction = 0; direction < 4; direction++) {
		// Two-in-a-rows
		const uint64_t mask = board & (board >> shifts[direction]);

		if (mask & (mask >> (2 * shifts[direction])))
			return true;
	}

	return false;
}

static void c4_init(const void* config, uint64_t state[]) {
	(void)config; // Unused

	for (size_t i = 0; i < C4_STATE_SIZE; i++)
		state[i] = 0;
}

static uint64_t c4_get_current_player(const uint64_t state[]) {
	return state[STATE_OFFSET_CURRENT_PLAYER];
}

static bool c4_is_chance_node(const uint64_t state[]) {
	(void)state;
	return false;
}

static uint64_t c4_get_valid_actions(const uint64_t state[],
                                     uint64_t actions_out[]) {
	uint64_t occupied = 0;

	for (size_t p = 0; p < C4_NUM_PLAYERS; p++)
		occupied |= state[STATE_OFFSET_BOARDS + p];

	uint64_t n = 0;

	for (uint64_t c = 0; c < C4_NUM_COLS; c++)
		if ((occupied & TOP_BIT(c)) == 0)
			actions_out[n++] = c;

	return n;
}

static bool c4_is_terminal(const uint64_t state[]);

static void c4_apply_action(uint64_t state[], uint64_t action) {
	assert(!c4_is_terminal(state));
	assert(action < C4_NUM_COLS);

	const uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];

	uint64_t occupied = 0;

	for (size_t p = 0; p < C4_NUM_PLAYERS; p++)
		occupied |= state[STATE_OFFSET_BOARDS + p];

	/**
	 * Adding 1 at the column's base carries up through the column's
	 * occupied cells, landing precisely on its first empty cell.
	 * Example:
	 * 	a      0000001 0111111 0000111 0000000
	 * 	+ b    0000001 0000001 0000001 0000001
	 * 	=      0000010 1000000 0001000 0000001
	 * 	             ^  ^^^^^^     ^^^
	 * 	Note: the smaller bits of each column are eaten by the carry!
	 * 	This is why we & COL_MASK(action) after - to repopulate the
	 * 	column
	 */
	const uint64_t move =
	        (occupied + BOTTOM_BIT(action)) & COL_MASK(action);

	assert(move && "column is full");

	state[STATE_OFFSET_BOARDS + player] |= move;
	state[STATE_OFFSET_CURRENT_PLAYER] = (player + 1) % C4_NUM_PLAYERS;
}

static bool c4_is_terminal(const uint64_t state[]) {
	uint64_t occupied = 0;

	for (size_t p = 0; p < C4_NUM_PLAYERS; p++)
		occupied |= state[STATE_OFFSET_BOARDS + p];

	if (occupied == BOARD_MASK)
		return true;

	for (size_t p = 0; p < C4_NUM_PLAYERS; p++)
		if (has_won(state[STATE_OFFSET_BOARDS + p]))
			return true;

	return false;
}

static void c4_get_outcome(const uint64_t state[], int64_t scores_out[]) {
	assert(c4_is_terminal(state));

	for (uint64_t p = 0; p < C4_NUM_PLAYERS; p++) {
		if (has_won(state[STATE_OFFSET_BOARDS + p])) {
			// Connect-4 is zero-sum: one winner, the rest lose
			for (uint64_t q = 0; q < C4_NUM_PLAYERS; q++)
				scores_out[q] = (q == p) ? 1 : -1;
			return;
		}
	}

	// No winner: drawn (board full)
	for (uint64_t p = 0; p < C4_NUM_PLAYERS; p++)
		scores_out[p] = 0;
}

static void c4_get_observation(const uint64_t state[], uint64_t player,
                               uint8_t* obs_out) {
	assert(player < C4_NUM_PLAYERS);

	size_t i = 0;

	for (uint64_t p = 0; p < C4_NUM_PLAYERS; p++) {
		const uint64_t board = state[STATE_OFFSET_BOARDS +
		                             ((player + p) % C4_NUM_PLAYERS)];

		for (uint64_t r = 0; r < C4_NUM_ROWS; r++)
			for (uint64_t c = 0; c < C4_NUM_COLS; c++)
				obs_out[i++] = (uint8_t)((board >>
				                          (c * COL_BITS + r)) &
				                         1);
	}

	assert(i == C4_OBS_SIZE);
}

static void c4_get_features(const uint64_t state[], uint64_t player,
                            float* features_out) {
	assert(player < C4_NUM_PLAYERS);

	uint8_t obs[C4_OBS_SIZE];

	c4_get_observation(state, player, obs);

	for (size_t i = 0; i < C4_OBS_SIZE; i++)
		features_out[i] = (float)obs[i];
}

static inline char piece_at(const uint64_t state[], uint64_t bit_index) {
	assert(bit_index < C4_NUM_COLS * COL_BITS);

	const uint64_t bit = UINT64_C(1) << bit_index;

	for (uint64_t p = 0; p < C4_NUM_PLAYERS; p++)
		if (state[STATE_OFFSET_BOARDS + p] & bit)
			return player_to_piece[p + 1];

	return '.';
}

static uint64_t c4_to_string(const uint64_t state[], uint64_t buf_size,
                             char* buf) {
	assert(buf != NULL);
	assert(buf_size >= C4_STRING_BUF_SIZE);

	char* const start     = buf;
	size_t remaining      = (size_t)buf_size;
	const uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];
	int n;

	n = snprintf(buf, remaining, "Player %u (%c) to move\n",
	             (unsigned)(player + 1), player_to_piece[player + 1]);
	if (n > 0 && (size_t)n < remaining) {
		buf += n;
		remaining -= (size_t)n;
	}

	for (uint64_t row = C4_NUM_ROWS; row-- > 0;) {
		for (uint64_t col = 0; col < C4_NUM_COLS; col++) {
			n = snprintf(buf, remaining, " %c",
			             piece_at(state, col * COL_BITS + row));
			if (n > 0 && (size_t)n < remaining) {
				buf += n;
				remaining -= (size_t)n;
			}
		}

		n = snprintf(buf, remaining, "\n");
		if (n > 0 && (size_t)n < remaining) {
			buf += n;
			remaining -= (size_t)n;
		}
	}

	n = snprintf(buf, remaining, " 0 1 2 3 4 5 6\n");
	if (n > 0 && (size_t)n < remaining)
		buf += n;

	return (uint64_t)(buf - start);
}

static const char* c4_help_prompt(void) {
	return "Connect-4\n"
	       "=========\n"
	       "\n"
	       "Drop a stone into one of the seven columns; it falls to the\n"
	       "lowest empty cell. The first player to line up four stones\n"
	       "horizontally, vertically, or diagonally wins; a full board\n"
	       "with no line is a draw.\n"
	       "\n"
	       "Actions are column indices 0-6, left to right. A column is\n"
	       "valid while it has an empty cell.\n";
}

const Game connect4 = {
	.init               = c4_init,
	.get_current_player = c4_get_current_player,
	.is_chance_node     = c4_is_chance_node,
	.get_valid_actions  = c4_get_valid_actions,
	.apply_action       = c4_apply_action,
	.is_terminal        = c4_is_terminal,
	.get_outcome        = c4_get_outcome,
	.get_observation    = c4_get_observation,
	.get_features       = c4_get_features,
	.to_string          = c4_to_string,
	.help_prompt        = c4_help_prompt,
	.obs_dims           = { C4_NUM_PLAYERS, C4_NUM_ROWS, C4_NUM_COLS },
	.features_dims      = { C4_NUM_PLAYERS, C4_NUM_ROWS, C4_NUM_COLS },
};
