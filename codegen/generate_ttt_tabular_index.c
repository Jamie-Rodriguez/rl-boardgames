#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "agents/tabular_index.h"
#include "games/tic_tac_toe.h"

/**
 * Generates `ttt_code_to_index`; a lookup table that maps a "position
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
 * Unreachable or terminal states map to `INDEX_NONE`, to indicate that
 * they do not exist in the corresponding lookup table.
 *
 * `ttt_code_to_index` only includes mappings for the **reachable**
 * states. This way the corresponding lookup table can be a densely-
 * populated lookup table of states (usually containing the estimated
 * values of the states, or a similar scheme) with no wasted space.
 *
 * Using this scheme, we reduce the lookup table from a naïve table,
 * enumerating all possible base-3 configurations = 3^9 = 19683, to
 * only the reachable (incomplete) states = 4520
 */

/**
 * Index of a position/state that is either unreachable, or terminal.
 * It does not have an entry in the corresponding lookup table.
 */
#define INDEX_NONE UINT16_C(0xFFFF)

#define STACK_CAPACITY (1 + TTT_MAX_TURNS * TTT_MAX_NUM_DECISION_ACTIONS)

typedef struct Stack {
	uint64_t states[STACK_CAPACITY][TTT_STATE_SIZE];
	size_t size;
} Stack;

typedef enum StateExplorationStatus {
	STATE_UNSEEN   = 0,
	STATE_TERMINAL = 1,
	STATE_IN_PLAY  = 2,
} StateExplorationStatus;

static void push(Stack* stack, const uint64_t state[]) {
	assert(stack->size < STACK_CAPACITY);
	memcpy(stack->states[stack->size++], state, sizeof(stack->states[0]));
}

/* Visits every code the game can reach, classifying each code in `statuses` */
static void walk(Stack* stack, StateExplorationStatus statuses[]) {
	uint64_t state[TTT_STATE_SIZE];

	tic_tac_toe.init(NULL, state);
	assert(!tic_tac_toe.is_terminal(state));
	push(stack, state);

	while (stack->size > 0) {
		// Only positions a player moves in are ever pushed
		memcpy(state, stack->states[--stack->size], sizeof(state));

		float features[TTT_FEATURES_SIZE];

		tic_tac_toe.get_features(
		        state, tic_tac_toe.get_current_player(state), features);

		const uint64_t code = ttt_features_to_code(features);

		if (statuses[code] == STATE_IN_PLAY)
			continue; // reached again, via another move order

		/**
		 * Terminal positions are never pushed, so a code already filed
		 * as terminal cannot come back off the stack
		 */
		assert(statuses[code] == STATE_UNSEEN);

		statuses[code] = STATE_IN_PLAY;

		uint64_t actions[TTT_MAX_NUM_DECISION_ACTIONS];
		const uint64_t num_actions =
		        tic_tac_toe.get_valid_actions(state, actions);

		for (size_t action = 0; action < num_actions; action++) {
			uint64_t child[TTT_STATE_SIZE];

			memcpy(child, state, sizeof(child));
			tic_tac_toe.apply_action(child, actions[action]);

			/**
			 * A terminal state is recorded but gets neither an
			 * index nor an expansion
			 */
			if (tic_tac_toe.is_terminal(child)) {
				float child_features[TTT_FEATURES_SIZE];

				tic_tac_toe.get_features(
				        child,
				        tic_tac_toe.get_current_player(child),
				        child_features);

				const uint64_t child_code =
				        ttt_features_to_code(child_features);

				/* Reached once per parent that can
				 * end there, so this runs several
				 * times per code; the write is
				 * idempotent */
				assert(statuses[child_code] != STATE_IN_PLAY);
				statuses[child_code] = STATE_TERMINAL;
			} else {
				push(stack, child);
			}
		}
	}
}

