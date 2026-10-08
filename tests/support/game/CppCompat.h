#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * The game value types are pure C now (plan.md S4), so GameAction, PlayerState,
 * StrategyMetadata, TurnSnapshot, TurnDecisionTrace and TurnRecord live at global
 * scope.  The C++ translation units that are not converted yet spell them
 * `game::X`, and this header is the only thing that keeps that spelling working.
 *
 * It grows as the rest of src/game is converted; it disappears together with the
 * last C++ file under src/.
 */

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/AiPlayer.h"
#include "rules/CppCompat.h"
#include "game/ExternalAiController.h"
#include "game/GameState.h"
#include "game/LocalAiController.h"
#include "game/Player.h"
#include "game/StrategyMetadata.h"
#include "game/TurnRecord.h"
#include "stats/CppCompat.h"

namespace pdk::game {

using ::AiContext;
using ::AiMoveChoice;
using ::AiPlayer;
using ::AiStrategy;
using ::AiStrategyVtbl;
using ::GameAction;
using ::OptionalPassObservation;
using ::PassHistory;
using ::PassObservation;
using ::PlayerState;
using ::StrategyMetadata;
using ::TurnDecisionReason;
using ::TurnDecisionSource;
using ::TurnDecisionTrace;
using ::TurnRecord;
using ::TurnSnapshot;

using ::TURN_REASON_CANNOT_BEAT;
using ::TURN_REASON_NORMAL_CHOICE;
using ::TURN_REASON_ONLY_LEGAL_MOVE;
using ::TURN_SOURCE_HUMAN;
using ::TURN_SOURCE_LOCAL_AI;
using ::TURN_SOURCE_SYSTEM;

using ::LOCAL_AI_BASIC;
using ::LOCAL_AI_STRONG;
using ::ExternalAiController_CanHandle;
using ::ExternalAiController_Cancel;
using ::ExternalAiController_Destroy;
using ::ExternalAiController_HasPending;
using ::ExternalAiController_MetadataFor;
using ::ExternalAiController_Start;
using ::ExternalAiController_TryGetResult;

using ::ExternalAiController;
using ::ExternalAiControllerVtbl;
using ::ExternalAiRequest;
using ::ExternalAiResult;
using ::LocalAiKind;
using ::LocalAiController;
using ::LocalAiController_Create;
using ::LocalAiController_Interface;
using ::LocalAiController_SetStrategy;
using ::PDK_AI_ERROR_CAP;
using ::PDK_AI_REASON_CAP;
using ::PDK_AI_SEATS;
using ::PDK_PASS_HISTORY_MAX;

using ::AiMoveChoice_MakePass;
using ::AiMoveChoice_MakePlay;
using ::OptionalPassObservation_Set;
using ::PassHistories_Copy;
using ::PassHistory_Add;
using ::PassHistory_Clear;
using ::PassObservations_Clear;
using ::PassObservations_Copy;

/* Test-facing shorthands for the two shapes that used to be plain assignment. */
inline void SetRemainingCards(AiContext& context, int player, int ai1, int ai2)
{
    context.remainingCards[0] = player;
    context.remainingCards[1] = ai1;
    context.remainingCards[2] = ai2;
}

inline void SetPassObservation(OptionalPassObservation& slot, const PassObservation& observation)
{
    slot.value = observation;
    slot.has = true;
}

/*
 * The old AiStrategy hierarchy, now that the C interface is a (vtable, user) pair.
 * These three classes exist only so the not-yet-ported C++ call sites and the
 * brute-force test strategy keep their shape; the real bodies are the free functions.
 */
class AiStrategyClass {
public:
    virtual ~AiStrategyClass() = default;
    virtual AiMoveChoice ChooseMove(const Cards& hand, const AiContext& context) = 0;
    virtual StrategyMetadata Metadata() const { return UnknownStrategyMetadata(); }
};

namespace detail {

inline AiMoveChoice AiStrategyClass_ChooseMove(void* user, const Cards* hand, const AiContext* context)
{
    return static_cast<AiStrategyClass*>(user)->ChooseMove(*hand, *context);
}

inline StrategyMetadata AiStrategyClass_Metadata(void* user)
{
    return static_cast<AiStrategyClass*>(user)->Metadata();
}

inline void AiStrategyClass_Destroy(void* user)
{
    delete static_cast<AiStrategyClass*>(user);
}

/* Borrows a C++ strategy as a C interface without taking ownership. */
inline ::AiStrategy Borrow(AiStrategyClass* strategy)
{
    static const AiStrategyVtbl vtbl = {
        AiStrategyClass_ChooseMove,
        AiStrategyClass_Metadata,
        NULL
    };
    ::AiStrategy borrowed;
    borrowed.vtbl = (strategy != nullptr) ? &vtbl : NULL;
    borrowed.user = strategy;
    return borrowed;
}

/* Hands a C++ strategy to the C side, which now owns it. */
inline ::AiStrategy Transfer(AiStrategyClass* strategy)
{
    static const AiStrategyVtbl vtbl = {
        AiStrategyClass_ChooseMove,
        AiStrategyClass_Metadata,
        AiStrategyClass_Destroy
    };
    ::AiStrategy owned;
    owned.vtbl = (strategy != nullptr) ? &vtbl : NULL;
    owned.user = strategy;
    return owned;
}

} // namespace detail

class BasicAiStrategy final : public AiStrategyClass {
public:
    AiMoveChoice ChooseMove(const Cards& hand, const AiContext& context) override
    {
        return BasicAiStrategy_ChooseMove(&hand, &context);
    }

