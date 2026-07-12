#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include "blackjack.h"

/**
 * State layout:
 * 	[0]:        phase of the round (deal / decide / draw / dealer / done)
 * 	[1..10]:    remaining shoe composition: count of each rank 0..9
 * 	[11]:       dealer upcard (rank + 1, 0 if not yet dealt)
 * 	[12]:       dealer hole card (rank + 1, 0 if not yet dealt)
 * 	[13]:       dealer hard total (aces counted as 1)
 * 	[14]:       dealer ace count
 * 	[15]:       dealer card count
 * 	[16]:       number of player hands (1..BJ_MAX_HANDS)
 * 	[17]:       index of the hand currently being played
 * 	[18..]:     BJ_MAX_HANDS hand records of 6 words each:
 * 	                [card1, card2, hard total, ace count, card count, flags]
 *
 * Card encoding: ranks 0..9 = A,2,3,4,5,6,7,8,9,T. Stored card fields hold
 * rank + 1 so that 0 means "no card".
 *
 * Hand totals are kept as *hard* totals (every ace counted as 1) plus an ace
 * count; the best total is hard + 10 whenever at least one ace can be promoted
 * without busting. A hand is bust iff its hard total exceeds 21.
 *
 * Note: Each hand record stores its *initial* two cards, because
 * split-eligibility requires an equal-rank pair. As well as this, the
 * "split-aces-one-card" rule depends on the first card being an ace.
 * Neither of these conditions is derivable solely from totals.
 * Every decision/rule after that depends only on the hand's hard total,
 * ace-count, and card count, which form a sufficient minimal state, so later
 * hits are folded into the aggregate totals rather than explicitly stored.
 */
#define STATE_OFFSET_PHASE         0
#define STATE_OFFSET_SHOE          1
#define STATE_OFFSET_DEALER_UP     11
#define STATE_OFFSET_DEALER_HOLE   12
#define STATE_OFFSET_DEALER_TOTAL  13
#define STATE_OFFSET_DEALER_ACES   14
#define STATE_OFFSET_DEALER_NCARDS 15
#define STATE_OFFSET_NUM_HANDS     16
#define STATE_OFFSET_CUR_HAND      17
#define STATE_OFFSET_HANDS         18

/* Note: These are offset from the specific hand's pointer */
#define HAND_CARD1  0 // rank + 1, 0 if not dealt
#define HAND_CARD2  1 // rank + 1, 0 if not dealt
#define HAND_TOTAL  2 // hard total (aces count as 1)
#define HAND_ACES   3
#define HAND_NCARDS 4
#define HAND_FLAGS  5

/* Bitmask of hand properties */
#define HAND_FLAG_FINISHED    (1 << 0) // no further decisions for this hand
#define HAND_FLAG_DOUBLED     (1 << 1)
#define HAND_FLAG_SURRENDERED (1 << 2)
#define HAND_FLAG_SPLIT       (1 << 3) // hand was created by/part of a split

/**
 * Round phases. Every phase except DECIDE and DONE is a chance node:
 * the next apply_action() consumes a card rank drawn from the shoe.
 */
#define PHASE_DEAL_PLAYER_1    0 // chance: player's first card
#define PHASE_DEAL_DEALER_UP   1 // chance: dealer's upcard
#define PHASE_DEAL_PLAYER_2    2 // chance: player's second card
#define PHASE_DEAL_DEALER_HOLE 3 // chance: dealer's hole card (+ peek)
#define PHASE_DECIDE           4 // decision: current hand acts
#define PHASE_PLAYER_DRAW      5 // chance: card for current hand
#define PHASE_DEALER_DRAW      6 // chance: card for the dealer
#define PHASE_DONE             7 // terminal

/**
 * The deck "player" is assigned the index BJ_NUM_PLAYERS. When
 * get_current_player() returns this value, the caller is responsible for
 * sampling one of the actions returned by get_valid_actions() uniformly at
 * random and passing it to apply_action(). The action array contains one entry
 * per remaining physical card, so a uniform sample is exactly a
 * draw-without-replacement from the shoe.
 */
