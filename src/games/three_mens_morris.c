#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#include "games/three_mens_morris.h"
// Auto-generated from `generate_tmm_has_win_bit_array.c`
#include "tmm_has_win_bit_array.h"
#include "utils.h"

#define STATE_OFFSET_PHASE          0
#define STATE_OFFSET_CURRENT_PLAYER 1
#define STATE_OFFSET_BOARD          2

#define OBS_OFFSET_PHASE          (TMM_NUM_PLAYERS * TMM_BOARD_SIZE)
#define OBS_OFFSET_CURRENT_PLAYER (OBS_OFFSET_PHASE + 1)

#define PHASE_PLACEMENT 0
#define PHASE_MOVEMENT  1

#define FULL_BOARD ((1U << TMM_BOARD_SIZE) - 1U)

#define FROM_HAND TMM_BOARD_SIZE

#define NUM_PIECES_PER_PLAYER 3

// Just used for asserting that the encoded action is in the correct range
#define NUM_ENCODED_ACTIONS TMM_ACTION_SPACE_SIZE

static const char player_to_piece[] = { '.', 'O', 'X' };

/**
 * Using `Move.from == FROM_HAND` represents the piece is coming from
 * the player's hand i.e. is a "placement" move from the initial
 * placement phase
 */
typedef struct {
	uint8_t from;
	uint8_t to;
} Move;

STATIC_ASSERT(STATE_OFFSET_BOARD + TMM_NUM_PLAYERS == TMM_STATE_SIZE,
              state_layout_fills_state_size);
STATIC_ASSERT(TMM_BOARD_SIZE <= CHAR_BIT * sizeof(tmm_bitboard),
              bitboard_holds_every_square);
STATIC_ASSERT(FROM_HAND >= TMM_BOARD_SIZE, from_hand_is_not_a_square);

static inline uint64_t encode_move(Move move) {
	assert(move.from < TMM_BOARD_SIZE || move.from == FROM_HAND);
	assert(move.to < TMM_BOARD_SIZE);
	assert(move.from != move.to);

	const uint64_t action = move.from * TMM_BOARD_SIZE + move.to;

	assert(action < NUM_ENCODED_ACTIONS);
	return action;
}

static inline Move decode_action(uint64_t action) {
	assert(action < NUM_ENCODED_ACTIONS);

	const Move move = { .from = (uint8_t)(action / TMM_BOARD_SIZE),
	                    .to   = (uint8_t)(action % TMM_BOARD_SIZE) };

	assert(move.from < TMM_BOARD_SIZE || move.from == FROM_HAND);
	assert(move.to < TMM_BOARD_SIZE);
	assert(move.from != move.to);
	// Decoding inverts encoding
	assert(encode_move(move) == action);
	return move;
}

#ifndef NDEBUG
static void assert_state_is_valid(const uint64_t state[]) {
	assert(state != NULL);
	assert(state[STATE_OFFSET_PHASE] == PHASE_PLACEMENT ||
	       state[STATE_OFFSET_PHASE] == PHASE_MOVEMENT);
	assert(state[STATE_OFFSET_CURRENT_PLAYER] < TMM_NUM_PLAYERS);

	tmm_bitboard occupied = 0;
	size_t num_pieces     = 0;

	for (size_t p = 0; p < TMM_NUM_PLAYERS; p++) {
		const uint64_t board = state[STATE_OFFSET_BOARD + p];

		assert(board <= FULL_BOARD);
		// No square holds more than one piece
		assert((board & occupied) == 0);
		assert(__builtin_popcountll(board) <= NUM_PIECES_PER_PLAYER);

		occupied |= (tmm_bitboard)board;
		num_pieces += (size_t)__builtin_popcountll(board);
	}

	if (state[STATE_OFFSET_PHASE] == PHASE_PLACEMENT) {
		assert(num_pieces < TMM_NUM_PLAYERS * NUM_PIECES_PER_PLAYER);

		const uint64_t current_player =
		        state[STATE_OFFSET_CURRENT_PLAYER];
		const size_t completed_rounds = num_pieces / TMM_NUM_PLAYERS;

		/**
		 * During placement phase, there will be uneven amounts of
		 * pieces on the board, as each player takes their turn/ply
		 */
		for (size_t p = 0; p < TMM_NUM_PLAYERS; p++)
			assert((size_t)__builtin_popcountll(
			               state[STATE_OFFSET_BOARD + p]) ==
			       completed_rounds +
			               (p < current_player ? 1U : 0U));
	} else {
		for (size_t p = 0; p < TMM_NUM_PLAYERS; p++)
			assert(__builtin_popcountll(
			               state[STATE_OFFSET_BOARD + p]) ==
			       NUM_PIECES_PER_PLAYER);
	}
}
#else
#define assert_state_is_valid(state) ((void)0)
#endif

static void tmm_init(const void* config, uint64_t state[]) {
	(void)config; // Unused
	assert(state != NULL);

	// Initialise state
	state[STATE_OFFSET_PHASE]          = PHASE_PLACEMENT;
	state[STATE_OFFSET_CURRENT_PLAYER] = 0;

	for (size_t p = STATE_OFFSET_BOARD;
	     p < STATE_OFFSET_BOARD + TMM_NUM_PLAYERS; p++)
		state[p] = 0; // Empty bitboard

	assert_state_is_valid(state);
}

