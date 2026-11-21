#ifndef BLACKJACK_H
#define BLACKJACK_H

#include "board_game.h"

/**
 * Number of 52-card decks in the shoe. Classic casino blackjack is dealt from
 * a 4-to 8-deck shoe.
 */
#define BLACKJACK_NUM_DECKS 6

#if BLACKJACK_NUM_DECKS < 4 || BLACKJACK_NUM_DECKS > 8
#error "BLACKJACK_NUM_DECKS must be in [4, 8]"
#endif

/**
 * Classic ("Vegas Strip") rules have the dealer stand on soft 17 (S17).
 * Define this to 1 for H17 tables (dealer *hits* soft 17).
 */
#define BLACKJACK_DEALER_HITS_SOFT_17 0

/**
 * Blackjack is a one-player game against the house. The dealer plays a fixed,
 * deterministic strategy and is therefore not an agent. Dealer card draws,
 * like all card draws, are chance nodes.
 *
 * As per the convention in this codebase, the deck i.e. chance "player" is
 * assigned index BLACKJACK_NUM_PLAYERS (= 1). When get_current_player()
 * returns this value, the caller must sample one of the actions returned by
 * get_valid_actions() uniformly at random and pass it to apply_action().
 */
#define BLACKJACK_NUM_PLAYERS 1

/**
 * A pair may be split up to three times, for at most four simultaneous
 * hands (classic rules).
 * Split aces receive exactly one card each and may not be re-split.
 */
#define BLACKJACK_MAX_HANDS 4

/**
 * get_outcome() writes the round's result as an integer count of
 * *score units*, where one bet is BLACKJACK_SCORE_UNITS_PER_BET units.
 * A unit of half a bet keeps every payout in the classic table exact:
 *
 * 	natural (blackjack) ........ 3:2 ........ +3 units (+1.5 bets)
 * 	win ........................ 1:1 ........ +2 units (×2 if doubled)
 * 	push ....................................  0
 * 	loss .................................... -2 units (×2 if doubled)
 * 	late surrender .......................... -1 unit  (-0.5 bets)
 *
 * Divide a score by BLACKJACK_SCORE_UNITS_PER_BET to express it in bets.
 * Scores lie within +/- 2 * BLACKJACK_MAX_HANDS bets (worst case: every hand
 * split, doubled, and won/lost).
 *
 * The payout table in blackjack_get_outcome() is derived from this constant,
 * so payout ratios live in exactly one place.
 * If a ratio is ever changed (e.g. a 6:5 natural table), set this constant to
 * the LCM of the payout denominators (e.g. 10 for 6:5 + half-bet surrender) so
 * every payout stays integral, and be sure to update the table quoted in
 * help_prompt() to match.
 *
 * Note: For training agents, it's fine to use the direct output of
 * get_outcome(), i.e. don't convert to bets using
 * BLACKJACK_SCORE_UNITS_PER_BET.
 * This is because the reward is just being scaled by a positive constant,
 * which leaves the optimal policy unchanged.
 * This is mainly useful if you want to express the performance in a more
 * human-friendly format i.e. bets while logging or plotting
 */
#define BLACKJACK_SCORE_UNITS_PER_BET 2

/**
 * Card ranks are mapped to the 10 blackjack-specific ranks:
 *
 * 	rank:  0   1   2   3   4   5   6   7   8   9
 * 	card:  A   2   3   4   5   6   7   8   9   T
 *
 * where T stands for any ten-valued card: 10, J, Q, K. A full shoe therefore
 * holds 4 * BLACKJACK_NUM_DECKS cards of each rank 0..8 and
 * 16 * BLACKJACK_NUM_DECKS cards of rank 9.
 *
 * Collapsing 10/J/Q/K into one rank is lossless under standard rules: any two
 * ten-valued cards form a splittable pair - the standard casino rule. No other
 * rule distinguishes them, and draw statistics match a physical shoe exactly
 * because all ten-value cards are interchangeable. Only the rare "split by rank"
 * variant (e.g. 10-10 splits, 10-Q does not) would require distinct 10/J/Q/K
 * ranks.
 */