#define BJ_DECK_PLAYER BJ_NUM_PLAYERS

#define RANK_ACE 0
#define RANK_TEN 9

static inline uint64_t* hand_ptr(uint64_t state[], uint64_t h) {
	assert(h < BJ_MAX_HANDS);

	return &state[STATE_OFFSET_HANDS + h * HAND_NUM_WORDS];
}

static inline const uint64_t* hand_cptr(const uint64_t state[], uint64_t h) {
	assert(h < BJ_MAX_HANDS);

	return &state[STATE_OFFSET_HANDS + h * HAND_NUM_WORDS];
}

/* Hard value of a rank (aces count as 1) */
static inline uint64_t rank_value(uint64_t rank) {
	assert(rank < BJ_NUM_RANKS);

	return rank + 1;
}

/* Best total: promote one ace from 1 to 11 if it does not bust */
static inline uint64_t best_total(uint64_t hard_total, uint64_t num_aces) {
	return (num_aces > 0 && hard_total + 10 <= 21) ? hard_total + 10
	                                               : hard_total;
}

static inline uint64_t hand_best_total(const uint64_t* hand) {
	return best_total(hand[HAND_TOTAL], hand[HAND_ACES]);
}

static inline bool hand_is_bust(const uint64_t* hand) {
	return hand[HAND_TOTAL] > 21;
}

// A natural blackjack: 21 with the original two cards of an "unsplit" hand
static inline bool is_natural_blackjack(const uint64_t state[]) {
	const uint64_t* hand = hand_cptr(state, 0);

	return state[STATE_OFFSET_NUM_HANDS] == 1 && hand[HAND_NCARDS] == 2 &&
	       (hand[HAND_FLAGS] & HAND_FLAG_SPLIT) == 0 &&
	       hand_best_total(hand) == 21;
}

static inline uint64_t dealer_best_total(const uint64_t state[]) {
	return best_total(state[STATE_OFFSET_DEALER_TOTAL],
	                  state[STATE_OFFSET_DEALER_ACES]);
}

static inline bool dealer_is_blackjack(const uint64_t state[]) {
	return state[STATE_OFFSET_DEALER_NCARDS] == 2 &&
	       dealer_best_total(state) == 21;
}

static inline bool dealer_must_hit(const uint64_t state[]) {
	const uint64_t total = dealer_best_total(state);

	if (total < 17)
		return true;

#if BJ_DEALER_HITS_SOFT_17
	/* Soft 17: a promoted ace is in play (best != hard total) */
	if (total == 17 && total != state[STATE_OFFSET_DEALER_TOTAL])
		return true;
#endif

	return false;
}

/* The dealer's hand is face up from the dealer's turn onwards */
static inline bool dealer_hand_revealed(const uint64_t state[]) {
	const uint64_t phase = state[STATE_OFFSET_PHASE];

	return phase == PHASE_DEALER_DRAW || phase == PHASE_DONE;
}

static void add_card_to_hand(uint64_t* hand, uint64_t rank) {
	hand[HAND_TOTAL] += rank_value(rank);

	if (rank == RANK_ACE)
		hand[HAND_ACES]++;

	if (hand[HAND_NCARDS] == 0)
		hand[HAND_CARD1] = rank + 1;
	else if (hand[HAND_NCARDS] == 1)
		hand[HAND_CARD2] = rank + 1;

	hand[HAND_NCARDS]++;
}

static void add_card_to_dealers_hand(uint64_t state[], uint64_t rank) {
	state[STATE_OFFSET_DEALER_TOTAL] += rank_value(rank);

	if (rank == RANK_ACE)
		state[STATE_OFFSET_DEALER_ACES]++;

	state[STATE_OFFSET_DEALER_NCARDS]++;
}

static void draw_from_shoe(uint64_t state[], uint64_t rank) {
	assert(rank < BJ_NUM_RANKS);
	assert(state[STATE_OFFSET_SHOE + rank] > 0);

	state[STATE_OFFSET_SHOE + rank]--;
}

