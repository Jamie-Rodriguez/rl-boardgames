#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include "checkers.h"


// Plies/moves without a capture or man move before the game is drawn
#define DRAW_PLIES 80

#define ACTION(square, direction) ((square) * CHECKERS_NUM_DIRS + (direction))
#define DIR_UP_LEFT               0
#define DIR_UP_RIGHT              1
#define DIR_DOWN_LEFT             2
#define DIR_DOWN_RIGHT            3

/**
 * The mover's men use the forward directions {0, 1} (up-left, up-right) and
 * only kings use the backward pair {2, 3}
 */
#define NUM_FWD_DIRS 2

/**
 * MEN_INIT_MOVER/MEN_INIT_OPP fill the board's two ends, and
 * man_chars[]/king_chars[] name two players. Extend those anchors
 * before raising the player count.
 */
#if CHECKERS_NUM_PLAYERS != 2
#error "Checkers' canonical board frame assumes exactly two players"
#endif

/**
 * The 180-degree rotation maps square (r, c) to
 * (CHECKERS_NUM_ROWS - 1 - r, CHECKERS_NUM_COLS - 1 - c), which preserves square colour only when
 * the two dimensions have equal parity
 */
#if (CHECKERS_NUM_ROWS + CHECKERS_NUM_COLS) % 2 != 0
#error "The 180-degree rotation must map dark squares to dark squares"
#endif

// All observation planes are bitboards except the final counter plane
#define NUM_BIT_PLANES (CHECKERS_OBS_PLANES - 1)

/**
 * `state` layout (see checkers.h for the authoritative description):
 * 	[0]: current player          [3]: king bitboard (both sides)
 * 	[1]: mover piece bitboard    [4]: quiet-ply counter
 * 	[2]: opponent piece bitboard [5]: multi-jump continuation bit
 *
 * Slot [1] always belongs to the side to move. Passing the turn rotates every
 * bitboard 180 degrees and shifts the piece slots so the new mover lands in
 * slot [1]
 */
#define STATE_OFFSET_CURRENT_PLAYER 0
#define STATE_OFFSET_BOARDS         1
#define STATE_OFFSET_KINGS          (STATE_OFFSET_BOARDS + CHECKERS_NUM_PLAYERS)
#define STATE_OFFSET_QUIET_PLIES    (STATE_OFFSET_KINGS + 1)
#define STATE_OFFSET_CONTINUATION   (STATE_OFFSET_QUIET_PLIES + 1)

#define LOW_BITS(n) (~UINT64_C(0) >> (64 - (n)))

#define BOARD_MASK LOW_BITS(CHECKERS_NUM_SQUARES)

#define ROW_MASK(r)  (LOW_BITS(CHECKERS_NUM_COLS) << ((r) * CHECKERS_NUM_COLS))
#define FILE_MASK(c) ((BOARD_MASK / ROW_MASK(0)) << (c))

// Guard shift_dir() against wrap-around
#define FILE_FIRST FILE_MASK(0)
#define FILE_LAST  FILE_MASK(CHECKERS_NUM_COLS - 1)

#define FILES_EVEN (((~UINT64_C(0) / 3) & ROW_MASK(0)) * FILE_MASK(0))
#define ROWS_ODD       \
	(ROW_MASK(1) * \
	 (LOW_BITS(2 * CHECKERS_NUM_COLS * (CHECKERS_NUM_ROWS / 2)) / LOW_BITS(2 * CHECKERS_NUM_COLS)))
#define DARK_SQUARES (FILES_EVEN ^ ROWS_ODD)

// The mover is orientated on the bottom rows, with the opponent on the top rows
#define MEN_INIT_MOVER (DARK_SQUARES & LOW_BITS(CHECKERS_MEN_ROWS * CHECKERS_NUM_COLS))
#define MEN_INIT_OPP                                   \
	(DARK_SQUARES & (LOW_BITS(CHECKERS_MEN_ROWS * CHECKERS_NUM_COLS) \
	                 << ((CHECKERS_NUM_ROWS - CHECKERS_MEN_ROWS) * CHECKERS_NUM_COLS)))

