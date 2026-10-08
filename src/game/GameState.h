#pragma once

/*
 * The game state machine: one three-player round of Pao De Kuai, from the deal to the
 * round-end score.
 *
 * Pure C.  The C++ class became a value type behind GameState_Init/GameState_Destroy and
 * every member function became GameState_Xxx(state, ...).  Six things needed care:
 *
 *   - `std::optional<HandPattern>` is a `HandPattern` plus `hasLastPattern`, and
 *     `std::optional<PlayerId>` a value plus `hasNextRoundLeader`.
 *   - `std::optional<std::size_t> standingBombIndex_` is an index plus
 *     `hasStandingBomb`.
 *   - `std::set<int>` (the selection) and `std::vector<int>` (the hint) of hand indices
 *     are `uint64_t` bitmasks.  A hand is at most CARDS_MAX (48) cards, so one word
 *     covers every index the game can produce, duplicates are impossible by
 *     construction, and iterating bits ascending is exactly what iterating the ordered
 *     set did.
 *   - the text members (toast, talk line, player name, trace root and path, the event
 *     messages) are fixed char buffers filled with Str_CopyTo.
 *   - the two collections a round cannot bound to a few bytes -- the event queue and the
 *     turn-record audit trail -- are fixed-capacity arrays allocated once by
 *     GameState_Init and owned by the state.  Both capacities are derived from the rules
 *     in the comments next to PDK_EVENT_SLOTS / PDK_TURN_RECORDS_MAX.
 *   - `LegalMoves()` built a vector of every legal (cards, pattern) pair per AI turn and
 *     only ever had `.empty()` / `.size()` called on it, so the C side counts the legal
 *     moves instead of materialising them.  Same enumeration, same validation, no
 *     per-turn vector.
 *
 * The struct is defined here rather than kept opaque because the temporary C++ facade in
 * game/CppCompat.h holds one by value; everything outside that pair goes through the
 * GameState_* functions.
 */

#include "game/AiPlayer.h"
#include "game/ExternalAiController.h"
#include "game/Player.h"
#include "rules/PaoDeKuaiRules.h"
#include "rules/Scoring.h"
#include "stats/StatStore.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Event kinds, in the order the old `enum class GameEventType` listed them; the C++
 * facade in game/CppCompat.h static_asserts that its enum keeps matching these values.
 */
typedef uint8_t GameEventType;

enum {
    GAME_EVENT_NONE = 0,
    GAME_EVENT_ROUND_STARTED = 1,
    GAME_EVENT_CARDS_PLAYED = 2,
    GAME_EVENT_PASSED = 3,
    GAME_EVENT_INVALID_MOVE = 4,
    GAME_EVENT_HINT = 5,
    GAME_EVENT_BOMB = 6,
    GAME_EVENT_ROUND_ENDED = 7,
    GAME_EVENT_TALK = 8
};

/*
 * The longest event text is "<player name> 出了 <pattern description>" from PlayCards:
 * the name buffer is PDK_SEAT_NAME_CAP (64) and PatternDescription is bounded by
 * PATTERN_REASON_CAP (128), so the worst case is 63 + 5 + 127 = 195 bytes.  The round-end
 * toast adds three numbers to a short prefix.  256 = PDK_TURN_TEXT_CAP covers both, and
 * the same cap is used for the toast and the talk line.
 */
enum { PDK_EVENT_TEXT_CAP = 256 };

/*
 * Event queue capacity.  GameScene clears the queue every frame, but a test or a caller
 * may not, so the ring is sized for a whole round without a single ClearEvents.  The
 * worst case a 48-card three-player round can produce is:
 *
 *   round started         1   (StartNewRound adds exactly one)
 *   card plays           48   (a play always removes at least one card and the deck is 48)
 *   passes               96   (a trick ends after two passes and a trick holds >= 1 play)
 *   bombs                12   (BOMB_EVENTS_MAX; the deck cannot hold a 12th four-of-a-kind)
 *   "your turn" talks   144   (at most one per turn record above)
 *   AI talk lines       145   (at most one per play or pass, plus the round-end line)
 *   round ended           1
 *   ------------------------
 *   sum                 447
 *
 * 512 rounds that up; invalid-move toasts (which a caller drives by hand) are the only
 * other source.  The queue is a ring buffer, so if it ever did fill, the OLDEST event is
 * dropped and GameState_EventsDropped reports it: deterministic, and never silent.
 */
enum { PDK_EVENT_SLOTS = 512 };

/*
 * Turn-record audit trail capacity.  Every record is one play or one pass, and:
 *   - a play removes at least one card from a hand, never adds any, and the round starts
 *     from a 48-card deck, so there are at most 48 plays;
 *   - a trick ends after two passes (passCount >= 2), and a trick always contains the
 *     play that led it, so there are at most as many tricks as plays -- hence at most
 *     2 * 48 = 96 passes.
 * 48 + 96 = 144 records, which is exactly the bound.  AppendRecord cannot reach it; if a
 * future rule change ever made it reachable, the oldest record would be dropped and
 * GameState_TurnRecordsDropped would count it, so a truncated trace stays visible.
 */