/**
 * Called once the player phase is over (every hand finished). Decides whether
 * the dealer needs to draw, and otherwise ends the round.
 */
static void enter_dealer_phase(uint64_t state[]) {
	bool any_hands_live = false;

	for (uint64_t h = 0; h < state[STATE_OFFSET_NUM_HANDS]; h++) {
		const uint64_t* hand = hand_cptr(state, h);

		if ((hand[HAND_FLAGS] & HAND_FLAG_SURRENDERED) == 0 &&
		    !hand_is_bust(hand)) {
			any_hands_live = true;
			break;
		}
	}

	/* Dealer only plays out their hand if a bet is still live */
	if (!any_hands_live || !dealer_must_hit(state)) {
		state[STATE_OFFSET_PHASE] = PHASE_DONE;
		return;
	}

	state[STATE_OFFSET_PHASE] = PHASE_DEALER_DRAW;
}

/**
 * Marks the current hand finished and moves play to the next hand (a hand
 * freshly created by a split still owes its second card, hence
 * PHASE_PLAYER_DRAW), or hands over to the dealer when none remain.
 */
static void finish_hand_and_advance(uint64_t state[]) {
	assert(state[STATE_OFFSET_CUR_HAND] < state[STATE_OFFSET_NUM_HANDS]);

	hand_ptr(state, state[STATE_OFFSET_CUR_HAND])[HAND_FLAGS] |=
	        HAND_FLAG_FINISHED;

	state[STATE_OFFSET_CUR_HAND]++;

	if (state[STATE_OFFSET_CUR_HAND] < state[STATE_OFFSET_NUM_HANDS]) {
		assert(hand_cptr(state,
		                 state[STATE_OFFSET_CUR_HAND])[HAND_NCARDS] ==
		       1);

		state[STATE_OFFSET_PHASE] = PHASE_PLAYER_DRAW;
		return;
	}

	enter_dealer_phase(state);
}

static void blackjack_init(const void* config, uint64_t state[]) {
	(void)config; // Unused

	for (size_t i = 0; i < BJ_STATE_SIZE; i++)
		state[i] = 0;

	for (uint64_t r = 0; r < BJ_NUM_RANKS; r++)
		state[STATE_OFFSET_SHOE + r] =
		        (r == RANK_TEN ? 16 : 4) * BJ_NUM_DECKS;

	state[STATE_OFFSET_NUM_HANDS] = 1;
	state[STATE_OFFSET_PHASE]     = PHASE_DEAL_PLAYER_1;
}

static bool blackjack_is_chance_node(const uint64_t state[]) {
	const uint64_t phase = state[STATE_OFFSET_PHASE];

	return phase != PHASE_DECIDE && phase != PHASE_DONE;
}

static uint64_t blackjack_get_current_player(const uint64_t state[]) {
	if (blackjack_is_chance_node(state))
		return BJ_DECK_PLAYER;

	return 0;
}

static uint64_t blackjack_get_valid_actions(const uint64_t state[],
                                            uint64_t actions_out[]) {
	if (state[STATE_OFFSET_PHASE] == PHASE_DONE)
		return 0;

	if (blackjack_is_chance_node(state)) {
		/**
		 * Chance node: one action entry per remaining physical card
		 * (action ID = rank), so that uniformly sampling the action
		 * array is a faithful draw-without-replacement from the shoe.
		 */
		uint64_t n = 0;

		for (uint64_t r = 0; r < BJ_NUM_RANKS; r++)
			for (uint64_t c = 0; c < state[STATE_OFFSET_SHOE + r];
			     c++)
				actions_out[n++] = r;

		assert(n > 0);
		assert(n <= BJ_MAX_NUM_ACTIONS);
		return n;
	}

	// Decision node
	const uint64_t* hand = hand_cptr(state, state[STATE_OFFSET_CUR_HAND]);
	uint64_t n           = 0;

	actions_out[n++] = BJ_ACTION_STAND;
	actions_out[n++] = BJ_ACTION_HIT;

	// Double down: only on a two-card hand (allowed after splits)
	if (hand[HAND_NCARDS] == 2)
		actions_out[n++] = BJ_ACTION_DOUBLE;

	/**
	 * Split: equal-rank pair, up to BJ_MAX_HANDS hands.
	 * (Split aces never reach a decision node, so re-splitting aces is
	 * implicitly impossible.)
	 */
	if (hand[HAND_NCARDS] == 2 && hand[HAND_CARD1] == hand[HAND_CARD2] &&
	    state[STATE_OFFSET_NUM_HANDS] < BJ_MAX_HANDS)
		actions_out[n++] = BJ_ACTION_SPLIT;

	/**
	 * Late surrender: only as the very first decision of the round.
	 * (The SPLIT-flag clause is provably redundant - a split always leaves
	 * NUM_HANDS > 1 and nothing ever decrements it, but keeping it in for
	 * completeness.)
	 */
	if (state[STATE_OFFSET_NUM_HANDS] == 1 && hand[HAND_NCARDS] == 2 &&
	    (hand[HAND_FLAGS] & HAND_FLAG_SPLIT) == 0)
		actions_out[n++] = BJ_ACTION_SURRENDER;

	return n;
}

