#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "pig.h"

/**
 * `state` layout:
 *
 * index:      0         1         2         3        3 + N - 1
 * 	+----------+---------+---------+--------+--------+---------+
 * 	|   turn   | waiting |  turn   |  p0    |  ...   |   pN-1  |
 * 	|  player  |  roll?  |  total  | score  |        |  score  |
 * 	+----------+---------+---------+--------+--------+---------+
 *
 * [0]:              turn player (0..PIG_NUM_PLAYERS-1)
 *                   The player whose turn it currently is. This is
 *                   stable across chance nodes: while the die is being
 *                   resolved, this still holds the player who chose to
 *                   roll, so that we know who to credit or pass the
 *                   turn from once the die face lands.
 * [1]:              "awaiting roll?" flag (0 or 1)
 *                   When 1, the current player is the dice player and
 *                   the caller must supply a die face via apply_action.
 * [2]:              turn total (running sum of the current turn's rolls)
 * [3..3+N-1]:       per-player banked scores
 */
#define STATE_OFFSET_TURN_PLAYER   0
#define STATE_OFFSET_AWAITING_ROLL 1
#define STATE_OFFSET_TURN_TOTAL    2
#define STATE_OFFSET_SCORES        3

#define PIG_NUM_DIE_FACES 6

/**
 * Decision actions (valid when it is a player's turn to decide):
 * 	PIG_ACTION_HOLD - bank the turn total and end the turn
 * 	PIG_ACTION_ROLL - roll the die (transitions to a chance node)
 */
#define PIG_ACTION_HOLD 0
#define PIG_ACTION_ROLL 1

/**
 * The dice "player" is assigned the last index `PIG_NUM_PLAYERS`. When
 * `get_current_player()` returns this value, the caller is responsible
 * for sampling a uniformly random dice face (action ID ∈ {0..5}) and passing
 * it to `apply_action()`. Agents never control the dice "player" directly.
 */
#define PIG_DICE_PLAYER PIG_NUM_PLAYERS


static void pig_init(const void* config, uint64_t state[]) {
	(void)config; // Unused

	for (size_t i = 0; i < PIG_STATE_SIZE; i++)
		state[i] = 0;
}

static uint64_t pig_get_current_player(const uint64_t state[]) {
	if (state[STATE_OFFSET_AWAITING_ROLL])
		return PIG_DICE_PLAYER;

	return state[STATE_OFFSET_TURN_PLAYER];
}

static bool pig_is_chance_node(const uint64_t state[]) {
	return state[STATE_OFFSET_AWAITING_ROLL] != 0;
}

static uint64_t pig_get_valid_actions(const uint64_t state[],
                                      uint64_t actions_out[]) {
	if (state[STATE_OFFSET_AWAITING_ROLL]) {
		// Chance node: six possible die faces (action ID = face - 1)
		for (uint64_t i = 0; i < PIG_NUM_DIE_FACES; i++)
			actions_out[i] = i;

		return PIG_NUM_DIE_FACES;
	}

	// Decision node: HOLD or ROLL
	actions_out[0] = PIG_ACTION_HOLD;
	actions_out[1] = PIG_ACTION_ROLL;

	return 2;
}

static inline uint64_t pig_next_player(uint64_t player) {
	return (player + 1) % PIG_NUM_PLAYERS;
}

static void pig_apply_action(uint64_t state[], uint64_t action) {
	const uint64_t player = state[STATE_OFFSET_TURN_PLAYER];
	assert(player < PIG_NUM_PLAYERS);

	if (state[STATE_OFFSET_AWAITING_ROLL]) {
		// Chance action: resolve a die face
		assert(action < PIG_NUM_DIE_FACES);

		const uint64_t face               = action + 1;
		state[STATE_OFFSET_AWAITING_ROLL] = 0;

		if (face == 1) {
			// Bust: forfeit turn total, advance to next player
			state[STATE_OFFSET_TURN_TOTAL] = 0;
			state[STATE_OFFSET_TURN_PLAYER] =
			        pig_next_player(player);
		} else {
			// Accumulate, same player decides again
			state[STATE_OFFSET_TURN_TOTAL] += face;
		}
		return;
	}

	// Decision action
	if (action == PIG_ACTION_HOLD) {
		// Bank turn total into player's score
		state[STATE_OFFSET_SCORES + player] +=
		        state[STATE_OFFSET_TURN_TOTAL];
		state[STATE_OFFSET_TURN_TOTAL]  = 0;
		state[STATE_OFFSET_TURN_PLAYER] = pig_next_player(player);
	} else {
		assert(action == PIG_ACTION_ROLL);
		// Transition to a chance node; die face resolved on next call
		state[STATE_OFFSET_AWAITING_ROLL] = 1;
	}
}

