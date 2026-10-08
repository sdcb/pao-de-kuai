#include "game/GameState.h"

#include "core/Str.h"
#include "game/RoundTraceRecorder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * One round of Pao De Kuai.  This is a straight port of game/GameState.cpp, so the
 * structure -- private helpers first, then the public functions -- mirrors the original
 * file rather than being reorganised.
 */

/* The AI talk pools; was `enum class TalkKind`.  The order is the original's, because
 * `lastTalkIndices` is indexed by it. */
typedef uint8_t TalkKind;

enum {
    TALK_NORMAL_PLAY = 0,
    TALK_PASS = 1,
    TALK_BOMB_PLAY = 2,
    TALK_ALMOST_OUT = 3,
    TALK_FORCED_BREAK_GOOD_HAND = 4,
    TALK_CANNOT_BEAT_BIG_MOVE = 5,
    TALK_BIG_MOVE_TAUNT = 6,
    TALK_HUMAN_GOOD_BOMB = 7,
    TALK_HUMAN_GOOD_STRAIGHT = 8,
    TALK_HUMAN_GOOD_PLANE = 9,
    TALK_HUMAN_GOOD_CONSECUTIVE_PAIRS = 10,
    TALK_ROUND_END_GOOD_STRAIGHT = 11,
    TALK_ROUND_END_GOOD_PLANE = 12,
    TALK_ROUND_END_GOOD_BOMB = 13,
    TALK_ROUND_END_GOOD_CONSECUTIVE_PAIRS = 14,
    TALK_KIND_COUNT = 15
};

/* The two sides are separate anonymous enums, so compare them as ints. */
_Static_assert((int)TALK_KIND_COUNT == (int)PDK_TALK_KINDS,
               "talk kind count drifted from the header");

/* The thirteen distinct ranks the fixed deck uses (3..15); every per-rank count in this
 * file is indexed by `rank - RANK_THREE`, which is what the std::map<Rank,int> ordering
 * gave the original code for free. */
enum { PDK_RANK_SLOTS = RANK_TWO - RANK_THREE + 1 };

/* ---- forward declarations (the round helpers call each other) -------- */

static bool IsHumanTurn(const GameState *state);
static bool CurrentPlayerLeads(const GameState *state);
static bool HasPlayableFollow(const GameState *state, PlayerId player);
static void Snapshot(const GameState *state, TurnSnapshot *out);
static void MakeAiContext(const GameState *state, PlayerId player, AiContext *out);
static void AddEvent(GameState *state, GameEventType type, PlayerId player,
                     const char *message, const Cards *cards);
static void MaybeTalk(GameState *state, PlayerId player, TalkKind kind, bool force);
static void MaybeTalkAboutHumanMove(GameState *state, const HandPattern *pattern);
static void MaybeTalkAboutRoundEndGoodHands(GameState *state, PlayerId winner);
static void AdvanceTurn(GameState *state);
static void RemoveCardsFromHand(GameState *state, PlayerId player, const Cards *cards);
static void PlayCards(GameState *state, PlayerId player, const Cards *cards,
                      const HandPattern *pattern, int disruptionPenalty);
static bool Pass(GameState *state, PlayerId player);
static void FinishRound(GameState *state, PlayerId winner);
static void PlayLocalAiTurn(GameState *state, PlayerId player);
static void StartExternalAiTurn(GameState *state);
static bool TryCompleteExternalAiTurn(GameState *state);
static bool ApplyLocalAiResult(GameState *state, const AiMoveChoice *choice,
                               TurnDecisionSource source);
static void AppendRecord(GameState *state, TurnRecord *record);

/* ---- small helpers --------------------------------------------------- */

static uint64_t IndexBit(int index)
{
    return (index >= 0 && index < 64) ? ((uint64_t)1 << index) : 0;
}

static bool MaskContains(uint64_t mask, int index)
{
    return (mask & IndexBit(index)) != 0;
}

static int NextIndex(int index)
{
    return (index + 2) % 3;
}

static bool SameCard(Card lhs, Card rhs)
{
    return lhs.rank == rhs.rank && lhs.suit == rhs.suit;
}

/* The original's overlap-aware subset test: a card may only satisfy the same hand entry
 * once, so this is a multiset containment check. */