static uint64_t tmm_get_current_player(const uint64_t state[]) {
	assert_state_is_valid(state);
	return state[STATE_OFFSET_CURRENT_PLAYER];
}

static bool tmm_is_chance_node(const uint64_t state[]) {
	assert_state_is_valid(state);
	(void)state;
	return false;
}

static uint64_t tmm_get_valid_actions(const uint64_t state[],
                                      uint64_t actions_out[]) {
	assert_state_is_valid(state);
	assert(actions_out != NULL);

	tmm_bitboard occupied = 0;
	for (size_t p = 0; p < TMM_NUM_PLAYERS; p++)
		occupied |= (tmm_bitboard)state[STATE_OFFSET_BOARD + p];

	const tmm_bitboard empty = (tmm_bitboard)~occupied & FULL_BOARD;
	const tmm_bitboard sources =
	        state[STATE_OFFSET_PHASE] == PHASE_PLACEMENT
	                ? (tmm_bitboard)(1U << FROM_HAND)
	                : (tmm_bitboard)
	                          state[STATE_OFFSET_BOARD +
	                                state[STATE_OFFSET_CURRENT_PLAYER]];

	size_t action_count = 0;

	/**
	 * Go through the sources bitboard, lowest (rightmost) set bit/square
	 * each time.
	 * x &= x - 1 removes the lowest (rightmost) set bit each time
	 */
	for (tmm_bitboard s = sources; s; s &= s - 1) {
		const uint8_t from = (uint8_t)__builtin_ctz(s);

		// Same loop, for the empty, potential destination squares
		for (tmm_bitboard e = empty; e; e &= e - 1)
			actions_out[action_count++] = encode_move(
			        (Move){ .from = from,
				        .to   = (uint8_t)__builtin_ctz(e) });
	}

	assert(action_count == (size_t)__builtin_popcount(sources) *
	                               (size_t)__builtin_popcount(empty));
	assert(action_count > 0);
	assert(action_count <= TMM_MAX_NUM_ACTIONS);

	return action_count;
}

static bool tmm_is_terminal(const uint64_t state[]);

static void tmm_apply_action(uint64_t state[], uint64_t action) {
	assert_state_is_valid(state);
	assert(!tmm_is_terminal(state));

	const Move move       = decode_action(action);
	const uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];
	const bool placing    = state[STATE_OFFSET_PHASE] == PHASE_PLACEMENT;

	tmm_bitboard board = (tmm_bitboard)state[STATE_OFFSET_BOARD + player];
	const tmm_bitboard from = (tmm_bitboard)(1U << move.from);
	const tmm_bitboard to   = (tmm_bitboard)(1U << move.to);

	tmm_bitboard occupied = 0;
	for (size_t p = 0; p < TMM_NUM_PLAYERS; p++)
		occupied |= (tmm_bitboard)state[STATE_OFFSET_BOARD + p];

	assert((move.from == FROM_HAND) == placing);
	assert(placing || (board & from) != 0);
	assert((occupied & to) == 0);

	board = (tmm_bitboard)((board & ~from) | to);
	state[STATE_OFFSET_BOARD + player] = board;

	if (placing && __builtin_popcount(occupied) + 1 ==
	                       NUM_PIECES_PER_PLAYER * TMM_NUM_PLAYERS)
		state[STATE_OFFSET_PHASE] = PHASE_MOVEMENT;

	state[STATE_OFFSET_CURRENT_PLAYER] = (player + 1) % TMM_NUM_PLAYERS;

	assert_state_is_valid(state);
}

static bool tmm_is_terminal(const uint64_t state[]) {
	assert_state_is_valid(state);

	for (size_t player = 0; player < TMM_NUM_PLAYERS; player++)
		if (tmm_check_win(
		            (tmm_bitboard)state[STATE_OFFSET_BOARD + player]))
			return true;

	return false;
}

static void tmm_get_outcome(const uint64_t state[], int64_t scores_out[]) {
	assert_state_is_valid(state);
	assert(tmm_is_terminal(state));
	assert(scores_out != NULL);

	for (size_t player = 0; player < TMM_NUM_PLAYERS; player++) {
		if (tmm_check_win(
		            (tmm_bitboard)state[STATE_OFFSET_BOARD + player])) {
			// Three men's morris is a zero-sum game
			// i.e. For a win, there's only one winner - the
			// rest are losers
			for (size_t p = 0; p < TMM_NUM_PLAYERS; p++)
				scores_out[p] = (p == player) ? 1 : -1;
			return;
		}
	}

	/**
	 * Draws aren't actually possible in three men's morris. So given valid
	 * play, this code should be unreachable.
	 */
	for (size_t player = 0; player < TMM_NUM_PLAYERS; player++)
		scores_out[player] = 0;
}