int main(void) {
	Stack stack = { 0 };

	StateExplorationStatus statuses[TTT_TABULAR_NUM_CODES] = {
		STATE_UNSEEN
	};

	walk(&stack, statuses);

	/* Number the survivors in ascending code order */
	uint16_t indices[TTT_TABULAR_NUM_CODES];
	/* Distinct codes reached, terminal positions included */
	uint64_t num_positions_seen = 0;
	/* Distinct codes reached, terminal positions *excluded* */
	uint64_t num_reachable_states = 0;

	for (uint64_t code = 0; code < TTT_TABULAR_NUM_CODES; code++) {
		if (statuses[code] != STATE_UNSEEN)
			num_positions_seen++;

		indices[code] = statuses[code] == STATE_IN_PLAY
		                        ? (uint16_t)num_reachable_states++
		                        : INDEX_NONE;
	}

	/**
	 * The walk enumerates the same positions that the game header counts,
	 * so a disagreement means the code encoding has started merging or
	 * splitting positions
	 */
	if (num_positions_seen != TTT_STATE_SPACE_SIZE) {
		fprintf(stderr,
		        "generate_ttt_tabular_index: walked %llu "
		        "positions, but TTT_STATE_SPACE_SIZE is %llu\n",
		        (unsigned long long)num_positions_seen,
		        (unsigned long long)TTT_STATE_SPACE_SIZE);
		return 1;
	}

	if (num_reachable_states != TTT_TABULAR_NUM_STATES) {
		fprintf(stderr,
		        "generate_ttt_tabular_index: found %llu states, but "
		        "TTT_TABULAR_NUM_STATES is %llu\n",
		        (unsigned long long)num_reachable_states,
		        (unsigned long long)TTT_TABULAR_NUM_STATES);
		return 1;
	}

	if (num_reachable_states >= INDEX_NONE) {
		fprintf(stderr,
		        "generate_ttt_tabular_index: %llu states do not fit a "
		        "uint16_t index\n",
		        (unsigned long long)num_reachable_states);
		return 1;
	}

	printf("/**\n");
	printf(" * This file was generated by "
	       "`generate_ttt_tabular_index.c`\n");
	printf(" */\n\n");

	printf("#ifndef TTT_TABULAR_INDEX_H\n");
	printf("#define TTT_TABULAR_INDEX_H\n\n");

	printf("#include <stdint.h>\n\n");

	printf("#include \"agents/tabular_index.h\"\n\n");

	printf("/* Index to indicate an unreachable state */\n");
	printf("#define TTT_TABULAR_INDEX_NONE UINT16_C(0x%04X)\n\n",
	       INDEX_NONE);

	printf("/* The table below was built against these two constants "
	       "*/\n");
	printf("#if TTT_TABULAR_NUM_CODES != %llu\n",
	       (unsigned long long)TTT_TABULAR_NUM_CODES);
	printf("#error \"agents/tabular_index.h changed shape: re-run "
	       "generate_ttt_tabular_index\"\n");
	printf("#endif\n");
	printf("#if TTT_TABULAR_NUM_STATES != %llu\n",
	       (unsigned long long)num_reachable_states);
	printf("#error \"agents/tabular_index.h changed shape: re-run "
	       "generate_ttt_tabular_index\"\n");
	printf("#endif\n\n");

	printf("/**\n");
	printf(" * Position, encoded as a base-3 number -> state index.\n");
	printf(" * TTT_TABULAR_INDEX_NONE is returned for an unreachable\n");
	printf(" * state. %llu of the %llu codes are indexed\n",
	       (unsigned long long)num_reachable_states,
	       (unsigned long long)TTT_TABULAR_NUM_CODES);
	printf(" */\n");
	printf("static const uint16_t "
	       "ttt_code_to_index[TTT_TABULAR_NUM_CODES] = {\n");

	const unsigned int codes_per_line = 10;
	for (uint64_t code = 0; code < TTT_TABULAR_NUM_CODES;
	     code += codes_per_line) {
		printf("\t");

		for (uint64_t c = code;
		     c < code + codes_per_line && c < TTT_TABULAR_NUM_CODES;
		     c++)
			printf("%5u%s", indices[c],
			       c + 1 < TTT_TABULAR_NUM_CODES ? "," : "");

		printf("\n");
	}

	printf("};\n\n");

	printf("#endif\n");

	return 0;
}