static bool ContainsCards(const Cards *hand, const Cards *cards)
{
    bool used[CARDS_MAX];

    memset(used, 0, sizeof(used));
    for (int c = 0; c < cards->count; ++c) {
        bool found = false;
        for (int i = 0; i < hand->count; ++i) {
            if (!used[i] && SameCard(hand->items[i], cards->items[c])) {
                used[i] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static void CountRanks(const Cards *cards, int counts[PDK_RANK_SLOTS])
{
    memset(counts, 0, sizeof(int) * (size_t)PDK_RANK_SLOTS);
    for (int i = 0; i < cards->count; ++i) {
        const int rank = (int)cards->items[i].rank;
        if (rank >= RANK_THREE && rank <= RANK_TWO) {
            ++counts[rank - RANK_THREE];
        }
    }
}

/* std::sort by RankValue; equal ranks are indistinguishable, so an insertion sort gives
 * the identical sequence.  These arrays never hold more than PDK_RANK_SLOTS entries. */
static void SortRanks(Rank *ranks, int count)
{
    for (int i = 1; i < count; ++i) {
        const Rank key = ranks[i];
        const int keyValue = RankValue(key);
        int j = i - 1;

        while (j >= 0 && RankValue(ranks[j]) > keyValue) {
            ranks[j + 1] = ranks[j];
            --j;
        }
        ranks[j + 1] = key;
    }
}

static bool IsConsecutiveRanks(const Rank *ranks, int count)
{
    if (count <= 0) {
        return false;
    }
    for (int i = 1; i < count; ++i) {
        if (RankValue(ranks[i]) != RankValue(ranks[i - 1]) + 1) {
            return false;
        }
    }
    return ranks[count - 1] != RANK_TWO;
}

static Rank MaxRank(const Rank *ranks, int count)
{
    Rank best = ranks[0];

    for (int i = 1; i < count; ++i) {
        if (RankValue(ranks[i]) > RankValue(best)) {
            best = ranks[i];
        }
    }
    return best;
}

/* Sorts, then dedupes the way `std::sort` + `std::unique` did, and returns the longest run
 * of consecutive RankValues.  `ranks` is scratch: the original took the vector by value. */
static int BestConsecutiveRunLength(Rank *ranks, int count)
{
    int uniqueCount = 0;
    int best = 1;
    int current = 1;

    if (count <= 0) {
        return 0;
    }
    SortRanks(ranks, count);
    for (int i = 0; i < count; ++i) {
        if (uniqueCount == 0 || ranks[i] != ranks[uniqueCount - 1]) {
            ranks[uniqueCount++] = ranks[i];
        }
    }
    for (int i = 1; i < uniqueCount; ++i) {
        if (RankValue(ranks[i]) == RankValue(ranks[i - 1]) + 1) {
            ++current;
        } else {
            current = 1;
        }
        if (current > best) {
            best = current;
        }
    }
    return best;
}

static int DragPatternTieBreaker(PatternType type)
{
    switch (type) {
    case PATTERN_STRAIGHT: return 7000;
    case PATTERN_PLANE: return 6500;
    case PATTERN_CONSECUTIVE_PAIRS: return 6000;
    case PATTERN_TRIPLE_WITH_PAIR: return 5000;
    case PATTERN_BOMB: return 4000;
    case PATTERN_TRIPLE_WITH_ONE: return 3000;
    case PATTERN_PAIR: return 2000;
    case PATTERN_SINGLE: return 1000;
    case PATTERN_INVALID: break;
    }
    return 0;
}

static int ScorePattern(const HandPattern *pattern)
{
    return pattern->cardCount * 100000 + DragPatternTieBreaker(pattern->type) +
           RankValue(pattern->mainRank);
}

/* The drag-selection-only patterns: the visual grouping helper accepts a bare triple and a
 * bare plane core, which the move validator rejects as leads on their own. */
static bool IdentifyDragOnlyPattern(const Cards *cards, HandPattern *out)
{
    int counts[PDK_RANK_SLOTS];
    Rank tripleRanks[PDK_RANK_SLOTS];
    int distinct = 0;
    int tripleCount = 0;
    const int total = cards->count;

    CountRanks(cards, counts);
    for (int i = 0; i < PDK_RANK_SLOTS; ++i) {
        if (counts[i] > 0) {
            ++distinct;
        }
    }

    if (total == 3 && distinct == 1) {
        out->type = PATTERN_TRIPLE_WITH_ONE;
        out->mainRank = cards->items[0].rank;
        out->cardCount = total;
        out->groupCount = 1;
        out->lastHandShort = true;
        return true;
    }

    if (total < 6 || total % 3 != 0) {
        return false;
    }

    for (int i = 0; i < PDK_RANK_SLOTS; ++i) {
        const Rank rank = (Rank)(i + RANK_THREE);

        if (counts[i] == 0) {
            continue;
        }
        if (rank == RANK_TWO || counts[i] != 3) {
            return false;
        }
        tripleRanks[tripleCount++] = rank;
    }
    SortRanks(tripleRanks, tripleCount);
    if (!IsConsecutiveRanks(tripleRanks, tripleCount)) {
        return false;
    }
    out->type = PATTERN_PLANE;
    out->mainRank = MaxRank(tripleRanks, tripleCount);
    out->cardCount = total;
    out->groupCount = tripleCount;
    out->lastHandShort = true;
    return true;
}

static void DragPatternDescription(const HandPattern *pattern, char *out, int cap)
{
    Str text;

    Str_Init(&text);
    if (pattern->type == PATTERN_TRIPLE_WITH_ONE && pattern->cardCount == 3) {
        Str_Append(&text, "三张 ");
        Str_Append(&text, RankName(pattern->mainRank));
    } else if (pattern->type == PATTERN_PLANE && pattern->cardCount == pattern->groupCount * 3) {
        Str_Append(&text, "飞机主体 ");
        Str_Append(&text, RankName(pattern->mainRank));
    } else {
        char description[PATTERN_REASON_CAP];

        PatternDescription(pattern, description, PATTERN_REASON_CAP);
        Str_Append(&text, description);
    }
    Str_CopyTo(out, cap, Str_CStr(&text));
    Str_Free(&text);
}

/* ---- talk ------------------------------------------------------------ */

typedef struct TalkPool {
    const char *const *lines;
    int count;
} TalkPool;

static const char *const kNormalPlay[] = {
    "这牌我忍半天了！",
    "轮到我了，看我走一手。",
    "别眨眼，我这手有点讲究。"
};
static const char *const kPass[] = {
    "先不要，你们继续。",
    "这手我让一让。",
    "过了过了，别看我。"
};
static const char *const kBombPlay[] = {
    "看，我有炸弹，没想到吧？",
    "炸一下，醒醒神！",
    "这炸弹我可憋很久了。"
};
static const char *const kAlmostOut[] = {
    "别急别急，我马上跑完。",
    "我手里没几张了，注意点。",
    "再给我一轮，我可能就溜了。"
};
static const char *const kForcedBreakGoodHand[] = {
    "哎呀，我的好牌都被拆光光了。",
    "这牌本来很顺的，非得拆我一手。",
    "要得起必须打，心疼我的牌型。"
};
static const char *const kCannotBeatBigMove[] = {
    "这么长一串？我先缓缓。",
    "这谁顶得住啊，我不要了。",
    "你这一下甩这么多，我接不住。"
};
static const char *const kBigMoveTaunt[] = {
    "看好了，一大把直接甩出去！",
    "这么多牌一起走，帅不帅？",
    "我这一手下去，桌面都清爽了。"
};
static const char *const kHumanGoodBomb[] = {
    "李姐手里还有炸弹？这谁敢动啊。",
    "完了完了，李姐藏着炸弹呢。",
    "这炸弹一亮，我有点慌。"
};
static const char *const kHumanGoodStraight[] = {
    "这顺子也太顺了吧。",
    "李姐这条顺子，漂亮得有点过分。",
    "这么长一串，看得我心里发虚。"
};
static const char *const kHumanGoodPlane[] = {
    "飞机都来了？这牌也太豪华了。",
    "这飞机一起飞，我可拦不住。",
    "李姐这飞机藏得真深。"
};
static const char *const kHumanGoodConsecutivePairs[] = {
    "这一排对子也太整齐了。",
    "连对这么长，我有点接不住。",
    "对子排队过来，压力很大。"
};
static const char *const kRoundEndGoodStraight[] = {
    "哎呀，我还有一条好顺子呢。",
    "这顺子还没来得及跑出去。",
    "可惜了，我手里这顺子挺漂亮。"
};
static const char *const kRoundEndGoodPlane[] = {
    "哎呀，我还有个好飞机呢。",
    "飞机还在手里，结果已经结束了。",
    "这把我的飞机没起飞。"
};
static const char *const kRoundEndGoodBomb[] = {
    "我炸弹还在手里呢，亏大了。",
    "这炸弹没甩出去，太憋屈了。",
    "早知道我就先炸一下了。"
};
static const char *const kRoundEndGoodConsecutivePairs[] = {
    "我这连对还挺整齐，可惜没机会了。",
    "对子排好了，牌局却结束了。",
    "这手连对没打出去，真难受。"
};

/* `PDK_ARRAY_COUNT` lives in graphics/win_compat.h, which this Win32-free translation unit
 * does not include, so the table carries its own counts. */
#define PDK_TALK_POOL(lines) {lines, (int)(sizeof(lines) / sizeof((lines)[0]))}

static const TalkPool kTalkPools[PDK_TALK_KINDS] = {
    PDK_TALK_POOL(kNormalPlay),
    PDK_TALK_POOL(kPass),
    PDK_TALK_POOL(kBombPlay),
    PDK_TALK_POOL(kAlmostOut),
    PDK_TALK_POOL(kForcedBreakGoodHand),
    PDK_TALK_POOL(kCannotBeatBigMove),
    PDK_TALK_POOL(kBigMoveTaunt),
    PDK_TALK_POOL(kHumanGoodBomb),
    PDK_TALK_POOL(kHumanGoodStraight),
    PDK_TALK_POOL(kHumanGoodPlane),
    PDK_TALK_POOL(kHumanGoodConsecutivePairs),
    PDK_TALK_POOL(kRoundEndGoodStraight),
    PDK_TALK_POOL(kRoundEndGoodPlane),
    PDK_TALK_POOL(kRoundEndGoodBomb),
    PDK_TALK_POOL(kRoundEndGoodConsecutivePairs)
};

static bool IsForceTalk(TalkKind kind)
{
    switch (kind) {
    case TALK_BOMB_PLAY:
    case TALK_FORCED_BREAK_GOOD_HAND:
    case TALK_CANNOT_BEAT_BIG_MOVE:
    case TALK_BIG_MOVE_TAUNT:
    case TALK_HUMAN_GOOD_BOMB:
    case TALK_HUMAN_GOOD_STRAIGHT:
    case TALK_HUMAN_GOOD_PLANE:
    case TALK_HUMAN_GOOD_CONSECUTIVE_PAIRS:
    case TALK_ROUND_END_GOOD_STRAIGHT:
    case TALK_ROUND_END_GOOD_PLANE:
    case TALK_ROUND_END_GOOD_BOMB:
    case TALK_ROUND_END_GOOD_CONSECUTIVE_PAIRS:
        return true;
    case TALK_NORMAL_PLAY:
    case TALK_PASS:
    case TALK_ALMOST_OUT:
    case TALK_KIND_COUNT:
        return false;
    }
    return false;
}

/* The original switch fell through to the normal-play pool for an out-of-range kind. */
static TalkPool TalkPoolFor(TalkKind kind)
{
    if (kind >= PDK_TALK_KINDS) {
        kind = TALK_NORMAL_PLAY;
    }
    return kTalkPools[kind];
}

/* Picks a line, avoiding the line picked last time for the same kind. */
static void ChooseTalkText(GameState *state, TalkKind kind, char *out, int cap)
{
    const TalkPool pool = TalkPoolFor(kind);
    int selected;

    if (kind >= PDK_TALK_KINDS) {
        kind = TALK_NORMAL_PLAY;
    }
    if (pool.count <= 0) {
        out[0] = '\0';
        return;
    }
    selected = rand() % pool.count;
    if (pool.count > 1 && selected == state->lastTalkIndices[kind]) {
        selected = (selected + 1) % pool.count;
    }
    state->lastTalkIndices[kind] = selected;
    Str_CopyTo(out, cap, pool.lines[selected]);
}

static void MaybeTalk(GameState *state, PlayerId player, TalkKind kind, bool force)
{
    /* Key reactions should be heard even if a recent ordinary line started the cooldown. */
    if (player == PLAYER_HUMAN || (!force && !IsForceTalk(kind) && state->talkCooldown > 0.0f)) {
        return;
    }
    state->talkPlayer = player;
    ChooseTalkText(state, kind, state->talkText, PDK_EVENT_TEXT_CAP);
    state->talkCooldown = 5.0f;
    AddEvent(state, GAME_EVENT_TALK, player, state->talkText, NULL);
}

static bool HumanGoodTalkKind(const HandPattern *pattern, TalkKind *out)
{
    if (pattern->type == PATTERN_BOMB) {
        *out = TALK_HUMAN_GOOD_BOMB;
        return true;
    }
    if (pattern->type == PATTERN_PLANE) {
        *out = TALK_HUMAN_GOOD_PLANE;
        return true;
    }
    if (pattern->type == PATTERN_STRAIGHT && pattern->cardCount >= 7) {
        *out = TALK_HUMAN_GOOD_STRAIGHT;
        return true;
    }
    if (pattern->type == PATTERN_CONSECUTIVE_PAIRS && pattern->cardCount >= 6) {
        *out = TALK_HUMAN_GOOD_CONSECUTIVE_PAIRS;
        return true;
    }
    return false;
}

static bool RoundEndGoodTalkKind(const Cards *hand, TalkKind *out)
{
    int counts[PDK_RANK_SLOTS];
    Rank tripleRanks[PDK_RANK_SLOTS];
    Rank straightRanks[PDK_RANK_SLOTS];
    Rank pairRanks[PDK_RANK_SLOTS];
    int tripleCount = 0;
    int straightCount = 0;
    int pairCount = 0;
    bool hasBomb = false;

    CountRanks(hand, counts);
    for (int i = 0; i < PDK_RANK_SLOTS; ++i) {
        const Rank rank = (Rank)(i + RANK_THREE);
        const int count = counts[i];

        if (count == 0) {
            continue;
        }
        if (rank != RANK_TWO) {
            straightRanks[straightCount++] = rank;
        }
        if (rank != RANK_TWO && count >= 2) {
            pairRanks[pairCount++] = rank;
        }
        if (rank != RANK_TWO && count >= 3) {
            tripleRanks[tripleCount++] = rank;
        }
        if (count >= 4 && rank != RANK_ACE && rank != RANK_TWO) {
            hasBomb = true;
        }
    }

    if (BestConsecutiveRunLength(tripleRanks, tripleCount) >= 2) {
        *out = TALK_ROUND_END_GOOD_PLANE;
        return true;
    }
    if (BestConsecutiveRunLength(straightRanks, straightCount) >= 7) {
        *out = TALK_ROUND_END_GOOD_STRAIGHT;
        return true;
    }
    if (hasBomb) {
        *out = TALK_ROUND_END_GOOD_BOMB;
        return true;
    }
    if (BestConsecutiveRunLength(pairRanks, pairCount) >= 3) {
        *out = TALK_ROUND_END_GOOD_CONSECUTIVE_PAIRS;
        return true;
    }
    return false;
}

static void MaybeTalkAboutHumanMove(GameState *state, const HandPattern *pattern)
{
    TalkKind kind;

    if (!HumanGoodTalkKind(pattern, &kind)) {
        return;
    }
    MaybeTalk(state, (rand() % 2) == 0 ? PLAYER_AI1 : PLAYER_AI2, kind, true);
}

static void MaybeTalkAboutRoundEndGoodHands(GameState *state, PlayerId winner)
{
    bool hasBest = false;
    PlayerId bestPlayer = PLAYER_AI1;
    TalkKind bestKind = TALK_NORMAL_PLAY;
    int bestPriority = 0;

    for (int seat = PLAYER_AI1; seat <= PLAYER_AI2; ++seat) {
        const PlayerId player = (PlayerId)seat;
        TalkKind kind;
        int priority;

        if (player == winner) {
            continue;
        }
        if (!RoundEndGoodTalkKind(&state->players[PlayerIndex(player)].hand, &kind)) {
            continue;
        }
        switch (kind) {
        case TALK_ROUND_END_GOOD_PLANE: priority = 400; break;
        case TALK_ROUND_END_GOOD_STRAIGHT: priority = 300; break;
        case TALK_ROUND_END_GOOD_BOMB: priority = 200; break;
        case TALK_ROUND_END_GOOD_CONSECUTIVE_PAIRS: priority = 100; break;
        default: priority = 0; break;
        }
        if (!hasBest || priority > bestPriority) {
            hasBest = true;
            bestPlayer = player;
            bestKind = kind;
            bestPriority = priority;
        }
    }

    if (hasBest) {
        MaybeTalk(state, bestPlayer, bestKind, true);
    }
}

/* ---- events ---------------------------------------------------------- */

static void ClearEvents(GameState *state)
{
    state->eventHead = 0;
    state->eventCount = 0;
}

/* `cards` NULL means "no cards", which is what MakeCards({}) produced at those call sites.
 * A full ring drops the oldest event and counts it; see PDK_EVENT_SLOTS. */
static void AddEvent(GameState *state, GameEventType type, PlayerId player,
                     const char *message, const Cards *cards)
{
    GameEvent *event;

    if (state->events == NULL || state->eventCap <= 0) {
        ++state->eventsDropped;
        return;
    }
    if (state->eventCount >= state->eventCap) {
        state->eventHead = (state->eventHead + 1) % state->eventCap;
        --state->eventCount;
        ++state->eventsDropped;
    }
    event = &state->events[(state->eventHead + state->eventCount) % state->eventCap];
    memset(event, 0, sizeof(*event));
    event->type = type;
    event->player = player;
    Str_CopyTo(event->message, PDK_EVENT_TEXT_CAP, message);
    if (cards != NULL) {
        event->cards = *cards;
    }
    ++state->eventCount;
}

/* ---- snapshots and records ------------------------------------------- */

static void Snapshot(const GameState *state, TurnSnapshot *out)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        out->hands[i] = state->players[i].hand;
    }
    out->lastCards = state->lastCards;
    out->hasLastPattern = state->hasLastPattern;
    if (state->hasLastPattern) {
        out->lastPattern = state->lastPattern;
    }
    out->lastMovePlayer = state->lastMovePlayer;
    out->currentPlayer = state->currentPlayer;
    out->passCount = state->passCount;
}

/* GameAction_Clear only clears the text and the count, so the unused rank slots are
 * explicitly zeroed here: a TurnRecord is copied into the audit array and round-traced in
 * full.  (The C++ code left them indeterminate; nothing ever read them.) */
static void ActionFromCards(const Cards *cards, bool pass, GameAction *out)
{
    memset(out, 0, sizeof(*out));
    GameAction_Set(out, pass ? "pass" : "play");
    if (!pass) {
        for (int i = 0; i < cards->count; ++i) {
            GameAction_AddRank(out, RankName(cards->items[i].rank));
        }
    }
}

static void AppendMoveText(Str *out, const GameAction *action)
{
    /*
     * The C++ original opened with `action.action == "pass"`, which became a pointer
     * comparison when S4c turned std::string into char[64] (so the branch could never be
     * taken), and iterated the whole `char[20][64]` array instead of the first
     * `rankCount` entries (so it appended nineteen indeterminate buffers).  Both are
     * restored to the behaviour the file had before that change -- see the port report.
     */
    if (strcmp(action->action, "pass") == 0) {
        Str_Append(out, "不要");
        return;
    }
    Str_Append(out, "出");
    for (int i = 0; i < action->rankCount; ++i) {
        Str_AppendChar(out, ' ');
        Str_Append(out, action->ranks[i]);
    }
}

static TurnDecisionTrace SyntheticTrace(const TurnRecord *record)
{
    TurnDecisionTrace trace;
    Str reasoning;

    memset(&trace, 0, sizeof(trace));
    Str_Init(&reasoning);
    Str_Append(&reasoning, "本地记录：");
    Str_Append(&reasoning, PlayerLabel(record->actor));
    Str_AppendChar(&reasoning, ' ');
    if (record->reason == TURN_REASON_CANNOT_BEAT) {
        Str_Append(&reasoning, "按规则要不起，只能不要。");
    } else if (record->reason == TURN_REASON_ONLY_LEGAL_MOVE) {
        Str_Append(&reasoning, "只有一种合法选择，直接执行 ");
        AppendMoveText(&reasoning, &record->finalAction);
        Str_Append(&reasoning, "。");
    } else {
        Str_Append(&reasoning, "执行 ");
        AppendMoveText(&reasoning, &record->finalAction);
        Str_Append(&reasoning, "。");
    }
    Str_CopyTo(trace.reasoningContent, PDK_TURN_TEXT_CAP, Str_CStr(&reasoning));
    Str_Free(&reasoning);
    return trace;
}

/* Every call site of the original passed an empty strategy and an empty trace, so the
 * fallback chain below always ran and AppendRecord always synthesised the trace. */
static TurnRecord BuildTurnRecord(GameState *state, const TurnSnapshot *before, PlayerId actor,
                                  TurnDecisionSource source, TurnDecisionReason reason,
                                  const GameAction *requested, const GameAction *finalAction,
                                  const Cards *finalCards, bool hasFinalPattern,
                                  const HandPattern *finalPattern, bool accepted,
                                  const char *validationMessage)
{
    TurnRecord record;
    StrategyMetadata strategy;

    memset(&record, 0, sizeof(record));
    record.turnNo = state->nextTurnNo;
    record.actor = actor;
    record.source = source;
    record.reason = reason;
    record.before = *before;
    Snapshot(state, &record.after);
    record.requestedAction = *requested;
    record.finalAction = *finalAction;
    record.finalCards = *finalCards;
    record.hasFinalPattern = hasFinalPattern;
    if (hasFinalPattern) {
        record.finalPattern = *finalPattern;
    }
    record.accepted = accepted;
    Str_CopyTo(record.validationMessage, PDK_TURN_TEXT_CAP, validationMessage);
    if (source == TURN_SOURCE_SYSTEM || reason == TURN_REASON_CANNOT_BEAT ||
        reason == TURN_REASON_ONLY_LEGAL_MOVE) {
        strategy = RulesStrategyMetadata();
    } else if (source == TURN_SOURCE_HUMAN) {
        strategy = HumanStrategyMetadata();
    } else if (source == TURN_SOURCE_LOCAL_AI && actor == PLAYER_HUMAN) {
        strategy = AiPlayer_Metadata(&state->aiPlayers[PlayerIndex(actor)]);
    } else {
        strategy = state->roundStrategies[PlayerIndex(actor)];
    }
    record.strategy = strategy;
    return record;
}

/* Writes the whole round as JSON once, when tracing is on and the round has ended. */
static void MaybeWriteRoundTrace(GameState *state)
{
    RoundTrace trace;
    char writtenPath[PDK_STAT_PATH_CAP];

    if (!state->roundTraceEnabled || state->roundTraceWritten || !state->roundOver) {
        return;
    }

    memset(&trace, 0, sizeof(trace));
    trace.seed = state->roundSeed;
    trace.playerName = state->playerName;
    trace.startedAt = state->startedAt;
    trace.roundLeader = state->roundLeader;
    trace.initialPlayers = state->initialPlayers;
    trace.strategies = state->initialStrategies;
    trace.turns = state->turnRecords;
    trace.turnCount = state->turnRecordCount;
    trace.result = &state->lastRoundRecord;

    writtenPath[0] = '\0';
    if (RoundTraceRecorder_WriteRound(state->roundTraceRoot, &trace, writtenPath,
                                      PDK_STAT_PATH_CAP)) {
        Str_CopyTo(state->lastRoundTracePath, PDK_STAT_PATH_CAP, writtenPath);
        state->roundTraceWritten = true;
    }
}

static void AppendRecord(GameState *state, TurnRecord *record)
{
    if (record->trace.reasoningContent[0] == '\0') {
        record->trace = SyntheticTrace(record);
    }
    if (state->turnRecords == NULL) {
        /* GameState_Init could not allocate the trail; count it instead of losing it. */
        ++state->turnRecordsDropped;
    } else if (state->turnRecordCount < state->turnRecordCap) {
        state->turnRecords[state->turnRecordCount++] = *record;
    } else {
        /* Unreachable: PDK_TURN_RECORDS_MAX is the proven bound.  If a rule change ever
         * makes it reachable, keep the newest tail and make the drop observable. */
        memmove(&state->turnRecords[0], &state->turnRecords[1],
                sizeof(TurnRecord) * (size_t)(state->turnRecordCount - 1));
        state->turnRecords[state->turnRecordCount - 1] = *record;
        ++state->turnRecordsDropped;
    }
    ++state->nextTurnNo;
    MaybeWriteRoundTrace(state);
}

/* ---- turn flow ------------------------------------------------------- */

static void AdvanceTurn(GameState *state)
{
    state->currentPlayer = PlayerFromIndex(NextIndex(PlayerIndex(state->currentPlayer)));
    if (state->currentPlayer == PLAYER_HUMAN) {
        AddEvent(state, GAME_EVENT_TALK, PLAYER_HUMAN, "轮到你", NULL);
    }
}

static void RecordPassObservation(GameState *state, PlayerId player,
                                  const HandPattern *pattern)
{
    const int index = PlayerIndex(player);
    PassObservation observation;
    OptionalPassObservation *existing = &state->passObservations[index];

    observation.pattern = *pattern;
    observation.remainingCards = state->players[index].hand.count;
    PassHistory_Add(&state->passHistory[index], &observation);

    if (existing->has && existing->value.pattern.type == PATTERN_SINGLE &&
        pattern->type == PATTERN_SINGLE) {
        /* For singles, a lower failed-to-beat rank is stronger information:
         * failing to beat Q proves K/A/2 are unavailable, while failing to beat K
         * still leaves open the possibility that the player has a K. */
        if (RankValue(pattern->mainRank) < RankValue(existing->value.pattern.mainRank)) {
            OptionalPassObservation_Set(existing, &observation);
        }
        return;
    }

    if (existing->has && existing->value.pattern.type == PATTERN_SINGLE &&
        pattern->type != PATTERN_SINGLE) {
        return;
    }

    OptionalPassObservation_Set(existing, &observation);
}

static void RemoveCardsFromHand(GameState *state, PlayerId player, const Cards *cards)
{
    Cards *hand = &state->players[PlayerIndex(player)].hand;

    for (int i = 0; i < cards->count; ++i) {
        /* Cards_Remove takes the first match, exactly like the find_if + erase loop. */
        Cards_Remove(hand, cards->items[i]);
    }
}

static void FinishRound(GameState *state, PlayerId winner)
{
    RoundScoreInput input;
    RoundScoreResult score;
    Str text;

    state->roundOver = true;
    state->nextRoundLeader = winner;
    state->hasNextRoundLeader = true;

    memset(&input, 0, sizeof(input));
    input.winner = winner;
    input.bombCount = 0;
    for (int i = 0; i < state->bombCount; ++i) {
        if (input.bombCount >= BOMB_EVENTS_MAX) {
            break;
        }
        input.bombs[input.bombCount++] = state->bombs[i];
    }
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        input.remainingCards[i] = state->players[i].hand.count;
        input.hasPlayedCards[i] = state->players[i].hasPlayedCards;
    }
    score = CalculateRoundScore(&input);

    RoundRecord_Init(&state->lastRoundRecord);
    Str_CopyTo(state->lastRoundRecord.startedAt, PDK_TIME_TEXT_CAP, state->startedAt);
    NowTimeText(state->lastRoundRecord.endedAt, PDK_TIME_TEXT_CAP);
    state->lastRoundRecord.winner = winner;
    Str_CopyTo(state->lastRoundRecord.playerName, PDK_PLAYER_NAME_CAP, state->playerName);
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        state->lastRoundRecord.scores[i] = score.scores[i];
        state->lastRoundRecord.remainingCards[i] = input.remainingCards[i];
    }
    for (int i = 0; i < state->bombCount; ++i) {
        RoundRecord_AddBomb(&state->lastRoundRecord, state->bombs[i]);
    }
    state->lastRoundRecord.spring = score.spring;

    Str_Init(&text);
    Str_Append(&text, winner == PLAYER_HUMAN ? "胜利" : "失败");
    Str_Append(&text, "  本局分: 玩家 ");
    Str_AppendNumber(&text, score.scores[0]);
    Str_Append(&text, " AI1 ");
    Str_AppendNumber(&text, score.scores[1]);
    Str_Append(&text, " AI2 ");
    Str_AppendNumber(&text, score.scores[2]);
    Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, Str_CStr(&text));
    Str_Free(&text);

    AddEvent(state, GAME_EVENT_ROUND_ENDED, winner, state->toast, NULL);
    MaybeTalkAboutRoundEndGoodHands(state, winner);
}