/* Resolve a freshly drawn card rank according to the current phase */
static void apply_chance_action(uint64_t state[], uint64_t rank) {
	draw_from_shoe(state, rank);

	switch (state[STATE_OFFSET_PHASE]) {
	case PHASE_DEAL_PLAYER_1:
		add_card_to_hand(hand_ptr(state, 0), rank);
		state[STATE_OFFSET_PHASE] = PHASE_DEAL_DEALER_UP;
		return;

	case PHASE_DEAL_DEALER_UP:
		add_card_to_dealers_hand(state, rank);
		state[STATE_OFFSET_DEALER_UP] = rank + 1;
		state[STATE_OFFSET_PHASE]     = PHASE_DEAL_PLAYER_2;
		return;

	case PHASE_DEAL_PLAYER_2:
		add_card_to_hand(hand_ptr(state, 0), rank);
		state[STATE_OFFSET_PHASE] = PHASE_DEAL_DEALER_HOLE;
		return;

	case PHASE_DEAL_DEALER_HOLE:
		add_card_to_dealers_hand(state, rank);
		state[STATE_OFFSET_DEALER_HOLE] = rank + 1;

		/**
		 * Peek: with an ace or ten up, the dealer checks the hole
		 * card; a natural ends the round before the player acts. A
		 * player natural is also paid out immediately.
		 */
		if (dealer_is_blackjack(state) || is_natural_blackjack(state)) {
			/**
			 * Close out the hand as finish_hand_and_advance()
			 * would, so terminal states are encoded identically
			 * regardless of how the round ended
			 */
			hand_ptr(state, 0)[HAND_FLAGS] |= HAND_FLAG_FINISHED;
			state[STATE_OFFSET_CUR_HAND] =
			        state[STATE_OFFSET_NUM_HANDS];
			state[STATE_OFFSET_PHASE] = PHASE_DONE;
			return;
		}

		state[STATE_OFFSET_PHASE] = PHASE_DECIDE;
		return;

	case PHASE_PLAYER_DRAW: {
		uint64_t* hand = hand_ptr(state, state[STATE_OFFSET_CUR_HAND]);

		add_card_to_hand(hand, rank);

		// Split aces receive exactly one card
		const bool split_ace_fill =
		        (hand[HAND_FLAGS] & HAND_FLAG_SPLIT) != 0 &&
		        hand[HAND_NCARDS] == 2 &&
		        hand[HAND_CARD1] == RANK_ACE + 1;

		if (split_ace_fill || hand_is_bust(hand) ||
		    hand_best_total(hand) == 21 ||
		    (hand[HAND_FLAGS] & HAND_FLAG_DOUBLED) != 0) {
			finish_hand_and_advance(state);
			return;
		}

		state[STATE_OFFSET_PHASE] = PHASE_DECIDE;
		return;
	}

	case PHASE_DEALER_DRAW:
		add_card_to_dealers_hand(state, rank);

		if (!dealer_must_hit(state))
			state[STATE_OFFSET_PHASE] = PHASE_DONE;
		return;

	default:
		assert(false && "apply_action() called in terminal state");
	}
}

