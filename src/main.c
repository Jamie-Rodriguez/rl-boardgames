#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "tic_tac_toe.h"

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	const Game* ttt = &tic_tac_toe;

	uint64_t state[TTT_STATE_SIZE]         = { 0 };
	uint64_t actions[TTT_MAX_NUM_ACTIONS]  = { 0 };
	char state_output[TTT_STRING_BUF_SIZE] = { 0 };

	ttt->init(NULL, state);

	while (!ttt->is_terminal(state)) {
		uint64_t current_player = ttt->get_current_player(state);
		uint64_t num_actions = ttt->get_valid_actions(state, actions);

		printf("Player %llu's turn. Valid moves:", current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++)
			printf(" %llu", actions[i]);
		printf("\n");

		uint64_t move;
		bool valid = false;

		while (!valid) {
			if (scanf("%llu", &move) != 1) {
				printf("Invalid input. Try again: ");
				while (getchar() != '\n')
					;
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

		ttt->apply_action(state, move);

		ttt->to_string(state, TTT_STRING_BUF_SIZE, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[TTT_NUM_PLAYERS];
	ttt->get_outcome(state, scores);

	printf("Final scores:\n");
	for (size_t p = 0; p < TTT_NUM_PLAYERS; p++)
		printf("  Player %zu: %lld\n", p + 1, (long long)scores[p]);

	size_t best_player = 0;
	bool tied          = false;

	for (size_t p = 1; p < TTT_NUM_PLAYERS; p++) {
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