    StrategyMetadata Metadata() const override { return BasicStrategyMetadata(); }
};

class StrongAiStrategy final : public AiStrategyClass {
public:
    AiMoveChoice ChooseMove(const Cards& hand, const AiContext& context) override
    {
        return StrongAiStrategy_ChooseMove(&hand, &context);
    }

    StrategyMetadata Metadata() const override { return StrongStrategyMetadata(); }
};

/*
 * The old ExternalAiController abstract class.  The test double still subclasses it, so
 * the facade bridges a C++ implementation onto the C vtable; Transfer hands ownership to
 * the C side, which is how GameState takes its controllers.
 */
class ExternalAiControllerClass {
public:
    virtual ~ExternalAiControllerClass() = default;

    virtual bool CanHandle(rules::PlayerId player) const = 0;
    virtual StrategyMetadata MetadataFor(rules::PlayerId player) const
    {
        return UnknownStrategyMetadata();
    }
    virtual bool HasPending() const = 0;
    virtual void Start(const ExternalAiRequest& request) = 0;
    /* Writes into `out` and returns true when a result is ready. */
    virtual bool TryGetResult(ExternalAiResult& out) = 0;
    virtual void Cancel() = 0;
};

namespace detail {

inline bool ExternalAiControllerClass_CanHandle(void* user, PlayerId player)
{
    return static_cast<ExternalAiControllerClass*>(user)->CanHandle(player);
}

inline StrategyMetadata ExternalAiControllerClass_MetadataFor(void* user, PlayerId player)
{
    return static_cast<ExternalAiControllerClass*>(user)->MetadataFor(player);
}

inline bool ExternalAiControllerClass_HasPending(void* user)
{
    return static_cast<ExternalAiControllerClass*>(user)->HasPending();
}

inline void ExternalAiControllerClass_Start(void* user, const ExternalAiRequest* request)
{
    static_cast<ExternalAiControllerClass*>(user)->Start(*request);
}

inline bool ExternalAiControllerClass_TryGetResult(void* user, ExternalAiResult* out)
{
    return static_cast<ExternalAiControllerClass*>(user)->TryGetResult(*out);
}

inline void ExternalAiControllerClass_Cancel(void* user)
{
    static_cast<ExternalAiControllerClass*>(user)->Cancel();
}

inline void ExternalAiControllerClass_Destroy(void* user)
{
    delete static_cast<ExternalAiControllerClass*>(user);
}

} // namespace detail

/* Takes ownership of the controller; GameState destroys it. */
inline ::ExternalAiController Transfer(ExternalAiControllerClass* controller)
{
    static const ExternalAiControllerVtbl vtbl = {
        detail::ExternalAiControllerClass_CanHandle,
        detail::ExternalAiControllerClass_MetadataFor,
        detail::ExternalAiControllerClass_HasPending,
        detail::ExternalAiControllerClass_Start,
        detail::ExternalAiControllerClass_TryGetResult,
        detail::ExternalAiControllerClass_Cancel,
        detail::ExternalAiControllerClass_Destroy
    };
    ::ExternalAiController owned;
    owned.vtbl = (controller != nullptr) ? &vtbl : NULL;
    owned.user = controller;
    return owned;
}

/* ---- GameState -------------------------------------------------------- */

/*
 * TEMPORARY C++ spelling of the ported state machine: src/game/GameState.h is pure C now,
 * and this facade wraps the C struct by value to keep the not-yet-converted callers
 * (tests/rules_tests and src/scenes/GameScene.cpp) compiling unchanged.  It must not grow
 * behaviour of its own, and it disappears with the last C++ file under src/.
 *
 * Three return types became small view types rather than the containers the old class
 * owned, because the C state stores fixed arrays and bitmasks now:
 *   - `std::set<int>` / `std::vector<int>` of hand indices -> IndexSetView over a uint64_t
 *     mask (a hand is at most CARDS_MAX cards);
 *   - `std::vector<TurnRecord>` / `std::vector<BombScoreEvent>` -> ConstSpan, which points
 *     straight at the C state's fixed array (the audit trail is ~640 KB, so it must not be
 *     copied per call);
 *   - `std::optional<HandPattern>` -> OptionalHandPatternView.
 * The text accessors keep returning a reference to a member cache so that the call sites
 * binding `const std::string&` keep working.
 */

/* The old scoped enum, in the C header's order; the asserts below stop the two drifting. */
enum class GameEventType {
    None = GAME_EVENT_NONE,
    RoundStarted = GAME_EVENT_ROUND_STARTED,
    CardsPlayed = GAME_EVENT_CARDS_PLAYED,
    Passed = GAME_EVENT_PASSED,
    InvalidMove = GAME_EVENT_INVALID_MOVE,
    Hint = GAME_EVENT_HINT,
    Bomb = GAME_EVENT_BOMB,
    RoundEnded = GAME_EVENT_ROUND_ENDED,
    Talk = GAME_EVENT_TALK
};

static_assert(static_cast<int>(GameEventType::None) == GAME_EVENT_NONE, "event kind drifted");
static_assert(static_cast<int>(GameEventType::RoundStarted) == GAME_EVENT_ROUND_STARTED,
              "event kind drifted");
static_assert(static_cast<int>(GameEventType::CardsPlayed) == GAME_EVENT_CARDS_PLAYED,
              "event kind drifted");
static_assert(static_cast<int>(GameEventType::Passed) == GAME_EVENT_PASSED,
              "event kind drifted");
static_assert(static_cast<int>(GameEventType::InvalidMove) == GAME_EVENT_INVALID_MOVE,
              "event kind drifted");
static_assert(static_cast<int>(GameEventType::Hint) == GAME_EVENT_HINT, "event kind drifted");
static_assert(static_cast<int>(GameEventType::Bomb) == GAME_EVENT_BOMB, "event kind drifted");
static_assert(static_cast<int>(GameEventType::RoundEnded) == GAME_EVENT_ROUND_ENDED,
              "event kind drifted");
static_assert(static_cast<int>(GameEventType::Talk) == GAME_EVENT_TALK, "event kind drifted");

/* The old event value: a std::string message, which the C side stores in a fixed buffer. */
struct GameEvent {
    GameEventType type{GameEventType::None};
    rules::PlayerId player{PLAYER_HUMAN};
    std::string message;
    rules::Cards cards;
};

inline std::uint64_t HandIndexBit(int index)
{
    return (index >= 0 && index < 64) ? (static_cast<std::uint64_t>(1) << index)
                                      : static_cast<std::uint64_t>(0);
}

/* Stands in for the `std::set<int>` / `std::vector<int>` of hand indices. */
class IndexSetView {
public:
    explicit IndexSetView(std::uint64_t mask = 0) : mask_(mask) {}