#define BLACKJACK_NUM_RANKS 10
#define BLACKJACK_SHOE_SIZE (52 * BLACKJACK_NUM_DECKS)

/*
 * Hand record fields (6 words per hand)
 * [card1, card2, hard total, aces, ncards, flags]
 */
#define HAND_NUM_WORDS 6

#define BLACKJACK_STATE_SIZE (18 + HAND_NUM_WORDS * BLACKJACK_MAX_HANDS)

/**
 * Decision actions (when it is the player's turn to decide).
 * 	- STAND and HIT are always available
 * 	- DOUBLE requires a two-card hand
 * 	- SPLIT additionally an equal-rank pair and a free hand slot
 * 	- SURRENDER only the very first decision of the round
 *
 * See get_valid_actions().
 *
 * At chance nodes, action IDs are card ranks (0..9).
 */
#define BLACKJACK_ACTION_STAND     0
#define BLACKJACK_ACTION_HIT       1
#define BLACKJACK_ACTION_DOUBLE    2
#define BLACKJACK_ACTION_SPLIT     3
#define BLACKJACK_ACTION_SURRENDER 4

/**
 * Max across all state types:
 * 	- Decision nodes: at most 5 actions
 * 	  (STAND, HIT, DOUBLE, SPLIT, SURRENDER)
 * 	- Chance nodes: one action entry *per remaining physical card*.
 * 	  Entries are duplicated per rank so that a uniform sample over the
 * 	  action array reproduces draw-without-replacement from the shoe -
 * 	  bounded by the full shoe size.
 */
#define BLACKJACK_MAX_NUM_ACTIONS BLACKJACK_SHOE_SIZE

/**
 * Observation: flat uint8 vector, from the player's perspective. The dealer's
 * hole card is *not* observable while face down: the hole-card/dealer-total
 * entries read 0 until the dealer's hand is revealed (dealer's turn onwards).
 *
 * 	[0]:      dealer upcard (rank + 1, 0 if not yet dealt)
 * 	[1]:      dealer hole card (rank + 1, 0 until revealed)
 * 	[2]:      dealer hard total (0 until revealed)
 * 	[3]:      dealer ace count (0 until revealed)
 * 	[4]:      dealer card count (0 until revealed)
 * 	[5]:      number of player hands
 * 	[6]:      index of the hand currently being played
 * 	[7..]:    per hand (6 values * BLACKJACK_MAX_HANDS):
 * 	              [card1, card2, hard total, ace count, card count, flags]
 */
#define BLACKJACK_OBS_NDIMS 1
#define BLACKJACK_OBS_SIZE  (7 + HAND_NUM_WORDS * BLACKJACK_MAX_HANDS)

/**
 * Features: flat float vector, normalised to [0, 1]. Ratios that can exceed 1
 * in rare states (bust totals, many-card hands) are clamped.
 * Dealer-hand entries read 0 until the hole card is revealed (dealer's turn
 * onwards).
 *
 * 	[0]:      dealer upcard value / 11 (ace = 11), 0 if not dealt
 * 	[1]:      dealer best total / 21 (0 until revealed, clamped)
 * 	[2]:      dealer card count / 8 (0 until revealed, clamped)
 * 	[3]:      num hands / BLACKJACK_MAX_HANDS
 * 	[4]:      current hand index / BLACKJACK_MAX_HANDS
 * 	[5..]:    per hand (5 values * BLACKJACK_MAX_HANDS):
 * 	              [best total / 21, is soft, card count / 8,
 * 	               doubled, finished]
 */
#define BLACKJACK_FEATURES_NDIMS 1
#define BLACKJACK_FEATURES_SIZE  (5 + 5 * BLACKJACK_MAX_HANDS)

/**
 * String buffer size: phase line + dealer line + one line per hand + null
 * terminator. Generous upper bound.
 */
#define BLACKJACK_STRING_BUF_SIZE (192 + 64 * BLACKJACK_MAX_HANDS)

extern const Game blackjack;

#endif