static void PlayCards(GameState *state, PlayerId player, const Cards *cards,
                      const HandPattern *pattern, int disruptionPenalty)
{
    const int index = PlayerIndex(player);
    const bool wasFollowing = !CurrentPlayerLeads(state);
    /* A bomb played on top of a standing bomb beats it: the beaten bomb no longer scores. */
    const bool beatsBomb = wasFollowing && state->hasLastPattern &&
                           state->lastPattern.type == PATTERN_BOMB;
    char message[PDK_EVENT_TEXT_CAP];
    Str text;

    RemoveCardsFromHand(state, player, cards);
    state->players[index].hasPlayedCards = true;
    Cards_Append(&state->playedCards, cards);
    state->lastCards = *cards;
    if (CurrentPlayerLeads(state)) {
        state->trickLeader = player;
    }
    state->lastPattern = *pattern;
    state->hasLastPattern = true;
    state->lastMovePlayer = player;
    state->passCount = 0;

    Str_Init(&text);
    Str_Append(&text, state->players[index].name);
    Str_Append(&text, " 出了 ");
    {
        char description[PATTERN_REASON_CAP];

        PatternDescription(pattern, description, PATTERN_REASON_CAP);
        Str_Append(&text, description);
    }
    Str_CopyTo(message, PDK_EVENT_TEXT_CAP, Str_CStr(&text));
    Str_Free(&text);

    Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, message);
    AddEvent(state, GAME_EVENT_CARDS_PLAYED, player, message, cards);

    if (pattern->type == PATTERN_BOMB) {
        if (beatsBomb && state->hasStandingBomb && state->standingBombIndex < state->bombCount) {
            state->bombs[state->standingBombIndex].beaten = true;
        }
        if (state->bombCount < BOMB_EVENTS_MAX) {
            BombScoreEvent bomb;

            bomb.by = player;
            bomb.score = 20;
            bomb.beaten = false;
            state->bombs[state->bombCount++] = bomb;
            state->standingBombIndex = state->bombCount - 1;
            state->hasStandingBomb = true;
        }
        /* else unreachable: eleven ranks (3..K) carry four cards, so a twelfth bomb cannot
         * exist; the standing index then keeps pointing at the last recorded bomb. */
        AddEvent(state, GAME_EVENT_BOMB, player, "炸弹 +20", cards);
    }

    if (player == PLAYER_HUMAN) {
        MaybeTalkAboutHumanMove(state, pattern);
    } else if (pattern->type == PATTERN_BOMB) {
        MaybeTalk(state, player, TALK_BOMB_PLAY, true);
    } else if (wasFollowing && disruptionPenalty >= 320) {
        MaybeTalk(state, player, TALK_FORCED_BREAK_GOOD_HAND, true);
    } else if (cards->count >= 7) {
        MaybeTalk(state, player, TALK_BIG_MOVE_TAUNT, true);
    } else if (state->players[index].hand.count <= 3) {
        MaybeTalk(state, player, TALK_ALMOST_OUT, false);
    } else {
        MaybeTalk(state, player, TALK_NORMAL_PLAY, false);
    }

    if (state->players[index].hand.count == 0) {
        FinishRound(state, player);
        return;
    }

    AdvanceTurn(state);
}

