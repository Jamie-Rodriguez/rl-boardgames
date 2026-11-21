#ifndef BOARD_INDEX_H
#define BOARD_INDEX_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "games/tic_tac_toe.h"

#define TTT_BOARD_INDEX_SQUARE_STATES (TTT_NUM_PLAYERS + 1)

#define TTT_BOARD_INDEX_NUM_CODES                                        \
	(TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES * TTT_BOARD_INDEX_SQUARE_STATES * \
	 TTT_BOARD_INDEX_SQUARE_STATES)

#if TTT_NUM_ROWS != TTT_NUM_COLS
#error "TTT_BOARD_INDEX_NUM_SYMMETRIES needs a square board (quarter turns)"
#endif
#define TTT_BOARD_INDEX_NUM_SYMMETRIES (2 * 4)

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

uint64_t ttt_afterstate_to_index(const float features[], uint64_t action);

#endif
