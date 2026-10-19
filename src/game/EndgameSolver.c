#include "game/EndgameSolver.h"

#include "rules/MoveValidator.h"

#include <stdlib.h>
#include <string.h>

/*
 * Plays are generated from shapes (singles, pairs, triples with kickers, runs, runs of
 * pairs, planes with kickers, bombs and the whole hand) and every candidate selection is
 * run through IdentifyPatternCounts, the same identification the game validates with,
 * so a shape that is not a legal play is simply dropped.  tests/rules_tests compares the
 * result with the full subset search.
 *
 * The search is a fail-hard alpha-beta over "points of the human seat" with a small
 * transposition table allocated per solve (the strategy runs solves on several pool
 * threads at once, so nothing here is shared).
 */

enum {
    ENDGAME_MAX_PLAYS = 192,
    ENDGAME_TT_BITS = 13,
    ENDGAME_TT_SIZE = 1 << ENDGAME_TT_BITS
};

enum { TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3 };

int Endgame_NextSeat(int seat)
{
    return (seat + 2) % 3;
}

static int CountCards(const uint8_t *counts)
{
    int total = 0;

    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        total += counts[rank];
    }
    return total;
}

/* Base-5 packing of a rank-count vector: 5^13 < 2^31. */
static uint32_t PackCounts(const uint8_t *counts)
{
    uint32_t key = 0;

    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        key = key * 5u + counts[rank];
    }
    return key;
}

/* ---- play generation --------------------------------------------------- */

typedef struct PlayBuilder {
    const uint8_t *hand;
    int handSize;
    bool leading;
    const HandPattern *last;
    uint8_t (*plays)[RANK_TWO + 1];
    HandPattern *patterns;
    uint32_t keys[ENDGAME_MAX_PLAYS];
    int count;
    int cap;
} PlayBuilder;

static void Offer(PlayBuilder *builder, const uint8_t *selection)
{
    const int total = CountCards(selection);
    HandPattern pattern;
    uint32_t key;

    if (total == 0 || builder->count >= builder->cap) {
        return;
    }
    /* Cheap card-count filter before identification: only a bomb (4 cards) or the same
     * class can beat the previous play. */
    if (!builder->leading && total != 4) {
        if (builder->last->type == PATTERN_PLANE) {
            if (total < builder->last->groupCount * 4 || total > builder->last->groupCount * 5) {
                return;
            }
        } else if (total != builder->last->cardCount) {
            return;
        }
    }
    if (!IdentifyPatternCounts(selection, builder->handSize, builder->leading, &pattern)) {
        return;
    }
    if (!builder->leading && !CanBeat(&pattern, builder->last)) {
        return;
    }
    key = PackCounts(selection);
    for (int i = 0; i < builder->count; ++i) {
        if (builder->keys[i] == key) {
            return;
        }
    }
    builder->keys[builder->count] = key;
    memcpy(builder->plays[builder->count], selection, RANK_TWO + 1);
    builder->patterns[builder->count] = pattern;
    builder->count++;
}

/* Every multiset of `left` more cards from `avail`, ranks >= `start`, added to
 * `selection` and offered. */
static void OfferWithKickers(PlayBuilder *builder, uint8_t *selection, uint8_t *avail,
                             int start, int left)
{
    if (left == 0) {
        Offer(builder, selection);
        return;
    }
    for (int rank = start; rank <= RANK_TWO && builder->count < builder->cap; ++rank) {
        if (avail[rank] == 0) {
            continue;
        }
        avail[rank]--;
        selection[rank]++;
        OfferWithKickers(builder, selection, avail, rank, left - 1);
        selection[rank]--;
        avail[rank]++;
    }
}

