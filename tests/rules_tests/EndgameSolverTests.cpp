#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/EndgameSolver.h"
#include "rules/CppCompat.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <random>
#include <set>
#include <vector>

using namespace pdk;
using tests::C;

namespace {

using RankCounts = std::array<uint8_t, RANK_TWO + 1>;

RankCounts CountsOf(const Cards& cards) {
    RankCounts counts{};
    for (int i = 0; i < cards.count; ++i) {
        counts[cards.items[i].rank]++;
    }
    return counts;
}

void SetHand(EndgamePosition& position, int seat, const Cards& cards) {
    const RankCounts counts = CountsOf(cards);
    std::memcpy(position.counts[seat], counts.data(), counts.size());
}

// Every distinct legal play by the real validator: all subsets of the hand.
std::set<RankCounts> ReferencePlays(const Cards& hand, const EndgamePosition& position) {
    std::set<RankCounts> plays;
    for (int mask = 1; mask < (1 << hand.count); ++mask) {
        Cards play;
        Cards_Clear(&play);
        for (int i = 0; i < hand.count; ++i) {
            if ((mask & (1 << i)) != 0) {
                Cards_Push(&play, hand.items[i]);
            }
        }
        const MoveValidation validation = position.hasLast
            ? ::ValidateFollow(&play, &position.last, hand.count)
            : ::ValidateLead(&play, hand.count);
        if (validation.ok) {
            plays.insert(CountsOf(play));
        }
    }
    return plays;
}

Cards CardsOf(const uint8_t* counts) {
    Cards cards;
    Cards_Clear(&cards);
    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        for (int i = 0; i < counts[rank]; ++i) {
            Cards_Push(&cards, C(static_cast<Rank>(rank), static_cast<Suit>(i)));
        }
    }
    return cards;
}

int CardCount(const uint8_t* counts) {
    int total = 0;
    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        total += counts[rank];
    }
    return total;
}

int LoserPoints(const EndgamePosition& p, int seat) {
    const int left = CardCount(p.counts[seat]);
    return !p.played[seat] ? 32 : (left == 1 ? 0 : left);
}

// Plain minimax with the real validator over every subset and no pruning or table: the
// reference value for Endgame_Solve.  Same rules: a seat that can beat the last play
// must play, a bomb nobody beats scores when its trick ends or when it ends the hand.
int ReferenceValue(const EndgamePosition& p, int human) {
    const Cards hand = CardsOf(p.counts[p.current]);
    std::vector<std::pair<RankCounts, HandPattern>> plays;
    for (int mask = 1; mask < (1 << hand.count); ++mask) {
        Cards play;
        Cards_Clear(&play);
        for (int i = 0; i < hand.count; ++i) {
            if ((mask & (1 << i)) != 0) {
                Cards_Push(&play, hand.items[i]);
            }
        }
        const MoveValidation validation = p.hasLast
            ? ::ValidateFollow(&play, &p.last, hand.count)
            : ::ValidateLead(&play, hand.count);
        if (validation.ok) {
            plays.emplace_back(CountsOf(play), validation.pattern);
        }
    }
    const int bombValueOfLast = p.lastMover == human ? 20 : -10;
    if (plays.empty()) {
        EndgamePosition child = p;
        child.passCount++;
        if (child.passCount < 2) {
            child.current = Endgame_NextSeat(child.current);
            return ReferenceValue(child, human);
        }
        const int bomb = child.last.type == PATTERN_BOMB ? bombValueOfLast : 0;
        child.hasLast = false;
        child.current = child.lastMover;
        child.passCount = 0;
        return bomb + ReferenceValue(child, human);
    }
    const bool maximise = p.current == human;
    int best = maximise ? -100000 : 100000;
    for (const auto& [counts, pattern] : plays) {
        EndgamePosition child = p;
        for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
            child.counts[p.current][rank] = static_cast<uint8_t>(child.counts[p.current][rank] - counts[rank]);
        }
        child.played[p.current] = true;
        int value;
        if (CardCount(child.counts[p.current]) == 0) {
            value = pattern.type == PATTERN_BOMB ? (p.current == human ? 20 : -10) : 0;
            if (p.current == human) {
                for (int seat = 0; seat < 3; ++seat) {
                    if (seat != human) {
                        value += LoserPoints(child, seat);
                    }
                }
            } else {
                value -= LoserPoints(child, human);
            }
        } else {
            child.last = pattern;
            child.hasLast = true;
            child.lastMover = p.current;
            child.passCount = 0;
            child.current = Endgame_NextSeat(p.current);
            value = ReferenceValue(child, human);
        }
        best = maximise ? std::max(best, value) : std::min(best, value);
    }
    return best;
}

} // namespace

