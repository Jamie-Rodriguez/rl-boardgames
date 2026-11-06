#ifndef BLACKJACK_H
#define BLACKJACK_H

#include "games/board_game.h"

/**
 * Classic ("Vegas Strip") rules have the dealer stand on soft 17 (S17).
 * Define this to 1 for H17 tables (dealer *hits* soft 17).
 */
#define DEALER_HITS_SOFT_17 0

/**
 * Number of 52-card decks in the shoe. Classic casino blackjack is dealt from
 * a 4-to 8-deck shoe.
 */
#define BJ_NUM_DECKS 6

#if BJ_NUM_DECKS < 4 || BJ_NUM_DECKS > 8
#error "BJ_NUM_DECKS must be in [4, 8]"
#endif

#define BJ_SHOE_SIZE (52 * BJ_NUM_DECKS)

/**
 * Blackjack is a one-player game against the house. The dealer plays a fixed,
 * deterministic strategy and is therefore not an agent. Dealer card draws,
 * like all card draws, are chance nodes.
 *
 * As per the convention in this codebase, the deck i.e. chance "player" is
 * assigned index BJ_NUM_PLAYERS (= 1). When get_current_player() returns this
 * value, the caller must sample one of the actions returned by
 * get_valid_actions() uniformly at random and pass it to apply_action().
 */
#define BJ_NUM_PLAYERS 1

/**
 * A pair may be split up to three times, for at most four simultaneous
 * hands (classic rules).
 * Split aces receive exactly one card each and may not be re-split.
 */
#define BJ_MAX_HANDS 4

/**
 * get_outcome() writes the round's result as an integer count of
 * *score units*, where one bet is BJ_SCORE_UNITS_PER_BET units.
 * A unit of half a bet keeps every payout in the classic table exact:
 *
 * 	natural (blackjack) ........ 3:2 ........ +3 units (+1.5 bets)
 * 	win ........................ 1:1 ........ +2 units (×2 if doubled)
 * 	push ....................................  0
 * 	loss .................................... -2 units (×2 if doubled)
 * 	late surrender .......................... -1 unit  (-0.5 bets)
 *
 * Divide a score by BJ_SCORE_UNITS_PER_BET to express it in bets.
 * Scores lie within +/- 2 * BJ_MAX_HANDS bets (worst case: every hand split,
 * doubled, and won/lost).
 *
 * The payout table in blackjack_get_outcome() is derived from this constant.
 * If a ratio is ever changed (e.g. a 6:5 natural table), set this constant to
 * the LCM of the payout denominators (e.g. 10 for 6:5 + half-bet surrender) so
 * every payout stays integral, and be sure to update the table quoted in
 * help_prompt() to match.
 *
 * Note: For training agents, it's fine to use the direct output of
 * get_outcome(), i.e. don't convert to bets using BJ_SCORE_UNITS_PER_BET.
 * This is because the reward is just being scaled by a positive constant,
 * which leaves the optimal policy unchanged.
 * This is mainly useful if you want to express the performance in a more
 * human-friendly format i.e. bets while logging or plotting
 */
#define BJ_SCORE_UNITS_PER_BET 2

/**
 * State: BJ_STATE_FIXED_WORDS fixed words followed by one record of
 * BJ_STATE_WORDS_PER_HAND words for each of the BJ_MAX_HANDS hands.
 * The fixed words are:
 *
 * 	 1  round phase
 * 	10  shoe: cards left of each rank
 * 	 5  dealer: upcard, hole card, hard total, ace count, card count
 * 	 1  hand count
 * 	 1  index of the current hand
 *
 * The hand records then follow the initial fixed words (indices shown for
 * BJ_MAX_HANDS = 4):
 *
 * 	   0..17    18..23   24..29   30..35   36..41
 * 	+---------+--------+--------+--------+--------+
 * 	|  fixed  | hand 0 | hand 1 | hand 2 | hand 3 |
 * 	|  words  |        |        |        |        |
 * 	+---------+--------+--------+--------+--------+
 * 	         /          \
 * 	 _______/            \__________________________
 * 	/                                               \
 * 	+-------+-------+-------+-------+-------+-------+
 * 	| card1 | card2 | hard  |  ace  | card  | flags |
 * 	|       |       | total | count | count |       |
 * 	+-------+-------+-------+-------+-------+-------+
 * 	   +0      +1      +2      +3      +4      +5
 */
#define BJ_STATE_FIXED_WORDS    18
#define BJ_STATE_WORDS_PER_HAND 6
#define BJ_STATE_SIZE \
	(BJ_STATE_FIXED_WORDS + BJ_STATE_WORDS_PER_HAND * BJ_MAX_HANDS)

/**
 * Decision actions (when it is the player's turn to decide).
 * 	- STAND and HIT are always available
 * 	- DOUBLE requires a two-card hand
 * 	- SPLIT additionally an equal-rank pair and a free hand slot
 * 	- SURRENDER only the very first decision of the round
 *
 * See get_valid_actions().
 *
 * At chance nodes, action IDs are card ranks:
 *
 * 	rank:  0   1   2   3   4   5   6   7   8   9
 * 	card:  A   2   3   4   5   6   7   8   9   T
 *
 * where T stands for any ten-valued card: 10, J, Q, K.
 */