static bool Pass(GameState *state, PlayerId player)
{
    char message[PDK_EVENT_TEXT_CAP];

    if (state->roundOver || CurrentPlayerLeads(state)) {
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, "当前需要主动出牌，不能不要");
        AddEvent(state, GAME_EVENT_INVALID_MOVE, player, state->toast, NULL);
        return false;
    }
    if (HasPlayableFollow(state, player)) {
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, "要得起必须出");
        AddEvent(state, GAME_EVENT_INVALID_MOVE, player, state->toast, NULL);
        return false;
    }

    ++state->passCount;
    RecordPassObservation(state, player, &state->lastPattern);
    {
        Str text;

        Str_Init(&text);
        Str_Append(&text, state->players[PlayerIndex(player)].name);
        Str_Append(&text, " 不要");
        Str_CopyTo(message, PDK_EVENT_TEXT_CAP, Str_CStr(&text));
        Str_Free(&text);
    }
    Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, message);
    AddEvent(state, GAME_EVENT_PASSED, player, message, NULL);
    if (player != PLAYER_HUMAN) {
        if (state->hasLastPattern && state->lastPattern.cardCount >= 7) {
            MaybeTalk(state, player, TALK_CANNOT_BEAT_BIG_MOVE, true);
        } else {
            MaybeTalk(state, player, TALK_PASS, false);
        }
    }

    if (state->passCount >= 2) {
        state->currentPlayer = state->lastMovePlayer;
        state->hasLastPattern = false;
        Cards_Clear(&state->lastCards);
        state->trickLeader = state->currentPlayer;
        state->passCount = 0;
        state->hasStandingBomb = false;
        {
            Str text;

            Str_Init(&text);
            Str_Append(&text, state->players[PlayerIndex(state->currentPlayer)].name);
            Str_Append(&text, " 重新领出");
            Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, Str_CStr(&text));
            Str_Free(&text);
        }
        return true;
    }

    AdvanceTurn(state);
    return true;
}

