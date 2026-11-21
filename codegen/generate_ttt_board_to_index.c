#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "games/tic_tac_toe.h"

/**
 * Generates `ttt_board_to_index`; a lookup table that maps a "position
 * code" to an index in a lookup table.
 *
 * Where
 *
 * A "position code" is a base-3 representation of a tic-tac-toe board
 * where each base-3 digit corresponds to a square on the board e.g.
 *
 * 	   |   | 2
 * 	---+---+---
 * 	 1 |   |
 * 	---+---+---
 * 	 1 |   | 2
 *
 * (0 = empty square, 1 = player 1, 2 = player 2)
 *
 * Equals (in base-3): 200001201 = 13168 (base-10)
 *
 * Unreachable states map to `INDEX_NONE`, to indicate that they do not
 * exist in the corresponding lookup table.
 *
 * `ttt_board_to_index` only includes mappings to the **reachable**
 * states, terminal ones included. This way the corresponding lookup
 * table can be a densely-populated lookup table of states (usually
 * containing the estimated values of the states, or a similar scheme)
 * with no wasted space, and a row for every position a move can lead
 * to.
 *
 * This lookup also incorporates "canonicalisation"; which is accounting
 * for symmetrical board states - i.e. any combination of rotations and
 * reflections.
 *
 * (An entry of `ttt_board_to_index` is therefore technically the index of the
 * code's class.)
 *
 * Using this scheme, we reduce the lookup table from a naïve table,
 * enumerating all possible base-3 configurations = 3^9 = 19683, to only the
 * reachable states = 5478, reduced via canonicalisation to 765.
 */

/**
 * NOTE: The macros and functions below are both required by this generator,
 * and also by the generated file "ttt_board_to_index.h". This way consumers
 * of the generated file can have the relevant mapping functions provided in
 * the same place as the lookup table.
 * Unfortunately, in C99 there's not a nice way to do this, so for now these
 * macros/functions are literally copied into the generated header (see
 * main()).
 * Therefore, if you update any of these macros or functions, be sure to
 * update them in main() also.
 * 	- TTT_BOARD_INDEX_SQUARE_STATES
 * 	- TTT_BOARD_INDEX_NUM_CODES
 * 	- ttt_features_to_code
 * 	- ttt_afterstate_to_code
 */
#define TTT_BOARD_INDEX_SQUARE_STATES (TTT_NUM_PLAYERS + 1)

#define TTT_BOARD_INDEX_NUM_CODES                                        \
	(TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES)

static inline uint64_t ttt_features_to_code(const float features[]) {
	assert(features != NULL);

	uint64_t code        = 0;
	uint64_t place_value = 1;

	for (size_t square = 0; square < TTT_BOARD_SIZE; square++) {
		uint64_t digit = 0;

		for (size_t player = 0; player < TTT_NUM_PLAYERS; player++) {
			const float feature =
			        features[player * TTT_BOARD_SIZE + square];

			assert(feature == 0.0f || feature == 1.0f);

			if (feature != 0.0f) {
				assert(digit == 0); // one piece per square
				digit = (uint64_t)(player + 1);
			}
		}

		code += digit * place_value;
		place_value *= TTT_BOARD_INDEX_SQUARE_STATES;
	}

	assert(code < TTT_BOARD_INDEX_NUM_CODES);
	return code;
}

static inline uint64_t ttt_afterstate_to_code(const float features[],
                                              uint64_t action) {
	assert(features != NULL);
	assert(action < TTT_BOARD_SIZE);

	float afterstate[TTT_FEATURES_SIZE];

	for (size_t plane = 0; plane < TTT_NUM_PLAYERS; plane++) {
		const size_t movers_plane = (plane + 1) % TTT_NUM_PLAYERS;
		const float* from = features + movers_plane * TTT_BOARD_SIZE;
		float* to         = afterstate + plane * TTT_BOARD_SIZE;

		for (size_t square = 0; square < TTT_BOARD_SIZE; square++)
			to[square] = from[square];

		assert(to[action] == 0.0f); // the square was empty
	}

	afterstate[(TTT_NUM_PLAYERS - 1) * TTT_BOARD_SIZE + action] = 1.0f;

	return ttt_features_to_code(afterstate);
}

