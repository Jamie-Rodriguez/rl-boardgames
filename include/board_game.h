#ifndef BOARD_GAME_H
#define BOARD_GAME_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |         required allocation        |
	 * 	|:---------:|:----------------------------------:|
	 * 	|   state   | Game.state_size * sizeof(uint64_t) |
	 */
	void (*init)(const void* config, uint64_t state[]);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |         required allocation        |
	 * 	|:---------:|:----------------------------------:|
	 * 	|   state   | Game.state_size * sizeof(uint64_t) |
	 */
	uint64_t (*get_current_player)(const uint64_t state[]);
	/**
	 * Implementation of this function should return the *total* count of
	 * valid actions.
	 *
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	|  parameter  |         required allocation         |
	 * 	|:-----------:|:-----------------------------------:|
	 * 	|    state    |  Game.state_size * sizeof(uint64_t) |
	 * 	| actions_out | Game.max_actions * sizeof(uint64_t) |
	 */
	uint64_t (*get_valid_actions)(const uint64_t state[],
	                              uint64_t actions_out[]);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |         required allocation        |
	 * 	|:---------:|:----------------------------------:|
	 * 	|   state   | Game.state_size * sizeof(uint64_t) |
	 */
	void (*apply_action)(uint64_t state[], uint64_t action);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |         required allocation        |
	 * 	|:---------:|:----------------------------------:|
	 * 	|   state   | Game.state_size * sizeof(uint64_t) |
	 */
	bool (*is_terminal)(const uint64_t state[]);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	|  parameter |         required allocation        |
	 * 	|:----------:|:----------------------------------:|
	 * 	|    state   | Game.state_size * sizeof(uint64_t) |
	 * 	| scores_out | Game.num_players * sizeof(int64_t) |
	 */
	void (*get_outcome)(const uint64_t state[], int64_t scores_out[]);
	/**
	 * Encodes the game state as a semantically-faithful observation
	 * from the perspective of `player`.
	 *
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |         required allocation        |
	 * 	|:---------:|:----------------------------------:|
	 * 	|   state   | Game.state_size * sizeof(uint64_t) |
	 * 	|  obs_out  |    Game.obs_size * sizeof(uint8_t) |
	 *
	 * `obs_out` is a contiguous buffer representing a tensor of shape
	 * `obs_dims[0..obs_ndims-1]` in row-major order.
	 */
	void (*get_observation)(const uint64_t state[], uint64_t player,
	                        uint8_t* obs_out);
	/**
	 * Encodes the game state as a float array optimised for neural
	 * network consumption, from the perspective of `player`.
	 * May normalise, broadcast, or otherwise transform values
	 * beyond a simple cast of `get_observation()`.
	 *
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	|   parameter  |         required allocation        |
	 * 	|:------------:|:----------------------------------:|
	 * 	|     state    | Game.state_size * sizeof(uint64_t) |
	 * 	| features_out | Game.features_size * sizeof(float) |
	 *
	 * `features_out` is a contiguous buffer representing a tensor of shape
	 * `features_dims[0..features_ndims-1]` in row-major order.
	 */
	void (*get_features)(const uint64_t state[], uint64_t player,
	                     float* features_out);
	uint64_t (*to_string)(const uint64_t state[], uint64_t buf_size,
	                      char* buf);
	const char* (*help_prompt)(void);
	uint64_t num_players;
	uint64_t state_size;
	uint64_t max_actions;
	/**
	 * Minimum buffer size (including null terminator) that the caller
	 * must allocate for `to_string()`.
	 */
	uint64_t string_buf_size;
	/**
	 * Number of dimensions in the observation tensor.
	 * 	e.g. 1 for a flat vector, 3 for (channels, rows, cols)
	 */
	uint64_t obs_ndims;
	/**
	 * Shape of the `observation` tensor.
	 * 	e.g. { 2, 3, 3 } for tic-tac-toe (2 planes of 3x3)
	 * 	     { 19, 8, 8 } for chess (19 planes of 8x8)
	 * 	     { 4 } for Pig (flat vector of 4 values)
	 *
	 * Note: Only the first `obs_ndims` elements are meaningful.
	 */
	uint64_t obs_dims[4];

	/**
	 * Total number of elements in the `observation`.
	 * Must equal the product of the first `obs_ndims` entries in
	 * `obs_dims`.
	 */
	uint64_t obs_size;
	/**
	 * Number of dimensions in the `feature` tensor.
	 * Often the same as `obs_ndims`, but may differ if `get_features()`
	 * broadcasts scalar values into full planes.
	 */
	uint64_t features_ndims;
	/**
	 * Shape of the `feature` tensor.
	 * Note: Only the first `features_ndims` elements are meaningful.
	 */
	uint64_t features_dims[4];
	/**
	 * Total number of elements in the feature tensor.
	 * Must equal the product of the first `features_ndims` entries in
	 * `features_dims`.
	 */
	uint64_t features_size;
	/**
	 * TO-DO:
	 * Zobrist hashing function:
	 * 	uint64_t (*hash)(const uint64_t state[])
	 */
} Game;

#endif
