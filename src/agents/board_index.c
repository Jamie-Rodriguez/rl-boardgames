#include <assert.h>
#include <stddef.h>

#include "agents/board_index.h"
// Auto-generated from `generate_ttt_board_to_index.c`
#include "ttt_board_to_index.h"

uint64_t ttt_afterstate_to_index(const float features[], uint64_t action) {
	const uint64_t index =
	        ttt_board_to_index[ttt_afterstate_to_code(features, action)];

	assert(index != TTT_BOARD_TO_INDEX_NONE);

	return index;
}