// The mover always promotes on the far row of the board
#define PROMO_ROW ROW_MASK(CHECKERS_NUM_ROWS - 1)

static inline uint64_t shift_dir(uint64_t board, uint64_t direction) {
	uint64_t shifted;

	switch (direction) {
	case DIR_UP_LEFT:
		shifted = (board & ~FILE_FIRST) << (CHECKERS_NUM_COLS - 1);
		break;
	case DIR_UP_RIGHT:
		shifted = (board & ~FILE_LAST) << (CHECKERS_NUM_COLS + 1);
		break;
	case DIR_DOWN_LEFT:
		shifted = (board & ~FILE_FIRST) >> (CHECKERS_NUM_COLS + 1);
		break;
	case DIR_DOWN_RIGHT:
		shifted = (board & ~FILE_LAST) >> (CHECKERS_NUM_COLS - 1);
		break;
	default:
		shifted = board;
		break;
	}

	return shifted & BOARD_MASK;
}

// 180-degree board rotation: bit i moves to bit CHECKERS_NUM_SQUARES - 1 - i
static inline uint64_t rotate180(uint64_t board) {
	return __builtin_bitreverse64(board) >> (64 - CHECKERS_NUM_SQUARES);
}

static inline uint64_t all_pieces(const uint64_t state[]) {
	uint64_t all = 0;

	for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
		all |= state[STATE_OFFSET_BOARDS + p];

	return all;
}

static inline uint64_t opp_pieces(const uint64_t state[]) {
	return all_pieces(state) & ~state[STATE_OFFSET_BOARDS];
}

static bool piece_can_jump(const uint64_t state[], uint64_t piece) {
	const uint64_t own        = state[STATE_OFFSET_BOARDS];
	const uint64_t opp        = opp_pieces(state);
	const uint64_t empty      = ~(own | opp);
	const bool has_king       = (state[STATE_OFFSET_KINGS] & piece) != 0;
	const uint64_t directions = has_king ? CHECKERS_NUM_DIRS : NUM_FWD_DIRS;

	for (uint64_t direction = 0; direction < directions; direction++) {
		const uint64_t adjacent_enemy =
		        shift_dir(piece, direction) & opp;

		if (adjacent_enemy &&
		    (shift_dir(adjacent_enemy, direction) & empty))
			return true;
	}

	return false;
}

static bool has_any_move(const uint64_t state[]) {
	if (state[STATE_OFFSET_CONTINUATION])
		return true;

	const uint64_t own   = state[STATE_OFFSET_BOARDS];
	const uint64_t opp   = opp_pieces(state);
	const uint64_t kings = state[STATE_OFFSET_KINGS] & own;
	const uint64_t empty = ~(own | opp);

	for (uint64_t fwd_move = 0; fwd_move < NUM_FWD_DIRS; fwd_move++) {
		// Forward 1-step moves
		if (shift_dir(own, fwd_move) & empty)
			return true;
		// Forward jumps/captures
		if (shift_dir(shift_dir(own, fwd_move) & opp, fwd_move) & empty)
			return true;

		// ...but only kings may move backward
		const uint64_t bwd_move = NUM_FWD_DIRS + fwd_move;

		// Backward 1-step moves
		if (shift_dir(kings, bwd_move) & empty)
			return true;
		// Backward jumps/captures
		if (shift_dir(shift_dir(kings, bwd_move) & opp, bwd_move) &
		    empty)
			return true;
	}

	return false;
}

#ifndef NDEBUG
/**
 * Jump-only variant of has_any_move(), used only to assert forced
 * captures in apply_action(). Compiled out with the asserts.
 */
static bool has_any_jump(const uint64_t state[]) {
	const uint64_t own   = state[STATE_OFFSET_BOARDS];
	const uint64_t opp   = opp_pieces(state);
	const uint64_t kings = state[STATE_OFFSET_KINGS] & own;
	const uint64_t empty = ~(own | opp);

	for (uint64_t fwd_move = 0; fwd_move < NUM_FWD_DIRS; fwd_move++) {
		if (shift_dir(shift_dir(own, fwd_move) & opp, fwd_move) & empty)
			return true;

		const uint64_t bwd_move = NUM_FWD_DIRS + fwd_move;

		if (shift_dir(shift_dir(kings, bwd_move) & opp, bwd_move) &
		    empty)
			return true;
	}

	return false;
}
#endif