/* ---- AI turns -------------------------------------------------------- */

static const ExternalAiController *AiControllerFor(const GameState *state, PlayerId player)
{
    for (int i = 0; i < state->externalAiControllerCount; ++i) {
        if (ExternalAiController_CanHandle(&state->externalAiControllers[i], player)) {
            return &state->externalAiControllers[i];
        }
    }
    return NULL;
}

/* The original built a vector of every legal (cards, pattern) pair and only ever asked it
 * for its size, so this counts instead of materialising.  Same mask enumeration, same
 * validation, no per-turn allocation. */
static int CountLegalMoves(const GameState *state, PlayerId player)
{
    const Cards *hand = &state->players[PlayerIndex(player)].hand;
    const int n = hand->count;
    uint64_t limit;
    int count = 0;

    if (n <= 0 || n >= 63) {
        return 0;
    }
    limit = (uint64_t)1 << n;
    for (uint64_t mask = 1; mask < limit; ++mask) {
        Cards cards;
        MoveValidation validation;

        Cards_Clear(&cards);
        for (int i = 0; i < n; ++i) {
            if ((mask & ((uint64_t)1 << i)) != 0) {
                Cards_Push(&cards, hand->items[i]);
            }
        }
        validation = CurrentPlayerLeads(state)
                         ? PaoDeKuaiRules_ValidateLeadMove(&state->rules, &cards, n)
                         : PaoDeKuaiRules_ValidateFollowMove(&state->rules, &cards,
                                                             &state->lastPattern, n);
        if (validation.ok) {
            ++count;
        }
    }
    return count;
}

static bool ApplyLocalAiResult(GameState *state, const AiMoveChoice *choice,
                               TurnDecisionSource source)
{
    TurnSnapshot before;
    const PlayerId actor = state->currentPlayer;
    GameAction action;
    Cards empty;
    TurnRecord record;

    Snapshot(state, &before);

    if (choice->pass) {
        if (CurrentPlayerLeads(state) || HasPlayableFollow(state, actor)) {
            return false;
        }
        if (!Pass(state, actor)) {
            return false;
        }
        Cards_Clear(&empty);
        ActionFromCards(&empty, true, &action);
        record = BuildTurnRecord(state, &before, actor, source, TURN_REASON_CANNOT_BEAT, &action,
                                 &action, &empty, false, NULL, true, "本地异步 AI 不要合法");
        AppendRecord(state, &record);
        return true;
    }

    {
        const Cards *hand = &state->players[PlayerIndex(actor)].hand;
        MoveValidation validation;

        if (!ContainsCards(hand, &choice->cards)) {
            return false;
        }
        validation = CurrentPlayerLeads(state)
                         ? PaoDeKuaiRules_ValidateLeadMove(&state->rules, &choice->cards,
                                                           hand->count)
                         : PaoDeKuaiRules_ValidateFollowMove(&state->rules, &choice->cards,
                                                             &state->lastPattern, hand->count);
        if (!validation.ok) {
            return false;
        }

        PlayCards(state, actor, &choice->cards, &validation.pattern, choice->disruptionPenalty);
        ActionFromCards(&choice->cards, false, &action);
        record = BuildTurnRecord(state, &before, actor, source, TURN_REASON_NORMAL_CHOICE, &action,
                                 &action, &choice->cards, true, &validation.pattern, true,
                                 "本地异步 AI 出牌合法");
        AppendRecord(state, &record);
        return true;
    }
}

static bool ApplyExternalAiResult(GameState *state, const ExternalAiResult *result)
{
    if (!result->ok || !result->hasLocalChoice) {
        return false;
    }
    return ApplyLocalAiResult(state, &result->localChoice, result->source);
}

static void PlayLocalAiTurn(GameState *state, PlayerId player)
{
    TurnSnapshot before;
    int legalCount;
    TurnDecisionSource source = TURN_SOURCE_LOCAL_AI;
    TurnDecisionReason reason;
    AiMoveChoice choice;
    GameAction action;
    Cards empty;
    TurnRecord record;

    Snapshot(state, &before);
    legalCount = CountLegalMoves(state, player);
    reason = legalCount == 1 ? TURN_REASON_ONLY_LEGAL_MOVE : TURN_REASON_NORMAL_CHOICE;
    memset(&choice, 0, sizeof(choice));
    if (legalCount == 0 && !CurrentPlayerLeads(state)) {
        source = TURN_SOURCE_SYSTEM;
        reason = TURN_REASON_CANNOT_BEAT;
        choice.pass = true;
    } else {
        AiContext seatContext;

        MakeAiContext(state, player, &seatContext);
        choice = AiPlayer_ChooseMove(&state->aiPlayers[PlayerIndex(player)],
                                     &state->players[PlayerIndex(player)].hand, &seatContext);
        if (choice.pass) {
            reason = TURN_REASON_CANNOT_BEAT;
        }
    }

    if (choice.pass) {
        if (!Pass(state, player)) {
            return;
        }
        Cards_Clear(&empty);
        ActionFromCards(&empty, true, &action);
        record = BuildTurnRecord(state, &before, player, source, reason, &action, &action, &empty,
                                 false, NULL, true, "本地 AI 不要");
        AppendRecord(state, &record);
        return;
    }

    PlayCards(state, player, &choice.cards, &choice.pattern, choice.disruptionPenalty);
    ActionFromCards(&choice.cards, false, &action);
    record = BuildTurnRecord(state, &before, player, source, reason, &action, &action,
                             &choice.cards, true, &choice.pattern, true, "本地 AI 出牌");
    AppendRecord(state, &record);
}

static void StartExternalAiTurn(GameState *state)
{
    const ExternalAiController *controller = NULL;
    ExternalAiRequest request;

    if (CountLegalMoves(state, state->currentPlayer) <= 1) {
        PlayLocalAiTurn(state, state->currentPlayer);
        return;
    }

    controller = AiControllerFor(state, state->currentPlayer);
    if (controller == NULL) {
        PlayLocalAiTurn(state, state->currentPlayer);
        return;
    }

    memset(&request, 0, sizeof(request));
    state->externalAiPending = true;
    state->activeExternalAi = *controller;
    state->hasActiveExternalAi = true;
    request.turnNo = state->nextTurnNo;
    request.player = state->currentPlayer;
    Snapshot(state, &request.snapshot);
    MakeAiContext(state, state->currentPlayer, &request.context);
    ExternalAiController_Start(controller, &request);
}

static bool TryCompleteExternalAiTurn(GameState *state)
{
    ExternalAiResult result;

    memset(&result, 0, sizeof(result));
    if (!state->hasActiveExternalAi) {
        state->externalAiPending = false;
        return false;
    }
    if (!ExternalAiController_TryGetResult(&state->activeExternalAi, &result)) {
        return false;
    }
    state->externalAiPending = false;
    state->hasActiveExternalAi = false;
    if (!ApplyExternalAiResult(state, &result)) {
        PlayLocalAiTurn(state, state->currentPlayer);
    }
    return true;
}

static float NextThinkDelay(const GameState *state)
{
    return state->currentPlayer == PLAYER_HUMAN ? 0.35f : 0.75f;
}

/* ---- queries used above ---------------------------------------------- */

static bool IsHumanTurn(const GameState *state)
{
    return state->currentPlayer == PLAYER_HUMAN && !state->roundOver;
}

static bool CurrentPlayerLeads(const GameState *state)
{
    return !state->hasLastPattern;
}

static bool HasPlayableFollow(const GameState *state, PlayerId player)
{
    const Cards *hand;

    if (CurrentPlayerLeads(state) || !state->hasLastPattern) {
        return false;
    }
    hand = &state->players[PlayerIndex(player)].hand;
    /* UI callers use this for pass/button state; keep it independent of AI strategy. */
    return HasAnyFollowMove(hand, &state->lastPattern, hand->count);
}