static bool pig_is_terminal(const uint64_t state[]) {
	for (size_t p = 0; p < PIG_NUM_PLAYERS; p++)
		if (state[STATE_OFFSET_SCORES + p] >= PIG_TARGET)
			return true;
	return false;
}

static void pig_get_outcome(const uint64_t state[], int64_t scores_out[]) {
	assert(pig_is_terminal(state));

	for (size_t p = 0; p < PIG_NUM_PLAYERS; p++) {
		if (state[STATE_OFFSET_SCORES + p] >= PIG_TARGET) {
			// Pig is zero-sum: winner takes +1, everyone else -1
			for (size_t q = 0; q < PIG_NUM_PLAYERS; q++)
				scores_out[q] = (q == p) ? 1 : -1;
			return;
		}
	}
}

static void pig_get_observation(const uint64_t state[], uint64_t player,
                                uint8_t* obs_out) {
	assert(player < PIG_NUM_PLAYERS);

	// Scores rotated so the requesting player is first
	for (size_t i = 0; i < PIG_NUM_PLAYERS; i++) {
		const uint64_t p = (player + i) % PIG_NUM_PLAYERS;
		obs_out[i]       = (uint8_t)state[STATE_OFFSET_SCORES + p];
	}

	obs_out[PIG_NUM_PLAYERS] = (uint8_t)state[STATE_OFFSET_TURN_TOTAL];
}

static void pig_get_features(const uint64_t state[], uint64_t player,
                             float* features_out) {
	assert(player < PIG_NUM_PLAYERS);

	const float norm = 1.0f / (float)PIG_TARGET;

	for (size_t i = 0; i < PIG_NUM_PLAYERS; i++) {
		const uint64_t p = (player + i) % PIG_NUM_PLAYERS;
		features_out[i]  = (float)state[STATE_OFFSET_SCORES + p] * norm;
	}

	features_out[PIG_NUM_PLAYERS] =
	        (float)state[STATE_OFFSET_TURN_TOTAL] * norm;
}

static uint64_t pig_to_string(const uint64_t state[], uint64_t buf_size,
                              char* buf) {
	assert(buf != NULL);
	assert(buf_size >= PIG_STRING_BUF_SIZE);

	char* const start = buf;
	size_t remaining  = (size_t)buf_size;
	int n;

	const unsigned player = (unsigned)(state[STATE_OFFSET_TURN_PLAYER] + 1);

	if (state[STATE_OFFSET_AWAITING_ROLL])
		n = snprintf(buf, remaining,
		             "Awaiting die roll (player %u's turn)\n", player);
	else
		n = snprintf(buf, remaining, "Player %u to move\n", player);

	if (n > 0 && (size_t)n < remaining) {
		buf += n;
		remaining -= (size_t)n;
	}

	for (size_t p = 0; p < PIG_NUM_PLAYERS; p++) {
		n = snprintf(
		        buf, remaining, "  Player %zu score: %llu\n", p + 1,
		        (unsigned long long)state[STATE_OFFSET_SCORES + p]);
		if (n > 0 && (size_t)n < remaining) {
			buf += n;
			remaining -= (size_t)n;
		}
	}

	n = snprintf(buf, remaining, "Turn total: %llu\n",
	             (unsigned long long)state[STATE_OFFSET_TURN_TOTAL]);
	if (n > 0 && (size_t)n < remaining)
		buf += n;

	return (uint64_t)(buf - start);
}

static const char* pig_help_prompt(void) {
	return "Pig\n"
	       "===\n"
	       "\n"
	       "On your turn, choose to HOLD (0) or ROLL (1):\n"
	       "\n"
	       "  HOLD: bank your turn total into your score, end turn\n"
	       "  ROLL: roll a six-sided die\n"
	       "          2-6: add to turn total, you decide again\n"
	       "          1:   forfeit turn total, turn ends\n"
	       "\n"
	       "First player to reach the target score wins.\n"
	       "\n"
	       "Note: ROLL transitions to a chance node. When\n"
	       "is_chance_node() returns true, the caller must sample\n"
	       "a uniformly random action ID in 0..5 (die face =\n"
	       "action + 1) and pass it to apply_action().\n";
}

const Game pig = {
	.init               = pig_init,
	.get_current_player = pig_get_current_player,
	.is_chance_node     = pig_is_chance_node,
	.get_valid_actions  = pig_get_valid_actions,
	.apply_action       = pig_apply_action,
	.is_terminal        = pig_is_terminal,
	.get_outcome        = pig_get_outcome,
	.get_observation    = pig_get_observation,
	.get_features       = pig_get_features,
	.to_string          = pig_to_string,
	.help_prompt        = pig_help_prompt,
	.obs_dims           = { PIG_OBS_SIZE },
	.features_dims      = { PIG_FEATURES_SIZE },
};