static void tmm_get_observation(const uint64_t state[], uint64_t player,
                                uint8_t* obs_out) {
	assert_state_is_valid(state);
	assert(player < TMM_NUM_PLAYERS);
	assert(obs_out != NULL);

	for (size_t p = 0; p < TMM_NUM_PLAYERS; p++) {
		const uint64_t current_player = (player + p) % TMM_NUM_PLAYERS;
		const tmm_bitboard board      = (tmm_bitboard)
		        state[STATE_OFFSET_BOARD + current_player];

		for (size_t sq = 0; sq < TMM_BOARD_SIZE; sq++)
			obs_out[p * TMM_BOARD_SIZE + sq] =
			        (uint8_t)((board >> sq) & 1U);
	}

	obs_out[OBS_OFFSET_PHASE] = (uint8_t)state[STATE_OFFSET_PHASE];
	obs_out[OBS_OFFSET_CURRENT_PLAYER] =
	        (uint8_t)((state[STATE_OFFSET_CURRENT_PLAYER] +
		           TMM_NUM_PLAYERS - player) %
		          TMM_NUM_PLAYERS);
}

static void tmm_get_features(const uint64_t state[], uint64_t player,
                             float* features_out) {
	assert(features_out != NULL);

	uint8_t obs[TMM_OBS_SIZE];
	tmm_get_observation(state, player, obs);

	for (size_t i = 0; i < TMM_NUM_PLAYERS * TMM_BOARD_SIZE; i++)
		features_out[i] = (float)obs[i];

	for (size_t sq = 0; sq < TMM_BOARD_SIZE; sq++) {
		features_out[TMM_NUM_PLAYERS * TMM_BOARD_SIZE + sq] =
		        (float)obs[OBS_OFFSET_PHASE];
		features_out[(TMM_NUM_PLAYERS + 1) * TMM_BOARD_SIZE + sq] =
		        (float)obs[OBS_OFFSET_CURRENT_PLAYER];
	}
}

static uint64_t tmm_to_string(const uint64_t state[], uint64_t buf_size,
                              char* buf) {
	assert_state_is_valid(state);
	assert(buf != NULL);
	assert(buf_size >= TMM_STRING_BUF_SIZE);

	char pieces[TMM_BOARD_SIZE];
	for (size_t square = 0; square < TMM_BOARD_SIZE; square++) {
		pieces[square] = player_to_piece[0];
		for (size_t p = 0; p < TMM_NUM_PLAYERS; p++)
			if (state[STATE_OFFSET_BOARD + p] & (1U << square))
				pieces[square] = player_to_piece[p + 1];
	}

	const uint64_t player = state[STATE_OFFSET_CURRENT_PLAYER];
	const char* phase     = state[STATE_OFFSET_PHASE] == PHASE_PLACEMENT
	                                ? "placement"
	                                : "movement";

	return (uint64_t)snprintf(buf, (size_t)buf_size,
	                          "Player %u (%c) to move (%s phase)\n"
	                          "%c-----%c-----%c\n"
	                          "| \\   |   / |\n"
	                          "|   \\ | /   |\n"
	                          "%c-----%c-----%c\n"
	                          "|   / | \\   |\n"
	                          "| /   |   \\ |\n"
	                          "%c-----%c-----%c\n",
	                          (unsigned)(player + 1),
	                          player_to_piece[player + 1], phase, pieces[6],
	                          pieces[7], pieces[8], pieces[3], pieces[4],
	                          pieces[5], pieces[0], pieces[1], pieces[2]);
}

static const char* tmm_help_prompt(void) {
	return "Three Men's Morris\n"
	       "==================\n"
	       "\n"
	       "Players take turns placing one piece on an empty square until\n"
	       "each has placed three pieces. Then, on each turn, move one of\n"
	       "your pieces to any empty square; it need not be adjacent.\n"
	       "The first player to line up three pieces horizontally,\n"
	       "vertically, or diagonally wins, in either phase.\n"
	       "\n"
	       "Square indexes:\n"
	       "\n"
	       "6-----7-----8\n"
	       "| \\   |   / |\n"
	       "|   \\ | /   |\n"
	       "3-----4-----5\n"
	       "|   / | \\   |\n"
	       "| /   |   \\ |\n"
	       "0-----1-----2\n"
	       "\n"
	       "Actions are encoded as from * 9 + to. During placement,\n"
	       "from is 9 (the hand), so enter 81 + to (actions 81-89).\n"
	       "During movement, from is the square of one of your pieces\n"
	       "and to is an empty square. For example, moving from square\n"
	       "3 to square 5 is action 32.\n";
}

const Game three_mens_morris = {
	.init               = tmm_init,
	.get_current_player = tmm_get_current_player,
	.is_chance_node     = tmm_is_chance_node,
	.get_valid_actions  = tmm_get_valid_actions,
	.apply_action       = tmm_apply_action,
	.is_terminal        = tmm_is_terminal,
	.get_outcome        = tmm_get_outcome,
	.get_observation    = tmm_get_observation,
	.get_features       = tmm_get_features,
	.to_string          = tmm_to_string,
	.help_prompt        = tmm_help_prompt,
	.obs_dims           = { TMM_OBS_SIZE },
	.features_dims = { TMM_FEATURES_PLANES, TMM_NUM_ROWS, TMM_NUM_COLS },
};