/**
 * Indicates a position/state that is unreachable i.e. it does not have an
 * entry in the corresponding lookup table.
 */
#define INDEX_NONE UINT16_C(0xFFFF)

#define STACK_CAPACITY (1 + TTT_MAX_TURNS * TTT_MAX_NUM_DECISION_ACTIONS)

#if TTT_NUM_ROWS != TTT_NUM_COLS
#error "TTT_BOARD_INDEX_NUM_SYMMETRIES requires a square board (quarter turns)"
#endif
#define NUM_ROTATIONS                  4
#define TTT_BOARD_INDEX_NUM_SYMMETRIES (2 * NUM_ROTATIONS)

typedef struct Stack {
	uint64_t states[STACK_CAPACITY][TTT_STATE_SIZE];
	size_t size;
} Stack;

typedef enum StateExplorationStatus {
	STATE_UNSEEN   = 0,
	STATE_TERMINAL = 1,
	STATE_IN_PLAY  = 2,
} StateExplorationStatus;

static inline void push(Stack* stack, const uint64_t state[]) {
	assert(stack->size < STACK_CAPACITY);
	memcpy(stack->states[stack->size++], state, sizeof(stack->states[0]));
}

static inline void pop(Stack* stack, uint64_t state[]) {
	assert(stack->size > 0);
	memcpy(state, stack->states[--stack->size], sizeof(stack->states[0]));
}

/**
 * Marks every code the game can reach in `statuses`, as "in-play" or
 * "terminal"; unreachable codes stay unseen
 */
static void mark_reachable_codes(Stack* stack,
                                 StateExplorationStatus statuses[]) {
	uint64_t state[TTT_STATE_SIZE];

	tic_tac_toe.init(NULL, state);
	assert(!tic_tac_toe.is_terminal(state));
	push(stack, state);

	while (stack->size > 0) {
		// Only positions a player moves in are ever pushed
		pop(stack, state);

		float features[TTT_FEATURES_SIZE];

		tic_tac_toe.get_features(
		        state, tic_tac_toe.get_current_player(state), features);

		const uint64_t code = ttt_features_to_code(features);

		if (statuses[code] == STATE_IN_PLAY)
			continue; // reached again, via a different move order

		assert(statuses[code] == STATE_UNSEEN);

		statuses[code] = STATE_IN_PLAY;

		uint64_t actions[TTT_MAX_NUM_DECISION_ACTIONS];
		const uint64_t num_actions =
		        tic_tac_toe.get_valid_actions(state, actions);

		for (size_t action = 0; action < num_actions; action++) {
			const uint64_t square = actions[action];
			uint64_t child_state[TTT_STATE_SIZE];
			float child_features[TTT_FEATURES_SIZE];

			memcpy(child_state, state, sizeof(child_state));
			tic_tac_toe.apply_action(child_state, square);

			tic_tac_toe.get_features(
			        child_state,
			        tic_tac_toe.get_current_player(child_state),
			        child_features);

			const uint64_t child_code =
			        ttt_features_to_code(child_features);

			assert(ttt_afterstate_to_code(features, square) ==
			       child_code);

			if (tic_tac_toe.is_terminal(child_state)) {
				assert(statuses[child_code] != STATE_IN_PLAY);
				statuses[child_code] = STATE_TERMINAL;
			} else {
				push(stack, child_state);
			}
		}
	}
}

/* f after g: the piece on square q lands on f[g[q]] */
static inline void compose(const uint8_t f[], const uint8_t g[],
                           uint8_t out[]) {
	for (size_t square = 0; square < TTT_BOARD_SIZE; square++)
		out[square] = f[g[square]];
}