    bool contains(int index) const { return (mask_ & HandIndexBit(index)) != 0; }
    bool empty() const { return mask_ == 0; }
    int size() const
    {
        int count = 0;
        std::uint64_t bits = mask_;

        while (bits != 0) {
            bits &= bits - 1;
            ++count;
        }
        return count;
    }
    std::uint64_t mask() const { return mask_; }

private:
    std::uint64_t mask_;
};

/* Stands in for `const std::vector<T>&` without copying: the C state owns the array. */
template <typename T>
class ConstSpan {
public:
    ConstSpan() : items_(NULL), count_(0) {}
    ConstSpan(const T* items, int count) : items_(items), count_(count) {}

    int size() const { return count_; }
    bool empty() const { return count_ == 0; }
    const T& operator[](int index) const { return items_[index]; }
    const T& front() const { return items_[0]; }
    const T& back() const { return items_[count_ - 1]; }

private:
    const T* items_;
    int count_;
};

/* Stands in for `const std::optional<HandPattern>&`. */
class OptionalHandPatternView {
public:
    explicit OptionalHandPatternView(const rules::HandPattern* value = NULL) : value_(value) {}

    bool has_value() const { return value_ != NULL; }
    explicit operator bool() const { return value_ != NULL; }
    const rules::HandPattern& operator*() const { return *value_; }
    const rules::HandPattern* operator->() const { return value_; }
    const rules::HandPattern& value() const { return *value_; }

private:
    const rules::HandPattern* value_;
};

class GameState {
public:
    GameState() { ::GameState_Init(&data_); }
    ~GameState() { ::GameState_Destroy(&data_); }
    /* The C state owns the audit buffers and the external controllers, so copying it would
     * double-free them; the old class was never copied either. */
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    void StartNewRound(const std::string& playerName, unsigned seed = 0)
    {
        ::GameState_StartNewRound(&data_, playerName.c_str(), seed);
    }
    void Update(float dt) { ::GameState_Update(&data_, dt); }