static void GeneratePlays(PlayBuilder *builder)
{
    const uint8_t *hand = builder->hand;
    uint8_t selection[RANK_TWO + 1];
    uint8_t avail[RANK_TWO + 1];

    /* The whole hand first: it ends the round, and it covers the short-final leads. */
    Offer(builder, hand);

    for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
        for (int n = 1; n <= hand[rank] && n <= 4; ++n) {
            memset(selection, 0, sizeof(selection));
            selection[rank] = (uint8_t)n;
            Offer(builder, selection);
        }
        if (hand[rank] >= 3) {
            /* Triple with two kickers from other ranks. */
            memset(selection, 0, sizeof(selection));
            memcpy(avail, hand, sizeof(avail));
            selection[rank] = 3;
            avail[rank] = 0;
            OfferWithKickers(builder, selection, avail, RANK_THREE, 2);
        }
    }

    /* Straights and runs of pairs (no 2 in either). */
    for (int start = RANK_THREE; start <= RANK_ACE; ++start) {
        memset(selection, 0, sizeof(selection));
        for (int end = start; end <= RANK_ACE && hand[end] >= 1; ++end) {
            selection[end] = 1;
            if (end - start + 1 >= 5) {
                Offer(builder, selection);
            }
        }
        memset(selection, 0, sizeof(selection));
        for (int end = start; end <= RANK_ACE && hand[end] >= 2; ++end) {
            selection[end] = 2;
            if (end - start + 1 >= 2) {
                Offer(builder, selection);
            }
        }
    }

    /* Planes: a run of g >= 2 triples plus g..2g kickers. */
    for (int start = RANK_THREE; start <= RANK_ACE; ++start) {
        memset(selection, 0, sizeof(selection));
        for (int end = start; end <= RANK_ACE && hand[end] >= 3; ++end) {
            const int groups = end - start + 1;

            selection[end] = 3;
            if (groups < 2) {
                continue;
            }
            for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
                avail[rank] = (uint8_t)(hand[rank] - selection[rank]);
            }
            for (int kickers = groups; kickers <= groups * 2; ++kickers) {
                OfferWithKickers(builder, selection, avail, RANK_THREE, kickers);
            }
        }
    }
}

int Endgame_LegalPlays(const EndgamePosition *position, uint8_t (*plays)[RANK_TWO + 1],
                       HandPattern *patterns, int cap)
{
    PlayBuilder builder;

    builder.hand = position->counts[position->current];
    builder.handSize = CountCards(builder.hand);
    builder.leading = !position->hasLast;
    builder.last = &position->last;
    builder.plays = plays;
    builder.patterns = patterns;
    builder.count = 0;
    builder.cap = cap < ENDGAME_MAX_PLAYS ? cap : ENDGAME_MAX_PLAYS;
    if (builder.handSize > 0) {
        GeneratePlays(&builder);
    }
    return builder.count;
}

/* ---- search ------------------------------------------------------------ */

typedef struct TTEntry {
    uint64_t key1;
    uint64_t key2;
    int16_t value;
    uint8_t flag; /* 0 = empty */
} TTEntry;

typedef struct Solver {
    TTEntry *table;
    int human;
    int nodes;
    int budget;
    bool aborted;
} Solver;

static void PositionKeys(const EndgamePosition *p, uint64_t *key1, uint64_t *key2)
{
    const HandPattern *last = &p->last;

    *key1 = (uint64_t)PackCounts(p->counts[0]) | ((uint64_t)PackCounts(p->counts[1]) << 31);
    *key2 = (uint64_t)PackCounts(p->counts[2]) |
            ((uint64_t)p->current << 31) |
            ((uint64_t)p->lastMover << 33) |
            ((uint64_t)(p->passCount & 1) << 35) |
            ((uint64_t)(p->hasLast ? 1 : 0) << 36) |
            ((uint64_t)(p->played[0] ? 1 : 0) << 37) |
            ((uint64_t)(p->played[1] ? 1 : 0) << 38) |
            ((uint64_t)(p->played[2] ? 1 : 0) << 39);
    if (p->hasLast) {
        *key2 |= ((uint64_t)(last->type & 15) << 40) |
                 ((uint64_t)(last->mainRank & 15) << 44) |
                 ((uint64_t)(last->cardCount & 31) << 48) |
                 ((uint64_t)(last->groupCount & 7) << 53) |
                 ((uint64_t)(last->lastHandShort ? 1 : 0) << 56);
    }
}

static int TableIndex(uint64_t key1, uint64_t key2)
{
    uint64_t h = key1 * 0x9e3779b97f4a7c15ull ^ (key2 + 0x632be59bd9b4e019ull) * 0xbf58476d1ce4e5b9ull;

    h ^= h >> 29;
    return (int)(h & (ENDGAME_TT_SIZE - 1));
}

/* The points a losing seat pays: everything left, except a single last card, and 32
 * for a hand that never played. */
static int LoserPoints(const EndgamePosition *p, int seat)
{
    const int left = CountCards(p->counts[seat]);

    if (!p->played[seat]) {
        return 32;
    }
    return left == 1 ? 0 : left;
}

static int BombValue(const Solver *solver, int owner)
{
    return owner == solver->human ? 20 : -10;
}

/* Human points when `winner` has just emptied its hand. */
static int TerminalValue(const Solver *solver, const EndgamePosition *p, int winner,
                         bool winningPlayIsBomb)
{
    int value = winningPlayIsBomb ? BombValue(solver, winner) : 0;

    if (winner == solver->human) {
        for (int seat = 0; seat < 3; ++seat) {
            if (seat != winner) {
                value += LoserPoints(p, seat);
            }
        }
    } else {
        value -= LoserPoints(p, solver->human);
    }
    return value;
}