static void checkers_init(const void* config, uint64_t state[]) {
	(void)config; // Unused

	for (size_t i = 0; i < CHECKERS_STATE_SIZE; i++)
		state[i] = 0;

	static const uint64_t men_init[CHECKERS_NUM_PLAYERS] = {
		MEN_INIT_MOVER,
		MEN_INIT_OPP,
	};

	for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
		state[STATE_OFFSET_BOARDS + p] = men_init[p];
}

static uint64_t checkers_get_current_player(const uint64_t state[]) {
	return state[STATE_OFFSET_CURRENT_PLAYER];
}

static bool checkers_is_chance_node(const uint64_t state[]) {
	(void)state;
	return false;
}

static uint64_t checkers_get_valid_actions(const uint64_t state[],
                                           uint64_t actions_out[]) {
	const uint64_t own   = state[STATE_OFFSET_BOARDS];
	const uint64_t opp   = opp_pieces(state);
	const uint64_t kings = state[STATE_OFFSET_KINGS];
	const uint64_t empty = ~(own | opp);
	uint64_t n           = 0;

	// We are mid multi-jump - only the jumping piece may act
	const uint64_t sources = state[STATE_OFFSET_CONTINUATION]
	                                 ? state[STATE_OFFSET_CONTINUATION]
	                                 : own;

	// Jumps are mandatory: emit them first and, if any, only them

	/**
	 * Iterates through *number of set bits* only - removes the lowest-set
	 * bit each loop
	 */
	for (uint64_t board = sources; board; board &= board - 1) {
		/**
		 * ~b + 1 is two's-complement negation
		 * AND-ing with the two's-complement negation gives only the
		 * lowest-set bit
		 */
		const uint64_t piece      = board & (~board + 1);
		const uint64_t square     = (uint64_t)__builtin_ctzll(piece);
		const bool king           = (kings & piece) != 0;
		const uint64_t directions = king ? CHECKERS_NUM_DIRS : NUM_FWD_DIRS;

		for (uint64_t direction = 0; direction < directions;
		     direction++) {
			const uint64_t move = shift_dir(piece, direction) & opp;

			if (move && (shift_dir(move, direction) & empty))
				actions_out[n++] = ACTION(square, direction);
		}
	}

	assert(n <= CHECKERS_MAX_NUM_ACTIONS);

	if (n > 0 || state[STATE_OFFSET_CONTINUATION])
		return n;

	// No jump anywhere: quiet steps
	for (uint64_t board = own; board; board &= board - 1) {
		const uint64_t piece      = board & (~board + 1);
		const uint64_t square     = (uint64_t)__builtin_ctzll(piece);
		const bool king           = (kings & piece) != 0;
		const uint64_t directions = king ? CHECKERS_NUM_DIRS : NUM_FWD_DIRS;

		for (uint64_t direction = 0; direction < directions;
		     direction++) {
			if (shift_dir(piece, direction) & empty)
				actions_out[n++] = ACTION(square, direction);
		}
	}

	assert(n <= CHECKERS_MAX_NUM_ACTIONS);
	return n;
}

static bool checkers_is_terminal(const uint64_t state[]);