/**
 * Returns index of `mapping` among the symmetries
 * Returns `NUM_SYMMETRIES` if not in `symmetry_square`
 */
static inline size_t
get_symmetry(const uint8_t symmetry_square[][TTT_BOARD_SIZE],
             const uint8_t mapping[]) {
	for (size_t s = 0; s < TTT_BOARD_INDEX_NUM_SYMMETRIES; s++)
		if (memcmp(symmetry_square[s], mapping,
		           sizeof(symmetry_square[s])) == 0)
			return s;

	return TTT_BOARD_INDEX_NUM_SYMMETRIES;
}

/**
 * Populates `symmetry_square` with the symmetries of the board, as mappings
 * to destination squares.
 * Symmetry `s` maps piece on square `q` to square `symmetry_square[s][q]`
 *
 * Symmetry 0 is the identity
 */
static void build_symmetries(uint8_t symmetry_square[][TTT_BOARD_SIZE]) {
	uint8_t quarter_turn[TTT_BOARD_SIZE]; // (Anticlockwise)
	uint8_t mirror[TTT_BOARD_SIZE];

	for (size_t row = 0; row < TTT_NUM_ROWS; row++) {
		for (size_t col = 0; col < TTT_NUM_COLS; col++) {
			const uint64_t square = TTT_ACTION(row, col);

			quarter_turn[square] = (uint8_t)TTT_ACTION(
			        col, TTT_NUM_ROWS - 1 - row);
			mirror[square] = (uint8_t)TTT_ACTION(
			        row, TTT_NUM_COLS - 1 - col);
		}
	}

	for (size_t square = 0; square < TTT_BOARD_SIZE; square++)
		symmetry_square[0][square] = (uint8_t)square;

	for (size_t turns = 1; turns < NUM_ROTATIONS; turns++)
		compose(quarter_turn, symmetry_square[turns - 1],
		        symmetry_square[turns]);

	for (size_t turns = 0; turns < NUM_ROTATIONS; turns++)
		compose(symmetry_square[turns], mirror,
		        symmetry_square[NUM_ROTATIONS + turns]);

	/**
	 * Sanity check:
	 * 	Each symmetry is unique,
	 * 	and that for each symmetry, each square maps to a unique index
	 */
	for (size_t s = 0; s < TTT_BOARD_INDEX_NUM_SYMMETRIES; s++) {
		bool landed_on[TTT_BOARD_SIZE] = { false };

		for (size_t square = 0; square < TTT_BOARD_SIZE; square++) {
			const uint8_t to = symmetry_square[s][square];

			assert(to < TTT_BOARD_SIZE);
			assert(!landed_on[to]);
			landed_on[to] = true;
		}

		assert(get_symmetry(symmetry_square, symmetry_square[s]) == s);
	}

	// Check every member has an inverse
	for (size_t a = 0; a < TTT_BOARD_INDEX_NUM_SYMMETRIES; a++) {
		bool inverted = false;

		for (size_t b = 0; b < TTT_BOARD_INDEX_NUM_SYMMETRIES; b++) {
			uint8_t product[TTT_BOARD_SIZE];

			compose(symmetry_square[a], symmetry_square[b],
			        product);

			const size_t c = get_symmetry(symmetry_square, product);

			assert(c < TTT_BOARD_INDEX_NUM_SYMMETRIES);

			if (c == 0) { // b should undo a
				assert(!inverted);
				inverted = true;
			}
		}

		// If not, there's been a problem calculating symmetries
		assert(inverted);
	}
}

static void code_to_features(uint64_t code, float features[]) {
	assert(code < TTT_BOARD_INDEX_NUM_CODES);

	for (size_t square = 0; square < TTT_BOARD_SIZE; square++) {
		const uint64_t digit = code % TTT_BOARD_INDEX_SQUARE_STATES;

		code /= TTT_BOARD_INDEX_SQUARE_STATES;

		for (size_t player = 0; player < TTT_NUM_PLAYERS; player++)
			features[player * TTT_BOARD_SIZE + square] =
			        (digit == player + 1) ? 1.0f : 0.0f;
	}
}