/* Higher first: ending the hand, then playing more cards. */
static int PlayOrderKey(const uint8_t *selection, int handSize)
{
    const int total = CountCards(selection);

    return (total == handSize ? 1000 : 0) + total;
}

static int Search(Solver *solver, const EndgamePosition *p, int alpha, int beta)
{
    uint8_t plays[ENDGAME_MAX_PLAYS][RANK_TWO + 1];
    HandPattern patterns[ENDGAME_MAX_PLAYS];
    int order[ENDGAME_MAX_PLAYS];
    int keys[ENDGAME_MAX_PLAYS];
    const int alphaIn = alpha;
    const int betaIn = beta;
    const bool maximise = p->current == solver->human;
    uint64_t key1;
    uint64_t key2;
    TTEntry *entry;
    int count;
    int handSize;
    int best;

    if (++solver->nodes > solver->budget) {
        solver->aborted = true;
        return 0;
    }

    PositionKeys(p, &key1, &key2);
    entry = &solver->table[TableIndex(key1, key2)];
    if (entry->flag != 0 && entry->key1 == key1 && entry->key2 == key2) {
        if (entry->flag == TT_EXACT) {
            return entry->value;
        }
        if (entry->flag == TT_LOWER && entry->value > alpha) {
            alpha = entry->value;
        } else if (entry->flag == TT_UPPER && entry->value < beta) {
            beta = entry->value;
        }
        if (alpha >= beta) {
            return entry->value;
        }
    }

    count = Endgame_LegalPlays(p, plays, patterns, ENDGAME_MAX_PLAYS);
    if (count == 0) {
        /* Nothing beats the previous play (a lead always has one), so the seat passes. */
        EndgamePosition child = *p;
        int bomb = 0;

        child.passCount++;
        if (child.passCount >= 2) {
            if (child.last.type == PATTERN_BOMB) {
                bomb = BombValue(solver, child.lastMover);
            }
            child.hasLast = false;
            child.current = child.lastMover;
            child.passCount = 0;
        } else {
            child.current = Endgame_NextSeat(child.current);
        }
        best = bomb + Search(solver, &child, alpha - bomb, beta - bomb);
    } else {
        handSize = CountCards(p->counts[p->current]);
        for (int i = 0; i < count; ++i) {
            int j = i;

            keys[i] = PlayOrderKey(plays[i], handSize);
            while (j > 0 && keys[order[j - 1]] < keys[i]) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = i;
        }

        best = maximise ? -100000 : 100000;
        for (int n = 0; n < count && !solver->aborted; ++n) {
            const int i = order[n];
            EndgamePosition child = *p;
            int value;

            for (int rank = RANK_THREE; rank <= RANK_TWO; ++rank) {
                child.counts[p->current][rank] =
                    (uint8_t)(child.counts[p->current][rank] - plays[i][rank]);
            }
            child.played[p->current] = true;
            if (CountCards(child.counts[p->current]) == 0) {
                value = TerminalValue(solver, &child, p->current,
                                      patterns[i].type == PATTERN_BOMB);
            } else {
                child.last = patterns[i];
                child.hasLast = true;
                child.lastMover = p->current;
                child.passCount = 0;
                child.current = Endgame_NextSeat(p->current);
                value = Search(solver, &child, alpha, beta);
            }
            if (maximise) {
                if (value > best) {
                    best = value;
                }
                if (best > alpha) {
                    alpha = best;
                }
            } else {
                if (value < best) {
                    best = value;
                }
                if (best < beta) {
                    beta = best;
                }
            }
            if (alpha >= beta) {
                break;
            }
        }
    }

    if (!solver->aborted) {
        entry->key1 = key1;
        entry->key2 = key2;
        entry->value = (int16_t)best;
        entry->flag = best <= alphaIn ? TT_UPPER : (best >= betaIn ? TT_LOWER : TT_EXACT);
    }
    return best;
}

bool Endgame_Solve(const EndgamePosition *position, int humanSeat, int nodeBudget, int *out)
{
    Solver solver;
    int value;

    solver.table = (TTEntry *)calloc(ENDGAME_TT_SIZE, sizeof(TTEntry));
    if (solver.table == NULL) {
        return false;
    }
    solver.human = humanSeat;
    solver.nodes = 0;
    solver.budget = nodeBudget;
    solver.aborted = false;
    value = Search(&solver, position, -100000, 100000);
    free(solver.table);
    if (solver.aborted) {
        return false;
    }
    *out = value;
    return true;
}