static void MakeAiContext(const GameState *state, PlayerId player, AiContext *out)
{
    const int currentIndex = PlayerIndex(player);

    memset(out, 0, sizeof(*out));
    out->leading = CurrentPlayerLeads(state);
    if (state->hasLastPattern) {
        out->previous = state->lastPattern;
    }
    out->ownRemainingCards = state->players[currentIndex].hand.count;
    out->currentPlayerIndex = currentIndex;
    out->lastMovePlayerIndex = PlayerIndex(state->lastMovePlayer);
    out->trickLeaderIndex =
        PlayerIndex(CurrentPlayerLeads(state) ? state->currentPlayer : state->trickLeader);
    out->roundLeaderIndex = PlayerIndex(state->roundLeader);
    out->currentTrickPassCount = state->passCount;
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        out->remainingCards[i] = state->players[i].hand.count;
    }
    out->nextPlayerRemainingCards =
        state->players[NextIndex(currentIndex)].hand.count;
    out->minOpponentRemainingCards = INT_MAX;
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        if (i != currentIndex && out->remainingCards[i] < out->minOpponentRemainingCards) {
            out->minOpponentRemainingCards = out->remainingCards[i];
        }
    }
    out->playedCards = state->playedCards;
    PassObservations_Copy(out->passObservations, state->passObservations, PDK_AI_SEATS);
    PassHistories_Copy(out->passHistory, state->passHistory, PDK_AI_SEATS);
}

static void SelectedCards(const GameState *state, Cards *out)
{
    const Cards *hand = &state->players[0].hand;

    Cards_Clear(out);
    for (int index = 0; index < CARDS_MAX; ++index) {
        if (!MaskContains(state->selectedMask, index)) {
            continue;
        }
        if (index < hand->count) {
            Cards_Push(out, hand->items[index]);
        }
    }
}

/* ---- lifetime -------------------------------------------------------- */

void GameState_Init(GameState *state)
{
    memset(state, 0, sizeof(*state));
    state->rules = PaoDeKuaiRules_Create();
    Str_CopyTo(state->players[0].name, PDK_SEAT_NAME_CAP, "\xE6\x9D\x8E\xE5\xA7\x90");
    Str_CopyTo(state->players[1].name, PDK_SEAT_NAME_CAP, "AI1");
    Str_CopyTo(state->players[2].name, PDK_SEAT_NAME_CAP, "AI2");
    state->currentPlayer = PLAYER_HUMAN;
    state->lastMovePlayer = PLAYER_HUMAN;
    state->trickLeader = PLAYER_HUMAN;
    state->roundLeader = PLAYER_HUMAN;
    state->talkPlayer = PLAYER_AI1;
    state->roundOver = true;
    state->nextTurnNo = 1;
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        AiPlayer_Init(&state->aiPlayers[i]);
        PassHistory_Clear(&state->passHistory[i]);
    }
    PassObservations_Clear(state->passObservations, PDK_AI_SEATS);
    for (int i = 0; i < PDK_TALK_KINDS; ++i) {
        state->lastTalkIndices[i] = -1;
    }
    state->roundStrategies[0] = HumanStrategyMetadata();
    state->roundStrategies[1] = BasicStrategyMetadata();
    state->roundStrategies[2] = BasicStrategyMetadata();

    state->events = (GameEvent *)malloc(sizeof(GameEvent) * (size_t)PDK_EVENT_SLOTS);
    if (state->events != NULL) {
        state->eventCap = PDK_EVENT_SLOTS;
    }
    state->turnRecords = (TurnRecord *)malloc(sizeof(TurnRecord) * (size_t)PDK_TURN_RECORDS_MAX);
    if (state->turnRecords != NULL) {
        state->turnRecordCap = PDK_TURN_RECORDS_MAX;
    }
}

void GameState_Destroy(GameState *state)
{
    /*
     * Only the externally supplied controllers are released, which is what the C++
     * destructor did; the per-seat AiPlayer strategies are left alone (see the port
     * report: SetLocalAiStrategy with takeOwnership is never called in-tree, and the
     * original also leaked that ownership).
     */
    for (int i = 0; i < state->externalAiControllerCount; ++i) {
        ExternalAiController_Cancel(&state->externalAiControllers[i]);
        ExternalAiController_Destroy(&state->externalAiControllers[i]);
    }
    state->externalAiControllerCount = 0;
    free(state->events);
    state->events = NULL;
    state->eventCap = 0;
    state->eventCount = 0;
    state->eventHead = 0;
    free(state->turnRecords);
    state->turnRecords = NULL;
    state->turnRecordCap = 0;
    state->turnRecordCount = 0;
}

/* ---- the round ------------------------------------------------------- */

void GameState_StartNewRound(GameState *state, const char *playerName, unsigned seed)
{
    Cards deck;
    Cards hands[PDK_AI_SEATS];
    bool requestedLeaderHas;
    PlayerId requestedLeader;

    if (seed == 0) {
        seed = (unsigned)time(NULL);
    }
    state->roundSeed = seed;
    state->roundTraceWritten = false;
    state->lastRoundTracePath[0] = '\0';
    memcpy(state->initialStrategies, state->roundStrategies, sizeof(state->initialStrategies));
    requestedLeaderHas = state->hasNextRoundLeader;
    requestedLeader = state->nextRoundLeader;
    state->hasNextRoundLeader = false;

    if (playerName == NULL || playerName[0] == '\0') {
        playerName = "\xE6\x9D\x8E\xE5\xA7\x90";
    }
    Str_CopyTo(state->playerName, PDK_SEAT_NAME_CAP, playerName);
    PlayerState_Init(&state->players[0], state->playerName);
    PlayerState_Init(&state->players[1], "AI1");
    PlayerState_Init(&state->players[2], "AI2");
    state->selectedMask = 0;
    state->hintMask = 0;
    state->bombCount = 0;
    state->hasStandingBomb = false;
    Cards_Clear(&state->lastCards);
    Cards_Clear(&state->playedCards);
    PassObservations_Clear(state->passObservations, PDK_AI_SEATS);
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        PassHistory_Clear(&state->passHistory[i]);
    }
    state->hasLastPattern = false;
    state->passCount = 0;
    state->roundOver = false;
    state->autoplay = false;
    state->aiDelay = 0.45f;
    state->talkCooldown = 0.0f;
    state->talkText[0] = '\0';
    for (int i = 0; i < PDK_TALK_KINDS; ++i) {
        state->lastTalkIndices[i] = -1;
    }
    state->turnRecordCount = 0;
    state->nextTurnNo = 1;
    state->externalAiPending = false;
    state->hasActiveExternalAi = false;
    for (int i = 0; i < state->externalAiControllerCount; ++i) {
        ExternalAiController_Cancel(&state->externalAiControllers[i]);
    }
    Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, "新一局开始");
    NowTimeText(state->startedAt, PDK_TIME_TEXT_CAP);
    RoundRecord_Init(&state->lastRoundRecord);
    ClearEvents(state);

    deck = PaoDeKuaiRules_CreateDeck(&state->rules);
    Shuffle(&deck, seed);
    for (int i = 0; i < deck.count; ++i) {
        Cards_Push(&state->players[i % 3].hand, deck.items[i]);
    }
    memcpy(state->initialPlayers, state->players, sizeof(state->initialPlayers));
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        hands[i] = state->players[i].hand;
    }
    state->currentPlayer = requestedLeaderHas
                               ? requestedLeader
                               : PlayerFromIndex(FindFirstPlayerBySpadeThree(hands,
                                                                            PDK_AI_SEATS));
    state->lastMovePlayer = state->currentPlayer;
    state->trickLeader = state->currentPlayer;
    state->roundLeader = state->currentPlayer;
    state->aiDelay = NextThinkDelay(state);
    AddEvent(state, GAME_EVENT_ROUND_STARTED, state->currentPlayer,
             requestedLeaderHas ? "上局赢家先出" : "黑桃 3 玩家先出", NULL);
}

void GameState_Update(GameState *state, float dt)
{
    bool aiControlled;

    if (state->roundOver) {
        return;
    }
    if (state->talkCooldown > 0.0f) {
        state->talkCooldown -= dt;
    }

    if (state->externalAiPending) {
        if (TryCompleteExternalAiTurn(state)) {
            state->aiDelay = NextThinkDelay(state);
        }
        return;
    }

    aiControlled = state->currentPlayer != PLAYER_HUMAN || state->autoplay;
    if (!aiControlled) {
        return;
    }

    state->aiDelay -= dt;
    if (state->aiDelay > 0.0f) {
        return;
    }

    if (AiControllerFor(state, state->currentPlayer) != NULL) {
        StartExternalAiTurn(state);
    } else {
        PlayLocalAiTurn(state, state->currentPlayer);
    }
    state->aiDelay = NextThinkDelay(state);
}

/* ---- queries --------------------------------------------------------- */

bool GameState_IsRoundOver(const GameState *state)
{
    return state->roundOver;
}

bool GameState_IsHumanTurn(const GameState *state)
{
    return IsHumanTurn(state);
}

PlayerId GameState_CurrentPlayer(const GameState *state)
{
    return state->currentPlayer;
}

PlayerId GameState_LastMovePlayer(const GameState *state)
{
    return state->lastMovePlayer;
}

const PlayerState *GameState_Players(const GameState *state)
{
    return state->players;
}

const Cards *GameState_LastCards(const GameState *state)
{
    return &state->lastCards;
}