TEST_CASE("endgame play generation matches the subset search through the validator") {
    Cards sortedDeck = rules::CreatePaoDeKuaiDeck();
    std::sort(sortedDeck.items, sortedDeck.items + sortedDeck.count,
        [](Card lhs, Card rhs) { return lhs.rank < rhs.rank; });
    std::mt19937 rng(20261020u);
    int checked = 0;
    int follows = 0;

    for (int round = 0; round < 600; ++round) {
        // A window of neighbouring ranks makes triples, runs and planes common.
        Cards source = sortedDeck;
        const int window = 24;
        const int offset = std::uniform_int_distribution<int>(0, source.count - window)(rng);
        std::shuffle(source.items + offset, source.items + offset + window, rng);
        const int size = std::uniform_int_distribution<int>(1, 12)(rng);
        Cards hand;
        Cards_Clear(&hand);
        for (int i = 0; i < size; ++i) {
            Cards_Push(&hand, source.items[offset + i]);
        }

        EndgamePosition position{};
        position.current = 1;
        SetHand(position, 1, hand);
        if (round % 2 == 1) {
            // Follow one of the legal plays of the rest of the window.
            Cards other;
            Cards_Clear(&other);
            for (int i = size; i < window && other.count < 10; ++i) {
                Cards_Push(&other, source.items[offset + i]);
            }
            std::vector<HandPattern> leads;
            for (int mask = 1; mask < (1 << other.count); ++mask) {
                Cards play;
                Cards_Clear(&play);
                for (int i = 0; i < other.count; ++i) {
                    if ((mask & (1 << i)) != 0) {
                        Cards_Push(&play, other.items[i]);
                    }
                }
                const HandPattern pattern = ::IdentifyPattern(&play, -1, false).pattern;
                if (HandPattern_IsValid(&pattern)) {
                    leads.push_back(pattern);
                }
            }
            if (leads.empty()) {
                continue;
            }
            position.hasLast = true;
            position.last = leads[std::uniform_int_distribution<std::size_t>(0, leads.size() - 1)(rng)];
            follows++;
        }

        uint8_t plays[192][RANK_TWO + 1];
        HandPattern patterns[192];
        const int count = Endgame_LegalPlays(&position, plays, patterns, 192);
        std::set<RankCounts> generated;
        for (int i = 0; i < count; ++i) {
            RankCounts counts{};
            std::memcpy(counts.data(), plays[i], counts.size());
            generated.insert(counts);
        }
        INFO("round " << round << " hand size " << hand.count << " follow " << position.hasLast);
        CHECK(generated == ReferencePlays(hand, position));
        checked++;
    }
    CHECK(checked > 400);
    CHECK(follows > 150);
}

