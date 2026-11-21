#ifndef BOARD_GAME_H
#define BOARD_GAME_H

#include <stdint.h>
#include <stdbool.h>

/**
 * Required Macros
 * ===============
 *
 * Each game is required to provide the following macros, in order to give
 * information about memory usage at compile-time.
 *
 * This is because the functions in `Game` assume that the caller has allocated
 * the amount of memory defined by the macros below.
 *
 * NOTE: **PREFIX EACH GAME'S MACROS WITH A NAMESPACE!**
 *
 * 	- <NAMESPACE>_NUM_PLAYERS
 * 	- <NAMESPACE>_STATE_SIZE
 * 	- <NAMESPACE>_MAX_NUM_ACTIONS
 * 	- <NAMESPACE>_STRING_BUF_SIZE
 * 		Minimum buffer size (including null terminator) that the
 * 		caller must allocate for `to_string()`.
 * 	- <NAMESPACE>_OBS_NDIMS
 * 		Number of dimensions in the `observation` tensor.
 * 		e.g. 1 for a flat vector, 3 for (channels, rows, cols)
 * 	- <NAMESPACE>_OBS_SIZE
 * 		Total number of elements in the `observation`.
 * 		Must equal the product of the first `OBS_NDIMS` entries in
 * 		`obs_dims`.
 * 	- <NAMESPACE>_FEATURES_NDIMS
 * 		Number of dimensions in the `features` tensor.
 * 		e.g. 1 for a flat vector, 3 for (channels, rows, cols)
 * 	- <NAMESPACE>_FEATURES_SIZE
 * 		Similar to `OBS_SIZE`
 */

typedef struct Game {
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |      required allocation      |
	 * 	|:---------:|:-----------------------------:|
	 * 	| state     | STATE_SIZE * sizeof(uint64_t) |
	 */
	void (*init)(const void* config, uint64_t state[]);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |      required allocation      |
	 * 	|:---------:|:-----------------------------:|
	 * 	| state     | STATE_SIZE * sizeof(uint64_t) |
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
	 * 	|  parameter  |         required allocation        |
	 * 	|:-----------:|:----------------------------------:|
	 * 	| state       | STATE_SIZE * sizeof(uint64_t)      |
	 * 	| actions_out | MAX_NUM_ACTIONS * sizeof(uint64_t) |
	 */
	uint64_t (*get_valid_actions)(const uint64_t state[],
	                              uint64_t actions_out[]);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |      required allocation      |
	 * 	|:---------:|:-----------------------------:|
	 * 	| state     | STATE_SIZE * sizeof(uint64_t) |
	 */
	void (*apply_action)(uint64_t state[], uint64_t action);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	| parameter |      required allocation      |
	 * 	|:---------:|:-----------------------------:|
	 * 	| state     | STATE_SIZE * sizeof(uint64_t) |
	 */
	bool (*is_terminal)(const uint64_t state[]);
	/**
	 * ASSUMPTIONS
	 * ===========
	 *
	 * Caller has allocated:
	 *
	 * 	|  parameter |      required allocation      |
	 * 	|:----------:|:-----------------------------:|
	 * 	| state      | STATE_SIZE * sizeof(uint64_t) |
	 * 	| scores_out | NUM_PLAYERS * sizeof(int64_t) |
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
	 * 	| parameter |      required allocation      |
	 * 	|:---------:|:-----------------------------:|
	 * 	| state     | STATE_SIZE * sizeof(uint64_t) |
	 * 	| obs_out   | OBS_SIZE * sizeof(uint8_t)    |
	 *
	 * `obs_out` is a contiguous buffer representing a tensor of shape
	 * `obs_dims[0..OBS_NDIMS-1]` in row-major order.
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
	 * 	|   parameter  |      required allocation      |
	 * 	|:------------:|:-----------------------------:|
	 * 	| state        | STATE_SIZE * sizeof(uint64_t) |
	 * 	| features_out | FEATURES_SIZE * sizeof(float) |
	 *
	 * `features_out` is a contiguous buffer representing a tensor of shape
	 * `features_dims[0..FEATURES_NDIMS-1]` in row-major order.
	 */
	void (*get_features)(const uint64_t state[], uint64_t player,
	                     float* features_out);
	uint64_t (*to_string)(const uint64_t state[], uint64_t buf_size,
	                      char* buf);
	const char* (*help_prompt)(void);
	/**
	 * Shape of the `observation` tensor.
	 * 	e.g. { 2, 3, 3 } for tic-tac-toe (2 planes of 3x3)
	 * 	     { 19, 8, 8 } for chess (19 planes of 8x8)
	 * 	     { 4 } for Pig (flat vector of 4 values)
	 *
	 * Note: Only the first `OBS_NDIMS` elements are meaningful.
	 */
	uint64_t obs_dims[4];
	/**
	 * Shape of the `feature` tensor.
	 * Note: Only the first `FEATURES_NDIMS` elements are meaningful.
	 */
	uint64_t features_dims[4];
	/**
	 * TO-DO:
	 * Zobrist hashing function:
	 * 	uint64_t (*hash)(const uint64_t state[])
	 */
} Game;

#endif
