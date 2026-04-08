#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include "pig.h"
#include "prng.h"

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	const Game* game = &pig;

	uint64_t state[PIG_STATE_SIZE]         = { 0 };
	uint64_t actions[PIG_MAX_NUM_ACTIONS]  = { 0 };
	char state_output[PIG_STRING_BUF_SIZE] = { 0 };

	// Seed the PRNG used for sampling chance actions. SplitMix64 is used
	// to bootstrap xoshiro256** from a single 64-bit seed, as recommended
	// by the xoshiro256** authors.
	uint64_t seed   = (uint64_t)time(NULL);
	uint64_t rng[4] = { 0 };
	for (size_t i = 0; i < 4; i++)
		rng[i] = splitmix64(&seed);

	printf("%s\n", game->help_prompt());

	game->init(NULL, state);
	game->to_string(state, PIG_STRING_BUF_SIZE, state_output);
	printf("\n%s\n", state_output);

	while (!game->is_terminal(state)) {
		uint64_t num_actions = game->get_valid_actions(state, actions);

		if (game->is_chance_node(state)) {
			// Sample a chance action uniformly at random
			uint64_t pick   = xoshiro256ss(rng) % num_actions;
			uint64_t action = actions[pick];

			printf("Chance event: sampled action %" PRIu64 "\n",
			       action);
			game->apply_action(state, action);

			game->to_string(state, PIG_STRING_BUF_SIZE,
			                state_output);
			printf("\n%s\n", state_output);
			continue;
		}

		uint64_t current_player = game->get_current_player(state);

		printf("Player %" PRIu64 "'s turn. Valid moves:",
		       current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++)
			printf(" %" PRIu64, actions[i]);
		printf("\n");

		uint64_t move;
		bool valid = false;

		while (!valid) {
			int scan_result = scanf("%" SCNu64, &move);
			if (scan_result == EOF) {
				fprintf(stderr, "\nEnd of input, exiting.\n");
				return 1;
			}
			if (scan_result != 1) {
				printf("Invalid input. Try again: ");
				int c;
				while ((c = getchar()) != '\n' && c != EOF)
					;
				if (c == EOF) {
					fprintf(stderr,
					        "\nEnd of input, exiting.\n");
					return 1;
				}
				continue;
			}

			for (uint64_t i = 0; i < num_actions; i++) {
				if (actions[i] == move) {
					valid = true;
					break;
				}
			}

			if (!valid)
				printf("Invalid move. Try again: ");
		}

		game->apply_action(state, move);

		game->to_string(state, PIG_STRING_BUF_SIZE, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[PIG_NUM_PLAYERS];
	game->get_outcome(state, scores);

	printf("Final scores:\n");
	for (size_t p = 0; p < PIG_NUM_PLAYERS; p++)
		printf("  Player %zu: %lld\n", p + 1, (long long)scores[p]);

	size_t best_player = 0;
	bool tied          = false;

	for (size_t p = 1; p < PIG_NUM_PLAYERS; p++) {
		if (scores[p] > scores[best_player]) {
			best_player = p;
			tied        = false;
		} else if (scores[p] == scores[best_player]) {
			tied = true;
		}
	}

	if (tied)
		printf("Draw!\n");
	else
		printf("Player %zu wins!\n", best_player + 1);

	return 0;
}