static void apply_decision_action(uint64_t state[], uint64_t action) {
	uint64_t* hand = hand_ptr(state, state[STATE_OFFSET_CUR_HAND]);

	switch (action) {
	case BJ_ACTION_STAND:
		finish_hand_and_advance(state);
		return;

	case BJ_ACTION_HIT:
		state[STATE_OFFSET_PHASE] = PHASE_PLAYER_DRAW;
		return;

	case BJ_ACTION_DOUBLE:
		assert(hand[HAND_NCARDS] == 2);

		hand[HAND_FLAGS] |= HAND_FLAG_DOUBLED;
		state[STATE_OFFSET_PHASE] = PHASE_PLAYER_DRAW;
		return;

	case BJ_ACTION_SPLIT: {
		assert(hand[HAND_NCARDS] == 2);
		assert(hand[HAND_CARD1] == hand[HAND_CARD2]);
		assert(state[STATE_OFFSET_NUM_HANDS] < BJ_MAX_HANDS);

		uint64_t* new_hand =
		        hand_ptr(state, state[STATE_OFFSET_NUM_HANDS]);
		const uint64_t rank = hand[HAND_CARD1] - 1;
		/**
		 * A split is only legal on an equal-rank pair (asserted at the
		 * top of the case), deals one card of that rank to each child
		 * hand - so aces is always the same for both hands
		 */
		const uint64_t aces = (rank == RANK_ACE) ? 1 : 0;

		new_hand[HAND_CARD1]  = rank + 1;
		new_hand[HAND_CARD2]  = 0;
		new_hand[HAND_TOTAL]  = rank_value(rank);
		new_hand[HAND_ACES]   = aces;
		new_hand[HAND_NCARDS] = 1;
		new_hand[HAND_FLAGS]  = HAND_FLAG_SPLIT;

		hand[HAND_CARD2]  = 0;
		hand[HAND_TOTAL]  = rank_value(rank);
		hand[HAND_ACES]   = aces;
		hand[HAND_NCARDS] = 1;
		hand[HAND_FLAGS] |= HAND_FLAG_SPLIT;

		state[STATE_OFFSET_NUM_HANDS]++;

		// The current hand draws its replacement card first
		state[STATE_OFFSET_PHASE] = PHASE_PLAYER_DRAW;
		return;
	}

	case BJ_ACTION_SURRENDER:
		assert(state[STATE_OFFSET_NUM_HANDS] == 1);
		assert(hand[HAND_NCARDS] == 2);

		hand[HAND_FLAGS] |= HAND_FLAG_SURRENDERED;
		finish_hand_and_advance(state);
		return;

	default:
		assert(false && "invalid decision action");
	}
}

static void blackjack_apply_action(uint64_t state[], uint64_t action) {
	assert(state[STATE_OFFSET_PHASE] != PHASE_DONE &&
	       "apply_action() called on a terminal state");

	if (blackjack_is_chance_node(state)) {
		apply_chance_action(state, action);
		return;
	}

	apply_decision_action(state, action);
}

static bool blackjack_is_terminal(const uint64_t state[]) {
	return state[STATE_OFFSET_PHASE] == PHASE_DONE;
}

/**
 * Scores are integers in units of 1/BJ_SCORE_UNITS_PER_BET of a bet -
 * see blackjack.h for the caller-facing contract. The payout table below is
 * derived from that constant so the ratios live in exactly one place:
 *
 * 	natural (blackjack) ........ 3:2 ........ +1.5 bets
 * 	win ........................ 1:1 ........ +1 bet (×2 if doubled)
 * 	push ....................................  0
 * 	loss .................................... -1 bet (×2 if doubled)
 * 	late surrender .......................... -0.5 bets
 */