static void checkers_apply_action(uint64_t state[], uint64_t action) {
	assert(!checkers_is_terminal(state));

	const uint64_t sq  = action / CHECKERS_NUM_DIRS;
	const uint64_t dir = action % CHECKERS_NUM_DIRS;

	assert(sq < CHECKERS_NUM_SQUARES);

	const uint64_t from = UINT64_C(1) << sq;
	const uint64_t own  = state[STATE_OFFSET_BOARDS];
	const uint64_t opp  = opp_pieces(state);

	assert(own & from);
	assert(state[STATE_OFFSET_CONTINUATION] == 0 ||
	       state[STATE_OFFSET_CONTINUATION] == from);

	const bool is_king = (state[STATE_OFFSET_KINGS] & from) != 0;

	// Men may only move forward/"up" the canonical board (kings any
	// direction)
	assert(is_king || dir < NUM_FWD_DIRS);

	const uint64_t step   = shift_dir(from, dir);
	const bool is_capture = step & opp;
	const uint64_t to     = is_capture ? shift_dir(step, dir) : step;

	if (is_capture) {
		/**
		 * Jump: remove the enemy piece landed over. Only its
		 * owner's board holds that square, so clearing it from
		 * every board changes exactly one of them.
		 */
		assert((to & ~(own | opp)) && to);

		for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
			state[STATE_OFFSET_BOARDS + p] &= ~step;
		state[STATE_OFFSET_KINGS] &= ~step;
	} else {
		// Quiet move
		assert((step & ~(own | opp)) && step);
		assert(state[STATE_OFFSET_CONTINUATION] == 0);
		assert(!has_any_jump(state) && "captures are mandatory");
	}

	// Put piece on the new spot
	state[STATE_OFFSET_BOARDS] = (own & ~from) | to;

	if (is_king)
		state[STATE_OFFSET_KINGS] =
		        (state[STATE_OFFSET_KINGS] & ~from) | to;

	const bool promotion = !is_king && (to & PROMO_ROW) != 0;

	if (promotion)
		state[STATE_OFFSET_KINGS] |= to;

	// The draw counter resets on any capture or man move
	if (is_capture || !is_king)
		state[STATE_OFFSET_QUIET_PLIES] = 0;
	else
		state[STATE_OFFSET_QUIET_PLIES]++;

	/**
	 * A jump that can be continued by the same piece keeps the turn (multi-
	 * jump). Promotion ends the move even if further jumps exist; the
	 * classic English rule. The turn does not pass, so  the frame stays the
	 * mover's
	 */
	if (is_capture && !promotion && piece_can_jump(state, to)) {
		state[STATE_OFFSET_CONTINUATION] = to;
		return;
	}

	// Rotate turn to next player
	state[STATE_OFFSET_CONTINUATION] = 0;
	state[STATE_OFFSET_CURRENT_PLAYER] =
	        (state[STATE_OFFSET_CURRENT_PLAYER] + 1) % CHECKERS_NUM_PLAYERS;

	/**
	 * Re-canonicalise for the new mover: rotate every bitboard 180
	 * degrees and shift the piece slots so slot 0 is the new mover.
	 */
	uint64_t rotated[CHECKERS_NUM_PLAYERS];

	for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
		rotated[p] = rotate180(state[STATE_OFFSET_BOARDS +
		                             (p + 1) % CHECKERS_NUM_PLAYERS]);

	for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
		state[STATE_OFFSET_BOARDS + p] = rotated[p];

	state[STATE_OFFSET_KINGS] = rotate180(state[STATE_OFFSET_KINGS]);
}

static bool checkers_is_terminal(const uint64_t state[]) {
	return state[STATE_OFFSET_QUIET_PLIES] >= DRAW_PLIES ||
	       !has_any_move(state);
}

static void checkers_get_outcome(const uint64_t state[], int64_t scores_out[]) {
	assert(checkers_is_terminal(state));

	if (!has_any_move(state)) {
		/**
		 * The player unable to move loses (includes "no pieces");
		 * every other player wins
		 */
		for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
			scores_out[p] = p == state[STATE_OFFSET_CURRENT_PLAYER]
			                        ? -1
			                        : 1;

		return;
	}

	// State is terminal with a move available.
	// The quiet-ply limit must've been reached: draw
	assert(state[STATE_OFFSET_QUIET_PLIES] >= DRAW_PLIES);

	for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
		scores_out[p] = 0;
}