enum { PDK_TURN_RECORDS_MAX = 144 };

/* The number of distinct AI talk pools; was `TalkKind::Count`. */
enum { PDK_TALK_KINDS = 15 };

typedef struct GameEvent {
    GameEventType type;
    PlayerId player;
    char message[PDK_EVENT_TEXT_CAP];
    Cards cards;
} GameEvent;

typedef struct GameState {
    PaoDeKuaiRules rules;

    PlayerState players[PDK_AI_SEATS];
    AiPlayer aiPlayers[PDK_AI_SEATS];
    /*
     * The externally supplied controllers, and how many of them there are.  The old
     * class comment is still the contract: GameState owns them and destroys them when it
     * is destroyed or handed a new set, and at most PDK_AI_SEATS are kept, one per seat.
     * `activeExternalAi` is a borrow of the same (vtable, user) pair as one of the owning
     * slots.  Both fields are cleared by GameState_Init, never left indeterminate.
     */
    ExternalAiController externalAiControllers[PDK_AI_SEATS];
    int externalAiControllerCount;
    ExternalAiController activeExternalAi;
    bool hasActiveExternalAi;

    PlayerId currentPlayer;
    PlayerId lastMovePlayer;
    PlayerId trickLeader;
    PlayerId roundLeader;

    HandPattern lastPattern;
    bool hasLastPattern;
    PlayerId nextRoundLeader;
    bool hasNextRoundLeader;

    Cards lastCards;
    Cards playedCards;

    OptionalPassObservation passObservations[PDK_AI_SEATS];
    PassHistory passHistory[PDK_AI_SEATS];
    int passCount;

    bool roundOver;
    bool autoplay;
    float aiDelay;
    float talkCooldown;

    /* Hand indices, bit i = players[0].hand.items[i].  A hand is at most CARDS_MAX (48)
     * cards, so one 64-bit word holds every index the game can produce. */
    uint64_t selectedMask;
    uint64_t hintMask;

    /* At most eleven four-of-a-kind groups exist (3..K; there is no A or 2 bomb), so
     * BOMB_EVENTS_MAX (12) is the proven bound -- the same one Scoring.h uses. */
    BombScoreEvent bombs[BOMB_EVENTS_MAX];
    int bombCount;
    int standingBombIndex;
    bool hasStandingBomb;

    char startedAt[PDK_TIME_TEXT_CAP];
    /* Was a std::string.  Every consumer (PlayerState.name, RoundRecord.playerName) is
     * capped at PDK_SEAT_NAME_CAP anyway and the only writer is the settings field of that
     * size, so one buffer serves all three; a longer name is truncated to 63 bytes. */
    char playerName[PDK_SEAT_NAME_CAP];

    /* PDK_EVENT_SLOTS entries, owned; NULL only when the allocation in GameState_Init
     * failed, in which case eventCap stays 0 and EventsDropped counts every event. */
    GameEvent *events;
    int eventCap;
    int eventHead;  /* index of the oldest queued event */
    int eventCount;
    int eventsDropped;

    char toast[PDK_EVENT_TEXT_CAP];
    char talkText[PDK_EVENT_TEXT_CAP];
    PlayerId talkPlayer;
    RoundRecord lastRoundRecord;
    int lastTalkIndices[PDK_TALK_KINDS];

    /* PDK_TURN_RECORDS_MAX entries, owned and contiguous oldest-first, because
     * RoundTrace points straight at them.  NULL under the same failure as `events`. */
    TurnRecord *turnRecords;
    int turnRecordCap;
    int turnRecordCount;
    int turnRecordsDropped;

    bool externalAiPending;
    int nextTurnNo;
    bool roundTraceEnabled;
    bool roundTraceWritten;
    unsigned roundSeed;
    char roundTraceRoot[PDK_STAT_ROOT_CAP];
    char lastRoundTracePath[PDK_STAT_PATH_CAP];

    PlayerState initialPlayers[PDK_AI_SEATS];
    StrategyMetadata roundStrategies[PDK_AI_SEATS];
    StrategyMetadata initialStrategies[PDK_AI_SEATS];
} GameState;

/* ---- lifetime -------------------------------------------------------- */

/* Zeroes the state, installs the default seat names, the basic per-seat strategies and
 * the default round strategies, and allocates the event queue and the audit trail.  A
 * failed allocation leaves that buffer empty (cap 0) instead of failing the call: the
 * round then runs without the audit data, which the drop counters make visible. */
void GameState_Init(GameState *state);
void GameState_Destroy(GameState *state);

/* ---- the round ------------------------------------------------------- */

