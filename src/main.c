#include <stdio.h>
#include <stdint.h>
#include "tic_tac_toe.h"

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	const Game* ttt = &tic_tac_toe;

	uint64_t state[ttt->state_size];
	uint64_t actions[ttt->max_actions];
	char state_output[ttt->string_buf_size];

	ttt->init(NULL, state);

	while (!ttt->is_terminal(state)) {
		uint64_t current_player = ttt->get_current_player(state);
		uint64_t num_actions = ttt->get_valid_actions(state, actions);

		printf("Player %llu's turn. Valid moves:", current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++)
			printf(" %llu", actions[i]);
		printf("\n");

		uint64_t move;
		scanf("%llu", &move);

		ttt->apply_action(state, move);

		ttt->to_string(state, ttt->string_buf_size, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[ttt->num_players];
	ttt->get_outcome(state, scores);

	if (scores[0] == 1)
		printf("Player 1 wins!\n");
	else if (scores[1] == 1)
		printf("Player 2 wins!\n");
	else
		printf("Draw!\n");

	return 0;
}