#define BJ_ACTION_STAND     0
#define BJ_ACTION_HIT       1
#define BJ_ACTION_DOUBLE    2
#define BJ_ACTION_SPLIT     3
#define BJ_ACTION_SURRENDER 4

/* STAND, HIT, DOUBLE, SPLIT SURRENDER */
#define BJ_MAX_NUM_DECISION_ACTIONS 5
/* The remaining cards in the shoe */
#define BJ_MAX_NUM_CHANCE_ACTIONS BJ_SHOE_SIZE
#define BJ_MAX_NUM_ACTIONS        BJ_MAX_NUM_CHANCE_ACTIONS

/* Decision IDs 0..4 (STAND..SURRENDER) are dense */
#define BJ_ACTION_SPACE_SIZE BJ_MAX_NUM_DECISION_ACTIONS

/**
 * Loose bound on player decisions per round: a live hand can grow to
 * at most 21 cards (all aces) before it must stand or bust, so no
 * hand takes more than ~22 decisions (its hits, its final action and
 * the split that created it), across at most BJ_MAX_HANDS hands.
 */
#define BJ_MAX_TURNS (22 * BJ_MAX_HANDS)

/**
 * The shoe composition alone admits (4 * BJ_NUM_DECKS + 1)^9 *
 * (16 * BJ_NUM_DECKS + 1) ~ 4 * 10^14 states with the default
 * six-deck shoe; combined with the hand records the count exceeds
 * what a uint64 can hold, so this saturates per the convention in
 * board_game.h.
 */
#define BJ_STATE_SPACE_SIZE UINT64_MAX

/**
 * Observation: flat uint64 vector, from the player's perspective at one of
 * their decisions: BJ_OBS_FIXED_FIELDS fixed fields followed by one block of
 * BJ_OBS_FIELDS_PER_HAND fields for each hand (indices shown for
 * BJ_MAX_HANDS = 4):
 *
 * 	    0         1       2..7     8..13   14..19   20..25
 * 	+--------+---------+--------+--------+--------+--------+
 * 	| dealer | current | hand 0 | hand 1 | hand 2 | hand 3 |
 * 	| upcard |  hand   |        |        |        |        |
 * 	+--------+---------+--------+--------+--------+--------+
 * 	                  /          \
 * 	 ________________/            \___________________
 * 	/                                                 \
 * 	+-------+-------+-------+-------+-------+---------+
 * 	| card1 | card2 | hard  |  ace  | card  | doubled |
 * 	|       |       | total | count | count |  flag   |
 * 	+-------+-------+-------+-------+-------+---------+
 * 	   +0      +1      +2      +3      +4       +5
 *
 * Cards are rank + 1 (0 = no card); the hard total counts every ace as 1.
 * Blocks of hands not yet in play are all zero.
 */
#define BJ_OBS_NDIMS           1
#define BJ_OBS_FIXED_FIELDS    2
#define BJ_OBS_FIELDS_PER_HAND 6
#define BJ_OBS_SIZE \
	(BJ_OBS_FIXED_FIELDS + BJ_OBS_FIELDS_PER_HAND * BJ_MAX_HANDS)

/**
 * Features: flat float vector, normalised to [0, 1]: BJ_FEATURES_FIXED_FIELDS
 * fixed fields followed by one block of BJ_FEATURES_FIELDS_PER_HAND fields for
 * each hand (indices shown for BJ_MAX_HANDS = 4):
 *
 * 	    0         1       2..7     8..13   14..19   20..25
 * 	+--------+---------+--------+--------+--------+--------+
 * 	| dealer | current | hand 0 | hand 1 | hand 2 | hand 3 |
 * 	| upcard |  hand   |        |        |        |        |
 * 	+--------+---------+--------+--------+--------+--------+
 * 	                  /          \
 * 	 ________________/            \___________________
 * 	/                                                 \
 * 	+-------+-------+-------+-------+-------+---------+
 * 	| best  | soft  | bust  | pair  | card  | doubled |
 * 	| total | flag  | flag  | flag  | count |  flag   |
 * 	+-------+-------+-------+-------+-------+---------+
 * 	   +0      +1      +2      +3      +4       +5
 */
#define BJ_FEATURES_NDIMS           1
#define BJ_FEATURES_FIXED_FIELDS    2
#define BJ_FEATURES_FIELDS_PER_HAND 6
#define BJ_FEATURES_SIZE \
	(BJ_FEATURES_FIXED_FIELDS + BJ_FEATURES_FIELDS_PER_HAND * BJ_MAX_HANDS)

/**
 * String buffer size: phase line + dealer line + one line per hand + null
 * terminator. Generous upper bound.
 */
#define BJ_STRING_BUF_SIZE (192 + 64 * BJ_MAX_HANDS)

extern const Game blackjack;

#endif