static void blackjack_get_outcome(const uint64_t state[],
                                  int64_t scores_out[]) {
	assert(blackjack_is_terminal(state));

	const int64_t bet     = BJ_SCORE_UNITS_PER_BET;
	const int64_t natural = 3 * bet / 2;

	const bool dealer_bj   = dealer_is_blackjack(state);
	const bool dealer_bust = state[STATE_OFFSET_DEALER_TOTAL] > 21;
	const uint64_t dealer  = dealer_best_total(state);
	int64_t total          = 0;

	if (is_natural_blackjack(state)) {
		scores_out[0] = dealer_bj ? 0 : natural;
		return;
	}

	for (uint64_t h = 0; h < state[STATE_OFFSET_NUM_HANDS]; h++) {
		const uint64_t* hand = hand_cptr(state, h);

		if (hand[HAND_FLAGS] & HAND_FLAG_SURRENDERED) {
			total -= bet / 2;
			continue;
		}

		const int64_t stake =
		        (hand[HAND_FLAGS] & HAND_FLAG_DOUBLED) ? 2 * bet : bet;

		if (dealer_bj) {
			// Round ended at the peek; no doubles can exist
			total -= bet;
			continue;
		}

		if (hand_is_bust(hand)) {
			total -= stake;
			continue;
		}

		const uint64_t best = hand_best_total(hand);

		if (dealer_bust || best > dealer)
			total += stake;
		else if (best < dealer)
			total -= stake;
	}

	scores_out[0] = total;
}

static void blackjack_get_observation(const uint64_t state[], uint64_t player,
                                      uint8_t* obs_out) {
	assert(player < BJ_NUM_PLAYERS);
	(void)player; // Single player; the shoe is omitted (see blackjack.h)

	size_t i = 0;

	obs_out[i++] = (uint8_t)state[STATE_OFFSET_DEALER_UP];

	if (dealer_hand_revealed(state)) {
		obs_out[i++] = (uint8_t)state[STATE_OFFSET_DEALER_HOLE];
		obs_out[i++] = (uint8_t)state[STATE_OFFSET_DEALER_TOTAL];
		obs_out[i++] = (uint8_t)state[STATE_OFFSET_DEALER_ACES];
		obs_out[i++] = (uint8_t)state[STATE_OFFSET_DEALER_NCARDS];
	} else {
		obs_out[i++] = 0; // hole card
		obs_out[i++] = 0; // hard total
		obs_out[i++] = 0; // ace count
		obs_out[i++] = 0; // card count
	}

	obs_out[i++] = (uint8_t)state[STATE_OFFSET_NUM_HANDS];
	obs_out[i++] = (uint8_t)state[STATE_OFFSET_CUR_HAND];

	for (uint64_t h = 0; h < BJ_MAX_HANDS; h++) {
		const uint64_t* hand = hand_cptr(state, h);

		for (size_t f = 0; f < HAND_NUM_WORDS; f++)
			obs_out[i++] = (uint8_t)hand[f];
	}

	assert(i == BJ_OBS_SIZE);
}