/* Maps `features` to a new set of features, via `symmetry` */
static void apply_symmetry(const float features[], const uint8_t symmetry[],
                           float out[]) {
	for (size_t p = 0; p < TTT_NUM_PLAYERS; p++)
		for (size_t square = 0; square < TTT_BOARD_SIZE; square++)
			out[p * TTT_BOARD_SIZE + symmetry[square]] =
			        features[p * TTT_BOARD_SIZE + square];
}

/**
 * The canonical form of a position is the smallest code among its
 * images under the symmetries
 */
static uint64_t
canonical_code(uint64_t code, const uint8_t symmetry_square[][TTT_BOARD_SIZE]) {
	float features[TTT_FEATURES_SIZE];
	float transformed[TTT_FEATURES_SIZE];

	code_to_features(code, features);

	uint64_t canonical = TTT_BOARD_INDEX_NUM_CODES;

	// Try all symmetry transformations, and return the smallest code
	for (size_t s = 0; s < TTT_BOARD_INDEX_NUM_SYMMETRIES; s++) {
		apply_symmetry(features, symmetry_square[s], transformed);
		const uint64_t transformed_code =
		        ttt_features_to_code(transformed);

		assert(s > 0 || transformed_code == code);

		if (transformed_code < canonical)
			canonical = transformed_code;
	}

	assert(canonical <= code);
	return canonical;
}

/**
 * Numbers the classes of the codes filed under `status`, in ascending
 * code order from `first_index`, and returns the index after the last
 * one handed out. Each such code gets the index of its class in
 * `classes`
 */
static uint64_t number_classes(const StateExplorationStatus statuses[],
                               StateExplorationStatus status,
                               const uint8_t symmetry_square[][TTT_BOARD_SIZE],
                               uint16_t classes[], uint64_t first_index) {
	uint64_t next_index = first_index;

	for (uint64_t code = 0; code < TTT_BOARD_INDEX_NUM_CODES; code++) {
		if (statuses[code] != status)
			continue;

		const uint64_t canonical =
		        canonical_code(code, symmetry_square);

		assert(statuses[canonical] == status);

		classes[code] = canonical == code ? (uint16_t)next_index++
		                                  : classes[canonical];
	}

	return next_index;
}