const HandPattern *GameState_LastPattern(const GameState *state)
{
    return state->hasLastPattern ? &state->lastPattern : NULL;
}

const Cards *GameState_PlayedCards(const GameState *state)
{
    return &state->playedCards;
}

const OptionalPassObservation *GameState_PassObservations(const GameState *state)
{
    return state->passObservations;
}

uint64_t GameState_SelectedMask(const GameState *state)
{
    return state->selectedMask;
}

uint64_t GameState_HintMask(const GameState *state)
{
    return state->hintMask;
}

const char *GameState_Toast(const GameState *state)
{
    return state->toast;
}

const char *GameState_TalkText(const GameState *state)
{
    return state->talkText;
}

PlayerId GameState_TalkPlayer(const GameState *state)
{
    return state->talkPlayer;
}

bool GameState_Autoplay(const GameState *state)
{
    return state->autoplay;
}

const RoundRecord *GameState_LastRoundRecord(const GameState *state)
{
    return &state->lastRoundRecord;
}

const BombScoreEvent *GameState_BombEvents(const GameState *state)
{
    return state->bombs;
}

int GameState_BombEventCount(const GameState *state)
{
    return state->bombCount;
}

const TurnRecord *GameState_TurnRecords(const GameState *state)
{
    return state->turnRecords;
}

int GameState_TurnRecordCount(const GameState *state)
{
    return state->turnRecordCount;
}

bool GameState_ExternalAiPending(const GameState *state)
{
    return state->externalAiPending;
}

bool GameState_CanCurrentPlayerPass(const GameState *state)
{
    return IsHumanTurn(state) && !CurrentPlayerLeads(state) &&
           !HasPlayableFollow(state, PLAYER_HUMAN);
}

bool GameState_IsInLeadState(const GameState *state)
{
    return CurrentPlayerLeads(state);
}

const char *GameState_LastRoundTracePath(const GameState *state)
{
    return state->lastRoundTracePath;
}

const GameEvent *GameState_EventAt(const GameState *state, int index)
{
    if (state->events == NULL || state->eventCap <= 0 || index < 0 ||
        index >= state->eventCount) {
        return NULL;
    }
    return &state->events[(state->eventHead + index) % state->eventCap];
}

int GameState_EventCount(const GameState *state)
{
    return state->eventCount;
}

int GameState_EventsDropped(const GameState *state)
{
    return state->eventsDropped;
}

int GameState_TurnRecordsDropped(const GameState *state)
{
    return state->turnRecordsDropped;
}

void GameState_ClearEvents(GameState *state)
{
    ClearEvents(state);
}

/* ---- player actions -------------------------------------------------- */

void GameState_ToggleAutoplay(GameState *state)
{
    state->autoplay = !state->autoplay;
    Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, state->autoplay ? "托管已开启" : "托管已取消");
}

void GameState_TogglePlayerCard(GameState *state, int handIndex)
{
    if (!IsHumanTurn(state) || handIndex < 0 || handIndex >= state->players[0].hand.count) {
        return;
    }
    state->hintMask = 0;
    if (MaskContains(state->selectedMask, handIndex)) {
        state->selectedMask &= ~IndexBit(handIndex);
    } else {
        state->selectedMask |= IndexBit(handIndex);
    }
}

void GameState_ClearSelection(GameState *state)
{
    state->selectedMask = 0;
    state->hintMask = 0;
}

void GameState_SortHands(GameState *state)
{
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        SortByGameOrder(&state->players[i].hand);
    }
}

bool GameState_PlaySelected(GameState *state)
{
    Cards cards;
    const int handSize = state->players[0].hand.count;
    MoveValidation validation;
    TurnSnapshot before;
    GameAction action;
    TurnRecord record;

    if (!IsHumanTurn(state)) {
        return false;
    }
    SelectedCards(state, &cards);
    validation = CurrentPlayerLeads(state)
                     ? PaoDeKuaiRules_ValidateLeadMove(&state->rules, &cards, handSize)
                     : PaoDeKuaiRules_ValidateFollowMove(&state->rules, &cards,
                                                         &state->lastPattern, handSize);
    if (!validation.ok) {
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, validation.reason);
        AddEvent(state, GAME_EVENT_INVALID_MOVE, PLAYER_HUMAN, validation.reason, &cards);
        return false;
    }

    Snapshot(state, &before);
    PlayCards(state, PLAYER_HUMAN, &cards, &validation.pattern, 0);
    ActionFromCards(&cards, false, &action);
    record = BuildTurnRecord(state, &before, PLAYER_HUMAN, TURN_SOURCE_HUMAN,
                             TURN_REASON_NORMAL_CHOICE, &action, &action, &cards, true,
                             &validation.pattern, true, "玩家出牌");
    AppendRecord(state, &record);
    state->selectedMask = 0;
    state->hintMask = 0;
    return true;
}

bool GameState_PassHuman(GameState *state)
{
    TurnSnapshot before;
    GameAction action;
    Cards empty;
    TurnRecord record;

    if (!IsHumanTurn(state)) {
        return false;
    }
    Snapshot(state, &before);
    if (!Pass(state, PLAYER_HUMAN)) {
        return false;
    }
    Cards_Clear(&empty);
    ActionFromCards(&empty, true, &action);
    record = BuildTurnRecord(state, &before, PLAYER_HUMAN, TURN_SOURCE_HUMAN,
                             TURN_REASON_CANNOT_BEAT, &action, &action, &empty, false, NULL, true,
                             "玩家不要");
    AppendRecord(state, &record);
    return true;
}

bool GameState_ApplyHint(GameState *state)
{
    AiContext humanContext;
    AiMoveChoice choice;
    uint64_t recommendedMask = 0;

    if (!IsHumanTurn(state)) {
        return false;
    }
    MakeAiContext(state, PLAYER_HUMAN, &humanContext);
    choice = AiPlayer_ChooseMove(&state->aiPlayers[0], &state->players[0].hand, &humanContext);
    if (choice.pass) {
        if (!CurrentPlayerLeads(state)) {
            TurnSnapshot before;
            GameAction action;
            Cards empty;
            TurnRecord record;

            Snapshot(state, &before);
            if (!Pass(state, PLAYER_HUMAN)) {
                return false;
            }
            Cards_Clear(&empty);
            ActionFromCards(&empty, true, &action);
            record = BuildTurnRecord(state, &before, PLAYER_HUMAN, TURN_SOURCE_HUMAN,
                                     TURN_REASON_CANNOT_BEAT, &action, &action, &empty, false, NULL,
                                     true, "提示直接不要");
            AppendRecord(state, &record);
            return true;
        }
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, choice.reason);
        AddEvent(state, GAME_EVENT_HINT, PLAYER_HUMAN, choice.reason, NULL);
        return false;
    }

    /*
     * The original built a std::set of the matching hand indices plus a std::vector with one
     * entry per (hand index, chosen card) match.  A real choice is a subset of the hand with
     * no repeated card, so those two hold the same ascending indices; the mask is that set.
     */
    for (int i = 0; i < state->players[0].hand.count; ++i) {
        for (int c = 0; c < choice.cards.count; ++c) {
            if (SameCard(state->players[0].hand.items[i], choice.cards.items[c])) {
                recommendedMask |= IndexBit(i);
            }
        }
    }

    /* Hint click acts like a toggle for the exact recommended cards: switch to
     * the recommendation when selection differs, clear it when already selected. */
    if (state->selectedMask == recommendedMask) {
        state->selectedMask = 0;
        state->hintMask = 0;
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, "已取消提示选择");
    } else {
        state->selectedMask = recommendedMask;
        state->hintMask = recommendedMask;
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, "已按 AI 逻辑选中推荐牌");
    }
    AddEvent(state, GAME_EVENT_HINT, PLAYER_HUMAN, choice.reason, &choice.cards);
    return true;
}

bool GameState_SelectByHoverPattern(GameState *state, int handIndex)
{
    if (!IsHumanTurn(state) || handIndex < 0 || handIndex >= state->players[0].hand.count) {
        return false;
    }
    return GameState_SelectBestPatternFromDraggedCards(state, &handIndex, 1);
}

