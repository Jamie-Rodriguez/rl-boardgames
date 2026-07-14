#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <inttypes.h>
#include "board_game.h"
#include "prng.h"
#include "tic_tac_toe.h"
#include "pig.h"
#include "blackjack.h"
#include "connect4.h"

static int handle_args(int argc, char* argv[], const Game* game) {
	if (argc <= 1)
		return -1;

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		printf("%s\n", game->help_prompt());
		return 0;
	}

	fprintf(stderr, "Unknown argument: %s\nUsage: %s [--help]\n", argv[1],
	        argv[0]);
	return 1;
}

/**
 * Seed the PRNG used for sampling chance actions. SplitMix64 is used to
 * bootstrap xoshiro256** from a single 64-bit seed, as recommended by
 * the xoshiro256** authors.
 */
static void seed_rng(uint64_t rng[4]) {
	uint64_t seed = (uint64_t)time(NULL);

	for (size_t i = 0; i < 4; i++)
		rng[i] = splitmix64(&seed);
}

/**
 * Reads moves from stdin until one matches the valid-action list,
 * storing it in *move_out. Returns false if input ends first.
 */
static bool read_move(const uint64_t actions[], uint64_t num_actions,
                      uint64_t* move_out) {
	for (;;) {
		uint64_t move;
		int scan_result = scanf("%" SCNu64, &move);

		if (scan_result == EOF) {
			fprintf(stderr, "\nEnd of input, exiting.\n");
			return false;
		}
		if (scan_result != 1) {
			printf("Invalid input. Try again: ");
			int c;
			while ((c = getchar()) != '\n' && c != EOF)
				;
			if (c == EOF) {
				fprintf(stderr, "\nEnd of input, exiting.\n");
				return false;
			}
			continue;
		}

		for (uint64_t i = 0; i < num_actions; i++) {
			if (actions[i] == move) {
				*move_out = move;
				return true;
			}
		}

		printf("Invalid move. Try again: ");
	}
}