static void checkers_get_observation(const uint64_t state[], uint64_t player,
                                     uint8_t* obs_out) {
	assert(player < CHECKERS_NUM_PLAYERS);

	// Rotate the observer's slot relative to the mover
	const uint64_t seat = (player + CHECKERS_NUM_PLAYERS -
	                       state[STATE_OFFSET_CURRENT_PLAYER]) %
	                      CHECKERS_NUM_PLAYERS;
	const bool rotate   = seat != 0;

	const uint64_t kings = rotate ? rotate180(state[STATE_OFFSET_KINGS])
	                              : state[STATE_OFFSET_KINGS];

	/**
	 * Men + kings planes per player: the observer first, then the
	 * remaining players in turn order
	 */
	uint64_t planes[NUM_BIT_PLANES];

	for (uint64_t i = 0; i < CHECKERS_NUM_PLAYERS; i++) {
		uint64_t board = state[STATE_OFFSET_BOARDS +
		                       (seat + i) % CHECKERS_NUM_PLAYERS];

		if (rotate)
			board = rotate180(board);

		planes[2 * i]     = board & ~kings;
		planes[2 * i + 1] = board & kings;
	}

	planes[2 * CHECKERS_NUM_PLAYERS] =
	        rotate ? rotate180(state[STATE_OFFSET_CONTINUATION])
	               : state[STATE_OFFSET_CONTINUATION];

	size_t i = 0;

	for (size_t p = 0; p < NUM_BIT_PLANES; p++)
		for (uint64_t sq = 0; sq < CHECKERS_NUM_SQUARES; sq++)
			obs_out[i++] = (uint8_t)((planes[p] >> sq) & 1);

	// Quiet-ply counter, broadcast (raw count, <= DRAW_PLIES)
	for (uint64_t sq = 0; sq < CHECKERS_NUM_SQUARES; sq++)
		obs_out[i++] = (uint8_t)state[STATE_OFFSET_QUIET_PLIES];

	assert(i == CHECKERS_OBS_SIZE);
}

static void checkers_get_features(const uint64_t state[], uint64_t player,
                                  float* features_out) {
	assert(player < CHECKERS_NUM_PLAYERS);

	uint8_t obs[CHECKERS_OBS_SIZE];

	checkers_get_observation(state, player, obs);

	for (size_t i = 0; i < NUM_BIT_PLANES * CHECKERS_NUM_SQUARES; i++)
		features_out[i] = (float)obs[i];

	// Normalise the counter plane to [0, 1]
	for (size_t i = NUM_BIT_PLANES * CHECKERS_NUM_SQUARES; i < CHECKERS_OBS_SIZE;
	     i++)
		features_out[i] = (float)obs[i] / (float)DRAW_PLIES;
}

static const char man_chars[CHECKERS_NUM_PLAYERS]  = { 'b', 'w' };
static const char king_chars[CHECKERS_NUM_PLAYERS] = { 'B', 'W' };

static inline char square_char(const uint64_t state[], uint64_t row,
                               uint64_t col) {
	if ((row + col) % 2 != 0)
		return ' '; // light square: never occupied

	const uint64_t bit   = UINT64_C(1) << (row * CHECKERS_NUM_COLS + col);
	const bool king      = (state[STATE_OFFSET_KINGS] & bit) != 0;
	const uint64_t mover = state[STATE_OFFSET_CURRENT_PLAYER];

	for (uint64_t p = 0; p < CHECKERS_NUM_PLAYERS; p++)
		if (state[STATE_OFFSET_BOARDS + p] & bit) {
			// Slot p belongs to the p-th player after the mover
			const uint64_t owner =
			        (mover + p) % CHECKERS_NUM_PLAYERS;

			return king ? king_chars[owner] : man_chars[owner];
		}

	return '.';
}