bool GameState_SelectBestPatternFromDraggedCards(GameState *state, const int *handIndices,
                                                 int count)
{
    const Cards *hand;
    bool inDragPath[CARDS_MAX];
    int dragIndices[CARDS_MAX];
    int dragCount = 0;
    uint64_t dragLimit;
    uint64_t chosenIndices = 0;
    HandPattern chosenPattern;
    Cards chosenCards;
    HandPattern additivePattern;
    uint64_t additiveMask = 0;
    bool hasChosen = false;
    bool hasAdditive = false;
    int chosenScore = 0;
    int additiveScore = 0;

    if (!IsHumanTurn(state)) {
        return false;
    }

    memset(inDragPath, 0, sizeof(inDragPath));
    memset(&chosenPattern, 0, sizeof(chosenPattern));
    memset(&additivePattern, 0, sizeof(additivePattern));
    Cards_Clear(&chosenCards);
    hand = &state->players[0].hand;
    if (handIndices == NULL) {
        count = 0;
    }
    for (int i = 0; i < count; ++i) {
        const int handIndex = handIndices[i];

        if (handIndex >= 0 && handIndex < hand->count) {
            inDragPath[handIndex] = true;
        }
    }
    for (int i = 0; i < hand->count; ++i) {
        if (inDragPath[i]) {
            dragIndices[dragCount++] = i;
        }
    }
    if (dragCount == 0) {
        return false;
    }
    dragLimit = dragCount >= 63 ? 0 : ((uint64_t)1 << dragCount);

    /*
     * The original collected every valid subset into a `candidates` vector and then took
     * max_element over it.  Only the winner is observable, and max_element keeps the first
     * maximum, so tracking "strictly greater" reproduces it without the vector; the same
     * applies to the additive pass below.
     */
    for (uint64_t mask = 1; mask < dragLimit; ++mask) {
        int score;
        Cards cards;
        HandPattern pattern;
        bool hasPattern = false;
        MoveValidation validation;

        Cards_Clear(&cards);
        for (int i = 0; i < dragCount; ++i) {
            if ((mask & ((uint64_t)1 << i)) != 0) {
                Cards_Push(&cards, hand->items[dragIndices[i]]);
            }
        }

        validation = PaoDeKuaiRules_ValidateLeadMove(&state->rules, &cards, hand->count);
        if (validation.ok) {
            pattern = validation.pattern;
            hasPattern = true;
        } else if (IdentifyDragOnlyPattern(&cards, &pattern)) {
            /* Drag selection is a visual grouping helper, not a promise that the
             * current selection can be played; PlaySelected still enforces rules. */
            hasPattern = true;
        }
        if (!hasPattern) {
            continue;
        }

        score = ScorePattern(&pattern);
        if (!hasChosen || score > chosenScore) {
            hasChosen = true;
            chosenScore = score;
            chosenPattern = pattern;
            chosenCards = cards;
        }
    }

    if (state->selectedMask != 0) {
        /* When a core group is already selected, dragging over loose cards should
         * first try to complete a larger legal move such as three-with-two or a
         * plane with wings. The dragged cards alone may not be a valid pattern. */
        for (uint64_t mask = 1; mask < dragLimit; ++mask) {
            uint64_t indices = state->selectedMask;
            int score;
            Cards cards;
            MoveValidation validation;

            for (int i = 0; i < dragCount; ++i) {
                if ((mask & ((uint64_t)1 << i)) != 0) {
                    indices |= IndexBit(dragIndices[i]);
                }
            }
            if (indices == state->selectedMask) {
                continue;
            }

            Cards_Clear(&cards);
            for (int index = 0; index < hand->count; ++index) {
                if (MaskContains(indices, index)) {
                    Cards_Push(&cards, hand->items[index]);
                }
            }
            validation = PaoDeKuaiRules_ValidateLeadMove(&state->rules, &cards, hand->count);
            if (!validation.ok) {
                continue;
            }

            score = ScorePattern(&validation.pattern);
            if (!hasAdditive || score > additiveScore) {
                hasAdditive = true;
                additiveScore = score;
                additiveMask = indices;
                additivePattern = validation.pattern;
            }
        }
    }

    if (!hasChosen && !hasAdditive) {
        return false;
    }

    if (hasChosen) {
        /* Resolve the winning cards back to hand indices by identity, exactly as the
         * original did before comparing with the current selection. */
        for (int i = 0; i < hand->count; ++i) {
            for (int c = 0; c < chosenCards.count; ++c) {
                if (SameCard(hand->items[i], chosenCards.items[c])) {
                    chosenIndices |= IndexBit(i);
                }
            }
        }
    }

    /* Repeating the same drag gesture toggles the selected group off, matching
     * hint-button behavior for an already selected recommendation. */
    if (chosenIndices != 0 && state->selectedMask == chosenIndices) {
        state->selectedMask = 0;
        state->hintMask = 0;
        Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, "已取消拖拽选择");
    } else {
        uint64_t finalIndices = chosenIndices;
        HandPattern finalPattern = chosenPattern;

        if (state->selectedMask != 0) {
            if (hasAdditive) {
                finalIndices = additiveMask;
                finalPattern = additivePattern;
            } else {
                finalIndices = state->selectedMask | chosenIndices;
            }
        }

        state->selectedMask = finalIndices;
        state->hintMask = finalIndices;
        {
            char description[PDK_EVENT_TEXT_CAP];
            Str text;

            DragPatternDescription(&finalPattern, description, PDK_EVENT_TEXT_CAP);
            Str_Init(&text);
            Str_Append(&text, "已按拖拽路线选中 ");
            Str_Append(&text, description);
            Str_CopyTo(state->toast, PDK_EVENT_TEXT_CAP, Str_CStr(&text));
            Str_Free(&text);
        }
    }
    return true;
}

/* ---- AI wiring ------------------------------------------------------- */

void GameState_SetExternalAiController(GameState *state, ExternalAiController controller)
{
    if (controller.vtbl != NULL) {
        GameState_SetExternalAiControllers(state, &controller, 1);
    } else {
        GameState_SetExternalAiControllers(state, NULL, 0);
    }
}

void GameState_SetExternalAiControllers(GameState *state,
                                        const ExternalAiController *controllers, int count)
{
    for (int i = 0; i < state->externalAiControllerCount; ++i) {
        ExternalAiController_Cancel(&state->externalAiControllers[i]);
        ExternalAiController_Destroy(&state->externalAiControllers[i]);
    }
    state->externalAiControllerCount = 0;
    for (int i = 0; controllers != NULL && i < count && i < PDK_AI_SEATS; ++i) {
        if (controllers[i].vtbl != NULL) {
            state->externalAiControllers[state->externalAiControllerCount++] = controllers[i];
        }
    }
    state->roundStrategies[0] = HumanStrategyMetadata();
    state->roundStrategies[1] = BasicStrategyMetadata();
    state->roundStrategies[2] = BasicStrategyMetadata();
    for (int i = 1; i < PDK_AI_SEATS; ++i) {
        const PlayerId player = PlayerFromIndex(i);

        for (int c = 0; c < state->externalAiControllerCount; ++c) {
            if (ExternalAiController_CanHandle(&state->externalAiControllers[c], player)) {
                state->roundStrategies[i] =
                    ExternalAiController_MetadataFor(&state->externalAiControllers[c], player);
                break;
            }
        }
    }
    state->hasActiveExternalAi = false;
    state->externalAiPending = false;
}

void GameState_SetLocalAiStrategy(GameState *state, PlayerId player, AiStrategy strategy,
                                 bool takeOwnership)
{
    const int index = PlayerIndex(player);

    AiPlayer_SetStrategy(&state->aiPlayers[index], strategy, takeOwnership);
    state->roundStrategies[index] = AiPlayer_Metadata(&state->aiPlayers[index]);
}

void GameState_SetRoundTraceEnabled(GameState *state, bool enabled)
{
    state->roundTraceEnabled = enabled;
}

void GameState_SetRoundTraceRoot(GameState *state, const char *root)
{
    Str_CopyTo(state->roundTraceRoot, PDK_STAT_ROOT_CAP, root);
}

/* ---- test hook ------------------------------------------------------- */

void GameState_TestSetRound(GameState *state, const Cards *hands, PlayerId currentPlayer,
                            const HandPattern *previousPattern, PlayerId lastMovePlayer)
{
    Str_CopyTo(state->players[0].name, PDK_SEAT_NAME_CAP, "Tester");
    state->players[0].hand = hands[0];
    state->players[0].hasPlayedCards = false;
    Str_CopyTo(state->players[1].name, PDK_SEAT_NAME_CAP, "AI1");
    state->players[1].hand = hands[1];
    state->players[1].hasPlayedCards = true;
    Str_CopyTo(state->players[2].name, PDK_SEAT_NAME_CAP, "AI2");
    state->players[2].hand = hands[2];
    state->players[2].hasPlayedCards = true;

    memcpy(state->initialPlayers, state->players, sizeof(state->initialPlayers));
    memcpy(state->initialStrategies, state->roundStrategies, sizeof(state->initialStrategies));
    Str_CopyTo(state->playerName, PDK_SEAT_NAME_CAP, state->players[0].name);
    NowTimeText(state->startedAt, PDK_TIME_TEXT_CAP);
    state->currentPlayer = currentPlayer;
    state->lastMovePlayer = lastMovePlayer;
    state->trickLeader = previousPattern != NULL ? lastMovePlayer : currentPlayer;
    state->roundLeader = currentPlayer;
    state->hasLastPattern = previousPattern != NULL;
    if (previousPattern != NULL) {
        state->lastPattern = *previousPattern;
    }
    Cards_Clear(&state->lastCards);
    Cards_Clear(&state->playedCards);
    PassObservations_Clear(state->passObservations, PDK_AI_SEATS);
    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        PassHistory_Clear(&state->passHistory[i]);
    }
    state->passCount = 0;
    state->roundOver = false;
    state->autoplay = false;
    state->selectedMask = 0;
    state->hintMask = 0;
    state->bombCount = 0;
    state->hasStandingBomb = false;
    ClearEvents(state);
    state->toast[0] = '\0';
    state->turnRecordCount = 0;
    state->nextTurnNo = 1;
    state->roundTraceWritten = false;
    state->lastRoundTracePath[0] = '\0';
    state->roundSeed = 0;
    state->externalAiPending = false;
    state->hasActiveExternalAi = false;
    for (int i = 0; i < state->externalAiControllerCount; ++i) {
        ExternalAiController_Cancel(&state->externalAiControllers[i]);
    }
}