    bool IsRoundOver() const { return ::GameState_IsRoundOver(&data_); }
    bool IsHumanTurn() const { return ::GameState_IsHumanTurn(&data_); }
    rules::PlayerId CurrentPlayer() const { return ::GameState_CurrentPlayer(&data_); }
    const PlayerState* Players() const { return ::GameState_Players(&data_); }
    const rules::Cards& LastCards() const { return *::GameState_LastCards(&data_); }
    OptionalHandPatternView LastPattern() const
    {
        return OptionalHandPatternView(::GameState_LastPattern(&data_));
    }
    rules::PlayerId LastMovePlayer() const { return ::GameState_LastMovePlayer(&data_); }
    const rules::Cards& PlayedCards() const { return *::GameState_PlayedCards(&data_); }
    const OptionalPassObservation* PassObservations() const
    {
        return ::GameState_PassObservations(&data_);
    }
    IndexSetView SelectedIndices() const
    {
        return IndexSetView(::GameState_SelectedMask(&data_));
    }
    std::vector<int> HintIndices() const
    {
        const std::uint64_t mask = ::GameState_HintMask(&data_);
        std::vector<int> indices;

        for (int index = 0; index < CARDS_MAX; ++index) {
            if ((mask & HandIndexBit(index)) != 0) {
                indices.push_back(index);
            }
        }
        return indices;
    }
    const std::vector<GameEvent>& Events() const
    {
        const int count = ::GameState_EventCount(&data_);

        eventCache_.clear();
        for (int i = 0; i < count; ++i) {
            const ::GameEvent* raw = ::GameState_EventAt(&data_, i);
            GameEvent event;

            event.type = static_cast<GameEventType>(raw->type);
            event.player = raw->player;
            event.message = raw->message;
            event.cards = raw->cards;
            eventCache_.push_back(event);
        }
        return eventCache_;
    }
    const std::string& Toast() const
    {
        toastCache_ = ::GameState_Toast(&data_);
        return toastCache_;
    }
    const std::string& TalkText() const
    {
        talkTextCache_ = ::GameState_TalkText(&data_);
        return talkTextCache_;
    }
    rules::PlayerId TalkPlayer() const { return ::GameState_TalkPlayer(&data_); }
    bool Autoplay() const { return ::GameState_Autoplay(&data_); }
    const stats::RoundRecord& LastRoundRecord() const
    {
        lastRoundRecordCache_ = stats::FromCRound(*::GameState_LastRoundRecord(&data_));
        return lastRoundRecordCache_;
    }
    ConstSpan<rules::BombScoreEvent> BombEvents() const
    {
        return ConstSpan<rules::BombScoreEvent>(::GameState_BombEvents(&data_),
                                                ::GameState_BombEventCount(&data_));
    }
    ConstSpan<TurnRecord> TurnRecords() const
    {
        return ConstSpan<TurnRecord>(::GameState_TurnRecords(&data_),
                                     ::GameState_TurnRecordCount(&data_));
    }
    bool ExternalAiPending() const { return ::GameState_ExternalAiPending(&data_); }
    bool CanCurrentPlayerPass() const { return ::GameState_CanCurrentPlayerPass(&data_); }
    bool IsInLeadState() const { return ::GameState_IsInLeadState(&data_); }