static void blackjack_get_features(const uint64_t state[], uint64_t player,
                                   float* features_out) {
	assert(player < BJ_NUM_PLAYERS);
	(void)player;

	size_t i = 0;

	if (state[STATE_OFFSET_DEALER_UP] != 0) {
		const uint64_t rank = state[STATE_OFFSET_DEALER_UP] - 1;
		const uint64_t val = (rank == RANK_ACE) ? 11 : rank_value(rank);

		features_out[i++] = (float)val / 11.0f;
	} else {
		features_out[i++] = 0.0f;
	}

	if (dealer_hand_revealed(state)) {
		/* Clamped like the hand totals: a bust dealer exceeds 21 */
		const float total = (float)dealer_best_total(state) / 21.0f;
		const float cards =
		        (float)state[STATE_OFFSET_DEALER_NCARDS] / 8.0f;

		features_out[i++] = total < 1.0f ? total : 1.0f;
		features_out[i++] = cards < 1.0f ? cards : 1.0f;
	} else {
		features_out[i++] = 0.0f; // best total
		features_out[i++] = 0.0f; // card count
	}

	features_out[i++] =
	        (float)state[STATE_OFFSET_NUM_HANDS] / (float)BJ_MAX_HANDS;
	features_out[i++] =
	        (float)state[STATE_OFFSET_CUR_HAND] / (float)BJ_MAX_HANDS;

	for (uint64_t h = 0; h < BJ_MAX_HANDS; h++) {
		const uint64_t* hand = hand_cptr(state, h);
		const uint64_t hard  = hand[HAND_TOTAL];
		const uint64_t best  = hand_best_total(hand);

		/**
		 * Clamp: busts exceed 21, and a freak hand can exceed 8 cards
		 * (e.g. many aces) without busting
		 */
		const float total = (float)best / 21.0f;
		const float cards = (float)hand[HAND_NCARDS] / 8.0f;

		features_out[i++] = total < 1.0f ? total : 1.0f;
		features_out[i++] = (best != hard) ? 1.0f : 0.0f;
		features_out[i++] = cards < 1.0f ? cards : 1.0f;
		features_out[i++] =
		        (hand[HAND_FLAGS] & HAND_FLAG_DOUBLED) ? 1.0f : 0.0f;
		features_out[i++] =
		        (hand[HAND_FLAGS] & HAND_FLAG_FINISHED) ? 1.0f : 0.0f;
	}

	assert(i == BJ_FEATURES_SIZE);
}

static char rank_char(uint64_t card) {
	// `card` is rank + 1; 0 means no card
	static const char chars[] = "A23456789T";

	return (card == 0) ? '-' : chars[card - 1];
}

/**
 * Append one snprintf result: advance `buf` past what was actually written,
 * clamped to the space available, so a truncated chunk ends the output cleanly
 * instead of being overwritten by later chunks.
 */
static void advance(char** buf, size_t* remaining, int n) {
	const size_t written = (n < 0)                    ? 0
	                       : ((size_t)n < *remaining) ? (size_t)n
	                                                  : *remaining - 1;

	*buf += written;
	*remaining -= written;
}

static uint64_t blackjack_to_string(const uint64_t state[], uint64_t buf_size,
                                    char* buf) {
	assert(buf != NULL);
	assert(buf_size >= BJ_STRING_BUF_SIZE);

	char* const start = buf;
	size_t remaining  = (size_t)buf_size;
	int n;

	const uint64_t phase = state[STATE_OFFSET_PHASE];

	switch (phase) {
	case PHASE_DEAL_PLAYER_1:
	case PHASE_DEAL_DEALER_UP:
	case PHASE_DEAL_PLAYER_2:
	case PHASE_DEAL_DEALER_HOLE:
		n = snprintf(buf, remaining, "Dealing...\n");
		break;
	case PHASE_DECIDE:
		n = snprintf(
		        buf, remaining, "Player to act on hand %llu of %llu\n",
		        (unsigned long long)state[STATE_OFFSET_CUR_HAND] + 1,
		        (unsigned long long)state[STATE_OFFSET_NUM_HANDS]);
		break;
	case PHASE_PLAYER_DRAW:
		n = snprintf(buf, remaining, "Awaiting card for hand %llu\n",
		             (unsigned long long)state[STATE_OFFSET_CUR_HAND] +
		                     1);
		break;
	case PHASE_DEALER_DRAW:
		n = snprintf(buf, remaining, "Awaiting dealer card\n");
		break;
	default:
		n = snprintf(buf, remaining, "Round over\n");
		break;
	}

	advance(&buf, &remaining, n);

	// The hole card is hidden until the player phase is over
	const bool reveal_hole = dealer_hand_revealed(state);

	if (reveal_hole)
		n = snprintf(
		        buf, remaining,
		        "  Dealer: %c %c%s  (total %llu%s, %llu cards)\n",
		        rank_char(state[STATE_OFFSET_DEALER_UP]),
		        rank_char(state[STATE_OFFSET_DEALER_HOLE]),
		        state[STATE_OFFSET_DEALER_NCARDS] > 2 ? " +" : "",
		        (unsigned long long)dealer_best_total(state),
		        state[STATE_OFFSET_DEALER_TOTAL] > 21 ? " BUST" : "",
		        (unsigned long long)state[STATE_OFFSET_DEALER_NCARDS]);
	else
		n = snprintf(buf, remaining, "  Dealer: %c ?\n",
		             rank_char(state[STATE_OFFSET_DEALER_UP]));

	advance(&buf, &remaining, n);

	/* '>' marks the hand the next player action or card applies to */
	const bool player_phase =
	        phase == PHASE_DECIDE || phase == PHASE_PLAYER_DRAW;

	for (uint64_t h = 0; h < state[STATE_OFFSET_NUM_HANDS]; h++) {
		const uint64_t* hand = hand_cptr(state, h);
		const uint64_t hard  = hand[HAND_TOTAL];
		const uint64_t best  = hand_best_total(hand);
		const uint64_t flags = hand[HAND_FLAGS];
		const bool active =
		        player_phase && h == state[STATE_OFFSET_CUR_HAND];

		n = snprintf(buf, remaining,
		             "%sHand %llu: %c %c%s  total %llu%s%s%s%s%s\n",
		             active ? "> " : "  ", (unsigned long long)h + 1,
		             rank_char(hand[HAND_CARD1]),
		             rank_char(hand[HAND_CARD2]),
		             hand[HAND_NCARDS] > 2 ? " +" : "",
		             (unsigned long long)best,
		             (best != hard) ? " (soft)" : "",
		             hard > 21 ? " BUST" : "",
		             (flags & HAND_FLAG_DOUBLED) ? " [doubled]" : "",
		             (flags & HAND_FLAG_SURRENDERED) ? " [surrendered]"
		                                             : "",
		             (flags & HAND_FLAG_FINISHED) ? " [done]" : "");
		advance(&buf, &remaining, n);
	}

	return (uint64_t)(buf - start);
}