int main(void) {
	Stack stack = { 0 };

	StateExplorationStatus statuses[TTT_BOARD_INDEX_NUM_CODES] = {
		STATE_UNSEEN
	};

	mark_reachable_codes(&stack, statuses);

	// Distinct codes reached, terminal positions included
	uint64_t num_positions_seen = 0;

	for (uint64_t code = 0; code < TTT_BOARD_INDEX_NUM_CODES; code++)
		if (statuses[code] != STATE_UNSEEN)
			num_positions_seen++;

	/**
	 * The codes marked should be the same positions that the game header
	 * specifies, so a disagreement must mean that the encoding has
	 * started merging or splitting positions
	 */
	if (num_positions_seen != TTT_STATE_SPACE_SIZE) {
		fprintf(stderr,
		        "generate_ttt_board_to_index: reached %llu "
		        "positions, but TTT_STATE_SPACE_SIZE is %llu\n",
		        (unsigned long long)num_positions_seen,
		        (unsigned long long)TTT_STATE_SPACE_SIZE);
		return 1;
	}

	uint8_t symmetry_square[TTT_BOARD_INDEX_NUM_SYMMETRIES][TTT_BOARD_SIZE];
	build_symmetries(symmetry_square);

	// Only the *reachable* codes' entries are read
	uint16_t classes[TTT_BOARD_INDEX_NUM_CODES];

	// The classes that a player can move in come first,
	// then the terminal ones
	const uint64_t num_playable_states = number_classes(
	        statuses, STATE_IN_PLAY, symmetry_square, classes, 0);
	const uint64_t num_states =
	        number_classes(statuses, STATE_TERMINAL, symmetry_square,
		               classes, num_playable_states);

	if (num_states >= INDEX_NONE) {
		fprintf(stderr,
		        "generate_ttt_board_to_index: %llu classes do not fit "
		        "a uint16_t entry\n",
		        (unsigned long long)num_states);
		return 1;
	}

	printf("/**\n");
	printf(" * This file was generated by "
	       "`generate_ttt_board_to_index.c`\n");
	printf(" */\n\n");

	printf("#ifndef TTT_BOARD_TO_INDEX_H\n");
	printf("#define TTT_BOARD_TO_INDEX_H\n\n");

	printf("#include <assert.h>\n");
	printf("#include <stddef.h>\n");
	printf("#include <stdint.h>\n\n");

	printf("#include \"games/tic_tac_toe.h\"\n\n");

	printf("/* Entry to indicate an unreachable state */\n");
	printf("#define TTT_BOARD_TO_INDEX_NONE UINT16_C(0x%04X)\n\n",
	       INDEX_NONE);

	printf("#define TTT_BOARD_INDEX_NUM_STATES %llu\n",
	       (unsigned long long)num_states);
	printf("#define TTT_BOARD_INDEX_NUM_DECISION_STATES %llu\n\n",
	       (unsigned long long)num_playable_states);

	printf("#define TTT_BOARD_INDEX_SQUARE_STATES (TTT_NUM_PLAYERS + 1)\n");
	printf("\n");
	printf("#define TTT_BOARD_INDEX_NUM_CODES                              "
	       "          \\\n");
	printf("\t(TTT_BOARD_INDEX_SQUARE_STATES * "
	       "TTT_BOARD_INDEX_SQUARE_STATES * \\\n");
	printf("\t TTT_BOARD_INDEX_SQUARE_STATES * "
	       "TTT_BOARD_INDEX_SQUARE_STATES * \\\n");
	printf("\t TTT_BOARD_INDEX_SQUARE_STATES * "
	       "TTT_BOARD_INDEX_SQUARE_STATES * \\\n");
	printf("\t TTT_BOARD_INDEX_SQUARE_STATES * "
	       "TTT_BOARD_INDEX_SQUARE_STATES * \\\n");
	printf("\t TTT_BOARD_INDEX_SQUARE_STATES)\n");
	printf("\n");

	printf("/* The table below was built against this constant */\n");
	printf("#if TTT_BOARD_INDEX_NUM_CODES != %llu\n",
	       (unsigned long long)TTT_BOARD_INDEX_NUM_CODES);
	printf("#error \"games/tic_tac_toe.h changed shape: re-run "
	       "generate_ttt_board_to_index\"\n");
	printf("#endif\n\n");

	printf("static inline uint64_t ttt_features_to_code(const float "
	       "features[]) {\n");
	printf("\tassert(features != NULL);\n");
	printf("\n");
	printf("\tuint64_t code        = 0;\n");
	printf("\tuint64_t place_value = 1;\n");
	printf("\n");
	printf("\tfor (size_t square = 0; square < TTT_BOARD_SIZE; square++) "
	       "{\n");
	printf("\t\tuint64_t digit = 0;\n");
	printf("\n");
	printf("\t\tfor (size_t player = 0; player < TTT_NUM_PLAYERS; "
	       "player++) {\n");
	printf("\t\t\tconst float feature =\n");
	printf("\t\t\t        features[player * TTT_BOARD_SIZE + square];\n");
	printf("\n");
	printf("\t\t\tassert(feature == 0.0f || feature == 1.0f);\n");
	printf("\n");
	printf("\t\t\tif (feature != 0.0f) {\n");
	printf("\t\t\t\tassert(digit == 0); // one piece per square\n");
	printf("\t\t\t\tdigit = (uint64_t)(player + 1);\n");
	printf("\t\t\t}\n");
	printf("\t\t}\n");
	printf("\n");
	printf("\t\tcode += digit * place_value;\n");
	printf("\t\tplace_value *= TTT_BOARD_INDEX_SQUARE_STATES;\n");
	printf("\t}\n");
	printf("\n");
	printf("\tassert(code < TTT_BOARD_INDEX_NUM_CODES);\n");
	printf("\treturn code;\n");
	printf("}\n");
	printf("\n");
	printf("static inline uint64_t ttt_afterstate_to_code(const float "
	       "features[],\n");
	printf("                                              uint64_t action) "
	       "{\n");
	printf("\tassert(features != NULL);\n");
	printf("\tassert(action < TTT_BOARD_SIZE);\n");
	printf("\n");
	printf("\tfloat afterstate[TTT_FEATURES_SIZE];\n");
	printf("\n");
	printf("\tfor (size_t plane = 0; plane < TTT_NUM_PLAYERS; plane++) "
	       "{\n");
	printf("\t\tconst size_t movers_plane = (plane + 1) %% "
	       "TTT_NUM_PLAYERS;\n");
	printf("\t\tconst float* from = features + movers_plane * "
	       "TTT_BOARD_SIZE;\n");
	printf("\t\tfloat* to         = afterstate + plane * "
	       "TTT_BOARD_SIZE;\n");
	printf("\n");
	printf("\t\tfor (size_t square = 0; square < TTT_BOARD_SIZE; "
	       "square++)\n");
	printf("\t\t\tto[square] = from[square];\n");
	printf("\n");
	printf("\t\tassert(to[action] == 0.0f); // the square was empty\n");
	printf("\t}\n");
	printf("\n");
	printf("\tafterstate[(TTT_NUM_PLAYERS - 1) * TTT_BOARD_SIZE + action] "
	       "= 1.0f;\n");
	printf("\n");
	printf("\treturn ttt_features_to_code(afterstate);\n");
	printf("}\n");
	printf("\n");

	printf("/**\n");
	printf(" * Position, encoded as a base-3 number -> class index.\n");
	printf(" * Maps to TTT_BOARD_TO_INDEX_NONE (%llu) for an\n",
	       (unsigned long long)INDEX_NONE);
	printf(" * unreachable state.\n");
	printf(" * Only %llu of the %llu codes are reachable.\n",
	       (unsigned long long)num_positions_seen,
	       (unsigned long long)TTT_BOARD_INDEX_NUM_CODES);
	printf(" * Those %llu reachable states are further reduced to %llu\n",
	       (unsigned long long)num_positions_seen,
	       (unsigned long long)num_states);
	printf(" * canonical states.\n");
	printf(" */\n");
	printf("static const uint16_t "
	       "ttt_board_to_index[TTT_BOARD_INDEX_NUM_CODES] = {\n");

	const unsigned int codes_per_line = 10;
	for (uint64_t code = 0; code < TTT_BOARD_INDEX_NUM_CODES;
	     code += codes_per_line) {
		printf("\t");

		for (uint64_t c = code;
		     c < code + codes_per_line && c < TTT_BOARD_INDEX_NUM_CODES;
		     c++) {
			const uint16_t index = statuses[c] != STATE_UNSEEN
			                               ? classes[c]
			                               : INDEX_NONE;

			printf("%5u%s", index,
			       c + 1 < TTT_BOARD_INDEX_NUM_CODES ? "," : "");
		}

		printf("\n");
	}

	printf("};\n\n");

	printf("static inline uint64_t ttt_afterstate_to_index(const float "
	       "features[],\n");
	printf("                                               uint64_t "
	       "action) {\n");
	printf("\tconst uint64_t index =\n");
	printf("\t        ttt_board_to_index[ttt_afterstate_to_code(features, "
	       "action)];\n");
	printf("\n");
	printf("\tassert(index != TTT_BOARD_TO_INDEX_NONE);\n");
	printf("\n");
	printf("\treturn index;\n");
	printf("}\n");
	printf("\n");

	printf("#endif\n");

	return 0;
}