    void ClearEvents() { ::GameState_ClearEvents(&data_); }
    void ToggleAutoplay() { ::GameState_ToggleAutoplay(&data_); }
    void TogglePlayerCard(int handIndex) { ::GameState_TogglePlayerCard(&data_, handIndex); }
    void ClearSelection() { ::GameState_ClearSelection(&data_); }
    void SortHands() { ::GameState_SortHands(&data_); }
    bool PlaySelected() { return ::GameState_PlaySelected(&data_); }
    bool PassHuman() { return ::GameState_PassHuman(&data_); }
    bool ApplyHint() { return ::GameState_ApplyHint(&data_); }
    bool SelectByHoverPattern(int handIndex)
    {
        return ::GameState_SelectByHoverPattern(&data_, handIndex);
    }
    bool SelectBestPatternFromDraggedCards(const std::vector<int>& handIndices)
    {
        return ::GameState_SelectBestPatternFromDraggedCards(
            &data_, handIndices.data(), static_cast<int>(handIndices.size()));
    }
    void SetExternalAiController(ExternalAiController controller)
    {
        ::GameState_SetExternalAiController(&data_, controller);
    }
    void SetExternalAiControllers(const ExternalAiController* controllers, int count)
    {
        ::GameState_SetExternalAiControllers(&data_, controllers, count);
    }
    void SetLocalAiStrategy(rules::PlayerId player, AiStrategy strategy, bool takeOwnership)
    {
        ::GameState_SetLocalAiStrategy(&data_, player, strategy, takeOwnership);
    }
    void SetRoundTraceEnabled(bool enabled)
    {
        ::GameState_SetRoundTraceEnabled(&data_, enabled);
    }
    void SetRoundTraceRoot(std::string root)
    {
        ::GameState_SetRoundTraceRoot(&data_, root.c_str());
    }
    const std::string& LastRoundTracePath() const
    {
        lastRoundTracePathCache_ = ::GameState_LastRoundTracePath(&data_);
        return lastRoundTracePathCache_;
    }

    void TestSetRound(const std::array<rules::Cards, 3>& hands,
                      rules::PlayerId currentPlayer,
                      const std::optional<rules::HandPattern>& previousPattern,
                      rules::PlayerId lastMovePlayer)
    {
        ::GameState_TestSetRound(&data_, hands.data(), currentPlayer,
                                 previousPattern.has_value() ? &*previousPattern : NULL,
                                 lastMovePlayer);
    }

private:
    ::GameState data_;
    /* Rebuilt by the accessors that have to return a reference to a computed value. */
    mutable std::vector<GameEvent> eventCache_;
    mutable std::string toastCache_;
    mutable std::string talkTextCache_;
    mutable stats::RoundRecord lastRoundRecordCache_;
    mutable std::string lastRoundTracePathCache_;
};

} // namespace pdk::game