static const char* blackjack_help_prompt(void) {
	return "Blackjack\n"
	       "=========\n"
	       "\n"
	       "One player versus the dealer. On your turn, choose:\n"
	       "\n"
	       "  0 STAND:     end your turn on this hand\n"
	       "  1 HIT:       draw another card\n"
	       "  2 DOUBLE:    draw exactly one more card, double the\n"
	       "               stake (two-card hands only)\n"
	       "  3 SPLIT:     split an equal-rank pair into two hands\n"
	       "               (up to 4 hands; split aces get one card\n"
	       "               each)\n"
	       "  4 SURRENDER: forfeit half the stake (first decision of\n"
	       "               the round only)\n"
	       "\n"
#if BJ_DEALER_HITS_SOFT_17
	       "The dealer hits soft 17 and pays 3:2 on a natural.\n"
#else
	       "The dealer stands on all 17s and pays 3:2 on a natural.\n"
#endif
	       "\n"
	       "Payouts:\n"
	       "  natural (blackjack) ........ 3:2 ........ +1.5 bets\n"
	       "  win ........................ 1:1 ........ +1 bet "
	       "(×2 if doubled)\n"
	       "  push ....................................  0\n"
	       "  loss .................................... -1 bet "
	       "(×2 if doubled)\n"
	       "  late surrender .......................... -0.5 bets\n"
	       "\n"
	       "(Raw scores from get_outcome() are integers in half-bet\n"
	       "units: bets × 2.)\n";
}

const Game blackjack = {
	.init               = blackjack_init,
	.get_current_player = blackjack_get_current_player,
	.is_chance_node     = blackjack_is_chance_node,
	.get_valid_actions  = blackjack_get_valid_actions,
	.apply_action       = blackjack_apply_action,
	.is_terminal        = blackjack_is_terminal,
	.get_outcome        = blackjack_get_outcome,
	.get_observation    = blackjack_get_observation,
	.get_features       = blackjack_get_features,
	.to_string          = blackjack_to_string,
	.help_prompt        = blackjack_help_prompt,
	.obs_dims           = { BJ_OBS_SIZE },
	.features_dims      = { BJ_FEATURES_SIZE },
};