TEST_CASE("endgame solver values match plain minimax through the validator") {
    // Small random three-hand endgames from a deck window of neighbouring ranks (so
    // pairs, triples, runs and bombs all occur), with random seat to move, human seat
    // and "has played" flags; alpha-beta and the transposition table must not change
    // the value.
    Cards sortedDeck = rules::CreatePaoDeKuaiDeck();
    std::sort(sortedDeck.items, sortedDeck.items + sortedDeck.count,
        [](Card lhs, Card rhs) { return lhs.rank < rhs.rank; });
    std::mt19937 rng(20261019u);
    int solved = 0;
    int bombs = 0;

    for (int round = 0; round < 300; ++round) {
        Cards source = sortedDeck;
        const int window = 20;
        // Every fourth round seat 0 holds a whole rank (the sorted deck starts 3333, so
        // a multiple of 4 is a rank boundary) plus one card: a bomb.
        const bool withBomb = round % 4 == 0;
        const int offset = withBomb
            ? 4 * std::uniform_int_distribution<int>(0, 7)(rng)
            : std::uniform_int_distribution<int>(0, source.count - window)(rng);
        std::shuffle(source.items + offset + (withBomb ? 4 : 0), source.items + offset + window, rng);
        EndgamePosition position{};
        int next = offset;
        for (int seat = 0; seat < 3; ++seat) {
            const int size = withBomb && seat == 0 ? 5 : std::uniform_int_distribution<int>(1, 4)(rng);
            Cards hand;
            Cards_Clear(&hand);
            for (int i = 0; i < size; ++i) {
                Cards_Push(&hand, source.items[next++]);
            }
            SetHand(position, seat, hand);
            position.played[seat] = std::uniform_int_distribution<int>(0, 3)(rng) != 0;
            for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
                bombs += position.counts[seat][rank] == 4 ? 1 : 0;
            }
        }
        position.current = std::uniform_int_distribution<int>(0, 2)(rng);
        position.lastMover = position.current;
        const int human = std::uniform_int_distribution<int>(0, 2)(rng);

        int value = 0;
        INFO("round " << round << " human " << human << " current " << position.current);
        REQUIRE(Endgame_Solve(&position, human, 10000000, &value));
        CHECK(value == ReferenceValue(position, human));
        solved++;
    }
    CHECK(solved == 300);
    CHECK(bombs > 0);
}

TEST_CASE("endgame solver blocks a one-card human with the high single") {
    // AI1 leads with {5, A}; the human (next) holds a single K.  Leading the 5 lets the
    // human out; leading the A first wins.  Value: the human's points.
    EndgamePosition position{};
    SetHand(position, 0, MakeCards({C(RANK_KING)}));
    SetHand(position, 1, MakeCards({C(RANK_FIVE), C(RANK_ACE)}));
    SetHand(position, 2, MakeCards({C(RANK_THREE), C(RANK_FOUR), C(RANK_SIX)}));
    position.played[0] = position.played[1] = position.played[2] = true;
    position.current = 1;
    position.lastMover = 1;

    int value = 0;
    REQUIRE(Endgame_Solve(&position, 0, 100000, &value));
    // AI1 leads A (nobody beats it), then 5 and goes out: the human pays nothing for
    // a single last card.
    CHECK(value == 0);

    // With the human holding two cards instead, losing costs the human 2 points.
    SetHand(position, 0, MakeCards({C(RANK_KING), C(RANK_THREE, SUIT_HEARTS)}));
    REQUIRE(Endgame_Solve(&position, 0, 100000, &value));
    CHECK(value == -2);
}

TEST_CASE("endgame solver counts a never-played hand as 32") {
    // The human leads and goes out with a straight; AI2 never played and pays 32, AI1
    // (who played) pays its 2 cards.
    EndgamePosition position{};
    SetHand(position, 0, MakeCards({C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN)}));
    SetHand(position, 1, MakeCards({C(RANK_NINE), C(RANK_TEN)}));
    SetHand(position, 2, MakeCards({C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING)}));
    position.played[0] = position.played[1] = true;
    position.played[2] = false;
    position.current = 0;
    position.lastMover = 0;

    int value = 0;
    REQUIRE(Endgame_Solve(&position, 0, 100000, &value));
    CHECK(value == 2 + 32);
}

TEST_CASE("endgame solver gives up beyond its node budget") {
    EndgamePosition position{};
    SetHand(position, 0, MakeCards({C(RANK_THREE), C(RANK_FIVE), C(RANK_SEVEN), C(RANK_NINE), C(RANK_JACK), C(RANK_KING)}));
    SetHand(position, 1, MakeCards({C(RANK_FOUR), C(RANK_SIX), C(RANK_EIGHT), C(RANK_TEN), C(RANK_QUEEN), C(RANK_ACE)}));
    SetHand(position, 2, MakeCards({C(RANK_THREE, SUIT_HEARTS), C(RANK_FIVE, SUIT_HEARTS), C(RANK_SEVEN, SUIT_HEARTS), C(RANK_NINE, SUIT_HEARTS)}));
    position.played[0] = position.played[1] = position.played[2] = true;
    position.current = 1;
    position.lastMover = 1;

    int value = 12345;
    CHECK_FALSE(Endgame_Solve(&position, 0, 3, &value));
    CHECK(value == 12345);
    CHECK(Endgame_Solve(&position, 0, 1000000, &value));
}