/* Prints per-player scores plus the winner (or a draw) */
static void print_scores_and_winner(const int64_t scores[],
                                    uint64_t num_players) {
	printf("Final scores:\n");
	for (uint64_t p = 0; p < num_players; p++)
		printf("  Player %" PRIu64 ": %+lld\n", p + 1,
		       (long long)scores[p]);

	uint64_t best_player = 0;
	bool tied            = false;

	for (uint64_t p = 1; p < num_players; p++) {
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
		printf("Player %" PRIu64 " wins!\n", best_player + 1);
}


/**
 * The main_<GAME>() functions below are concrete implementations of the
 * typical game loop for a particular game.
 * Note that these are all extremely similar - by design! If training
 * autonomous agents, the loops will be nearly identical.
 * The differences here are mainly for displaying human-readable output to
 * users, for demo purposes.
 */


int main_tic_tac_toe(int argc, char* argv[]) {
	const Game* game = &tic_tac_toe;

	const int status = handle_args(argc, argv, game);
	if (status >= 0)
		return status;

	uint64_t state[TTT_STATE_SIZE]         = { 0 };
	uint64_t actions[TTT_MAX_NUM_ACTIONS]  = { 0 };
	char state_output[TTT_STRING_BUF_SIZE] = { 0 };

	printf("%s\n", game->help_prompt());

	game->init(NULL, state);

	game->to_string(state, TTT_STRING_BUF_SIZE, state_output);
	printf("\n%s\n", state_output);

	while (!game->is_terminal(state)) {
		uint64_t num_actions = game->get_valid_actions(state, actions);
		uint64_t current_player = game->get_current_player(state);

		printf("Player %" PRIu64 "'s turn. Valid moves:",
		       current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++)
			printf(" %" PRIu64, actions[i]);
		printf("\n");

		uint64_t move;
		if (!read_move(actions, num_actions, &move))
			return 1;

		game->apply_action(state, move);

		game->to_string(state, TTT_STRING_BUF_SIZE, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[TTT_NUM_PLAYERS];
	game->get_outcome(state, scores);
	print_scores_and_winner(scores, TTT_NUM_PLAYERS);

	return 0;
}

static const char* const pig_action_names[] = { "HOLD", "ROLL" };

int main_pig(int argc, char* argv[]) {
	const Game* game = &pig;

	const int status = handle_args(argc, argv, game);
	if (status >= 0)
		return status;

	uint64_t state[PIG_STATE_SIZE]         = { 0 };
	uint64_t actions[PIG_MAX_NUM_ACTIONS]  = { 0 };
	char state_output[PIG_STRING_BUF_SIZE] = { 0 };
	uint64_t rng[4];

	seed_rng(rng);

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

			// Die-face action IDs are face - 1
			printf("Chance event: rolled a %" PRIu64 "\n",
			       action + 1);
			game->apply_action(state, action);

			game->to_string(state, PIG_STRING_BUF_SIZE,
			                state_output);
			printf("\n%s\n", state_output);
			continue;
		}

		uint64_t current_player = game->get_current_player(state);

		printf("Player %" PRIu64 "'s turn. Valid moves:",
		       current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++) {
			const uint64_t a = actions[i];

			if (a < sizeof(pig_action_names) /
			                sizeof(pig_action_names[0]))
				printf(" %" PRIu64 " (%s)", a,
				       pig_action_names[a]);
			else
				printf(" %" PRIu64, a);
		}
		printf("\n");

		uint64_t move;
		if (!read_move(actions, num_actions, &move))
			return 1;

		game->apply_action(state, move);

		game->to_string(state, PIG_STRING_BUF_SIZE, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[PIG_NUM_PLAYERS];
	game->get_outcome(state, scores);
	print_scores_and_winner(scores, PIG_NUM_PLAYERS);

	return 0;
}

static const char* const blackjack_action_names[] = {
	"STAND", "HIT", "DOUBLE", "SPLIT", "SURRENDER",
};

int main_blackjack(int argc, char* argv[]) {
	const Game* game = &blackjack;

	const int status = handle_args(argc, argv, game);
	if (status >= 0)
		return status;

	uint64_t state[BJ_STATE_SIZE]         = { 0 };
	uint64_t actions[BJ_MAX_NUM_ACTIONS]  = { 0 };
	char state_output[BJ_STRING_BUF_SIZE] = { 0 };
	uint64_t rng[4];

	seed_rng(rng);

	printf("%s\n", game->help_prompt());

	game->init(NULL, state);

	game->to_string(state, BJ_STRING_BUF_SIZE, state_output);
	printf("\n%s\n", state_output);

	uint64_t chance_draws = 0;
	uint64_t hands_played = 1;

	while (!game->is_terminal(state)) {
		uint64_t num_actions = game->get_valid_actions(state, actions);

		if (game->is_chance_node(state)) {
			// Sample a chance action uniformly at random
			uint64_t pick   = xoshiro256ss(rng) % num_actions;
			uint64_t action = actions[pick];

			// Every round opens with exactly four chance draws:
			// player, dealer upcard, player, dealer hole card.
			// The 4th is dealt face down - don't print it.
			chance_draws++;
			if (chance_draws == 4)
				printf("Chance event: hole card dealt face "
				       "down\n");
			else
				printf("Chance event: drew %c\n",
				       "A23456789T"[action]);
			game->apply_action(state, action);

			game->to_string(state, BJ_STRING_BUF_SIZE,
			                state_output);
			printf("\n%s\n", state_output);
			continue;
		}

		uint64_t current_player = game->get_current_player(state);

		printf("Player %" PRIu64 "'s turn. Valid moves:",
		       current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++) {
			const uint64_t a = actions[i];

			if (a < sizeof(blackjack_action_names) /
			                sizeof(blackjack_action_names[0]))
				printf(" %" PRIu64 " (%s)", a,
				       blackjack_action_names[a]);
			else
				printf(" %" PRIu64, a);
		}
		printf("\n");

		uint64_t move;
		if (!read_move(actions, num_actions, &move))
			return 1;

		game->apply_action(state, move);

		if (move == BJ_ACTION_SPLIT)
			hands_played++;

		game->to_string(state, BJ_STRING_BUF_SIZE, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[BJ_NUM_PLAYERS];
	game->get_outcome(state, scores);

	const long long score = (long long)scores[0];
	const long long unit  = BJ_SCORE_UNITS_PER_BET;

	if (score == 0 && hands_played > 1)
		printf("Final score: +0 bets (broke even across %" PRIu64
		       " settled bets)\n",
		       hands_played);
	else
		printf("Final score: %+g bet%s (%s)\n",
		       (double)score / (double)unit,
		       (score == unit || score == -unit) ? "" : "s",
		       score > 0   ? "player wins"
		       : score < 0 ? "player loses"
		                   : "push");

	return 0;
}

int main_connect4(int argc, char* argv[]) {
	const Game* game = &connect4;

	const int status = handle_args(argc, argv, game);
	if (status >= 0)
		return status;

	uint64_t state[C4_STATE_SIZE]         = { 0 };
	uint64_t actions[C4_MAX_NUM_ACTIONS]  = { 0 };
	char state_output[C4_STRING_BUF_SIZE] = { 0 };

	printf("%s\n", game->help_prompt());

	game->init(NULL, state);

	game->to_string(state, C4_STRING_BUF_SIZE, state_output);
	printf("\n%s\n", state_output);

	// Connect-4 is fully deterministic: no chance nodes, no PRNG
	while (!game->is_terminal(state)) {
		uint64_t num_actions = game->get_valid_actions(state, actions);
		uint64_t current_player = game->get_current_player(state);

		printf("Player %" PRIu64 "'s turn. Valid columns:",
		       current_player + 1);
		for (uint64_t i = 0; i < num_actions; i++)
			printf(" %" PRIu64, actions[i]);
		printf("\n");

		uint64_t move;
		if (!read_move(actions, num_actions, &move))
			return 1;

		game->apply_action(state, move);

		game->to_string(state, C4_STRING_BUF_SIZE, state_output);
		printf("\n%s\n", state_output);
	}

	int64_t scores[C4_NUM_PLAYERS];
	game->get_outcome(state, scores);
	print_scores_and_winner(scores, C4_NUM_PLAYERS);

	return 0;
}

int main(int argc, char* argv[]) {
	/**
	 * Switch the demoed game by calling the relevant main_<GAME>()
	 * function here
	 */
	return main_blackjack(argc, argv);
}