static uint64_t checkers_to_string(const uint64_t state[], uint64_t buf_size,
                                   char* buf) {
	assert(buf != NULL);
	assert(buf_size >= CHECKERS_STRING_BUF_SIZE);

	char* const start     = buf;
	size_t remaining      = (size_t)buf_size;
	const uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];
	int n;

	n = snprintf(buf, remaining,
	             "Player %u (%c) to move (quiet plies %u/%u)\n",
	             (unsigned)(player + 1), man_chars[player],
	             (unsigned)state[STATE_OFFSET_QUIET_PLIES],
	             (unsigned)DRAW_PLIES);
	if (n > 0 && (size_t)n < remaining) {
		buf += n;
		remaining -= (size_t)n;
	}

	if (state[STATE_OFFSET_CONTINUATION]) {
		n = snprintf(buf, remaining,
		             "Must continue jumping from square %u\n",
		             (unsigned)__builtin_ctzll(
		                     state[STATE_OFFSET_CONTINUATION]));
		if (n > 0 && (size_t)n < remaining) {
			buf += n;
			remaining -= (size_t)n;
		}
	}

	for (uint64_t row = CHECKERS_NUM_ROWS; row-- > 0;) {
		n = snprintf(buf, remaining, "%u ", (unsigned)row);
		if (n > 0 && (size_t)n < remaining) {
			buf += n;
			remaining -= (size_t)n;
		}

		for (uint64_t col = 0; col < CHECKERS_NUM_COLS; col++) {
			n = snprintf(buf, remaining, " %c",
			             square_char(state, row, col));
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

	n = snprintf(buf, remaining, "  ");
	if (n > 0 && (size_t)n < remaining) {
		buf += n;
		remaining -= (size_t)n;
	}

	for (uint64_t col = 0; col < CHECKERS_NUM_COLS; col++) {
		n = snprintf(buf, remaining, " %u", (unsigned)col);
		if (n > 0 && (size_t)n < remaining) {
			buf += n;
			remaining -= (size_t)n;
		}
	}

	n = snprintf(buf, remaining, "\n");
	if (n > 0 && (size_t)n < remaining)
		buf += n;

	return (uint64_t)(buf - start);
}

// Two-level stringify, to embed macro values in string literals
#define STR_(x) #x
#define STR(x)  STR_(x)

static const char* checkers_help_prompt(void) {
	return "Checkers (English draughts)\n"
	       "===========================\n"
	       "\n"
	       "Men (b/w) move one square diagonally towards the opponent;\n"
	       "kings (B/W) move diagonally in any direction. Jump an\n"
	       "adjacent enemy piece to capture it - captures are mandatory,\n"
	       "and a piece that can keep jumping must (each jump is entered\n"
	       "as its own action). A man reaching the far row becomes a\n"
	       "king, which ends the move. A player with no legal move\n"
	       "loses; " STR(
	               DRAW_PLIES) " plies without a capture "
	                           "or man move is a draw.\n"
	                           "\n"
	                           "The board is stored and shown from the "
	                           "side to move's\n"
	                           "perspective: the mover's pieces always "
	                           "advance up the\n"
	                           "board, and the view rotates 180 degrees "
	                           "when the turn\n"
	                           "passes. Actions use this frame too, "
	                           "encoded as\n"
	                           "square * " STR(
	                                   CHECKERS_NUM_DIRS) " + direction, with "
	                                             "square = row * " STR(
	                                                     CHECKERS_NUM_COLS) " + "
	                                                               "column"
	                                                               "\n"
	                                                               "(row 0 "
	                                                               "at the "
	                                                               "bottom "
	                                                               "of the "
	                                                               "mover'"
	                                                               "s "
	                                                               "view) "
	                                                               "and "
	                                                               "directi"
	                                                               "ons\n"
	                                                               "0: "
	                                                               "up-"
	                                                               "left, "
	                                                               "1: "
	                                                               "up-"
	                                                               "right, "
	                                                               "2: "
	                                                               "down-"
	                                                               "left, "
	                                                               "3: "
	                                                               "down-"
	                                                               "right."
	                                                               "\n"
	                                                               "Men "
	                                                               "only "
	                                                               "ever "
	                                                               "use "
	                                                               "the "
	                                                               "forward"
	                                                               " direct"
	                                                               "ions 0 "
	                                                               "and "
	                                                               "1.\n";
}

const Game checkers = {
	.init               = checkers_init,
	.get_current_player = checkers_get_current_player,
	.is_chance_node     = checkers_is_chance_node,
	.get_valid_actions  = checkers_get_valid_actions,
	.apply_action       = checkers_apply_action,
	.is_terminal        = checkers_is_terminal,
	.get_outcome        = checkers_get_outcome,
	.get_observation    = checkers_get_observation,
	.get_features       = checkers_get_features,
	.to_string          = checkers_to_string,
	.help_prompt        = checkers_help_prompt,
	.obs_dims           = { CHECKERS_OBS_PLANES, CHECKERS_NUM_ROWS, CHECKERS_NUM_COLS },
	.features_dims      = { CHECKERS_OBS_PLANES, CHECKERS_NUM_ROWS, CHECKERS_NUM_COLS },
};