/* `seed` 0 means "pick the wall clock", matching the old std::time(nullptr) default.
 * `playerName` NULL or empty becomes the default name. */
void GameState_StartNewRound(GameState *state, const char *playerName, unsigned seed);
void GameState_Update(GameState *state, float dt);

/* ---- state queries --------------------------------------------------- */

bool GameState_IsRoundOver(const GameState *state);
bool GameState_IsHumanTurn(const GameState *state);
PlayerId GameState_CurrentPlayer(const GameState *state);
PlayerId GameState_LastMovePlayer(const GameState *state);
/* PDK_AI_SEATS entries in seat order. */
const PlayerState *GameState_Players(const GameState *state);
const Cards *GameState_LastCards(const GameState *state);
/* NULL while the current player leads (the old empty optional). */
const HandPattern *GameState_LastPattern(const GameState *state);
const Cards *GameState_PlayedCards(const GameState *state);
const OptionalPassObservation *GameState_PassObservations(const GameState *state);
/* Bit i = hand index i of the human seat. */
uint64_t GameState_SelectedMask(const GameState *state);
uint64_t GameState_HintMask(const GameState *state);
const char *GameState_Toast(const GameState *state);
const char *GameState_TalkText(const GameState *state);
PlayerId GameState_TalkPlayer(const GameState *state);
bool GameState_Autoplay(const GameState *state);
const RoundRecord *GameState_LastRoundRecord(const GameState *state);
const BombScoreEvent *GameState_BombEvents(const GameState *state);
int GameState_BombEventCount(const GameState *state);
/* Contiguous, oldest first; the audit trail AppendRecord fills. */
const TurnRecord *GameState_TurnRecords(const GameState *state);
int GameState_TurnRecordCount(const GameState *state);
int GameState_TurnRecordsDropped(const GameState *state);
bool GameState_ExternalAiPending(const GameState *state);
bool GameState_CanCurrentPlayerPass(const GameState *state);
bool GameState_IsInLeadState(const GameState *state);
const char *GameState_LastRoundTracePath(const GameState *state);

/* ---- the event queue ------------------------------------------------- */

/* The i-th oldest queued event, or NULL when out of range or when the queue could not be
 * allocated.  Events are a ring buffer: a full queue drops the oldest, which
 * GameState_EventsDropped counts. */
const GameEvent *GameState_EventAt(const GameState *state, int index);
int GameState_EventCount(const GameState *state);
int GameState_EventsDropped(const GameState *state);
void GameState_ClearEvents(GameState *state);

/* ---- player actions -------------------------------------------------- */

void GameState_ToggleAutoplay(GameState *state);
void GameState_TogglePlayerCard(GameState *state, int handIndex);
void GameState_ClearSelection(GameState *state);
void GameState_SortHands(GameState *state);
bool GameState_PlaySelected(GameState *state);
bool GameState_PassHuman(GameState *state);
bool GameState_ApplyHint(GameState *state);
bool GameState_SelectByHoverPattern(GameState *state, int handIndex);
/* `handIndices` is the drag route, in any order; `count` its length. */
bool GameState_SelectBestPatternFromDraggedCards(GameState *state, const int *handIndices,
                                                 int count);

/* ---- AI wiring ------------------------------------------------------- */

/* Takes ownership of the controllers: GameState destroys them when it replaces them or
 * is destroyed.  At most PDK_AI_SEATS are kept, one per seat. */
void GameState_SetExternalAiController(GameState *state, ExternalAiController controller);
void GameState_SetExternalAiControllers(GameState *state,
                                        const ExternalAiController *controllers, int count);
void GameState_SetLocalAiStrategy(GameState *state, PlayerId player, AiStrategy strategy,
                                  bool takeOwnership);

/* ---- round trace ----------------------------------------------------- */

void GameState_SetRoundTraceEnabled(GameState *state, bool enabled);
/* NULL or "" means the process current directory, as in RoundTraceRecorder_WriteRound. */
void GameState_SetRoundTraceRoot(GameState *state, const char *root);

/* ---- test hook ------------------------------------------------------- */

/* `hands` holds PDK_AI_SEATS hands; `previousPattern` NULL means "the table is clear". */
void GameState_TestSetRound(GameState *state, const Cards *hands, PlayerId currentPlayer,
                            const HandPattern *previousPattern, PlayerId lastMovePlayer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifdef __cplusplus
/*
 * TEMPORARY transition include, deleted with src/game/CppCompat.h.
 *
 * The not-yet-converted C++ callers (tests/rules_tests and src/scenes/GameScene.cpp) still
 * spell the state machine `game::GameState`, and game/CppCompat.h is where that facade
 * lives.  They include this header rather than the facade header, so the include is here
 * to keep that include graph unchanged; a C compiler never sees it.  The two headers
 * include each other, which `#pragma once` resolves in either order because the facade
 * only needs the declarations above.
 */
#include "game/CppCompat.h"
#endif
