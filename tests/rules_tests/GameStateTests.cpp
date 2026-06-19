#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "TestWeakAiStrategy.h"
#include "game/GameState.h"

#include <cstdlib>
#include <memory>

using namespace pdk;
using tests::C;
using tests::CountRank;

namespace {

std::vector<std::string> TalkMessages(const game::GameState& state) {
    std::vector<std::string> messages;
    for (const game::GameEvent& event : state.Events()) {
        if (event.type == game::GameEventType::Talk && event.player != rules::PlayerId::Player) {
            messages.push_back(event.message);
        }
    }
    return messages;
}

bool HasTalkContaining(const game::GameState& state, const std::string& text) {
    for (const std::string& message : TalkMessages(state)) {
        if (message.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool HasCard(const rules::Cards& cards, rules::Card card) {
    return std::find_if(cards.begin(), cards.end(), [card](rules::Card owned) {
        return owned.rank == card.rank && owned.suit == card.suit;
    }) != cards.end();
}

unsigned SeedWhereFirstLeaderIsNot(rules::PlayerId player) {
    for (unsigned seed = 1; seed < 300; ++seed) {
        game::GameState state;
        state.StartNewRound("Tester", seed);
        if (state.CurrentPlayer() != player) {
            return seed;
        }
    }
    return 0;
}

bool RunAiFairnessTest() {
    const char* value = std::getenv("PDK_RUN_AI_FAIRNESS_TEST");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

std::array<int, 3> RunAutoplayRounds(game::GameState& state, unsigned seedBase, int roundCount) {
    constexpr int maxUpdatesPerRound = 500;
    std::array<int, 3> wins{0, 0, 0};
    state.ToggleAutoplay();

    for (int round = 0; round < roundCount; ++round) {
        state.StartNewRound("Tester", seedBase + static_cast<unsigned>(round));

        for (int update = 0; update < maxUpdatesPerRound && !state.IsRoundOver(); ++update) {
            state.Update(1.0f);
            state.ClearEvents();
        }

        REQUIRE(state.IsRoundOver());
        ++wins[rules::PlayerIndex(state.LastRoundRecord().winner)];
    }

    return wins;
}

class MockExternalAiController final : public game::ExternalAiController {
public:
    explicit MockExternalAiController(
        game::ExternalAiResult result,
        rules::PlayerId handledPlayer = rules::PlayerId::Ai1)
        : result_(std::move(result)), handledPlayer_(handledPlayer) {}

    bool CanHandle(rules::PlayerId player) const override {
        return player == handledPlayer_;
    }

    bool HasPending() const override {
        return pending_;
    }

    void Start(game::ExternalAiRequest request) override {
        startCount++;
        lastRequest = std::move(request);
        pending_ = true;
    }

    std::optional<game::ExternalAiResult> TryGetResult() override {
        if (!pending_) {
            return std::nullopt;
        }
        pending_ = false;
        return result_;
    }

    void Cancel() override {
        pending_ = false;
    }

    int startCount{0};
    std::optional<game::ExternalAiRequest> lastRequest;

private:
    game::ExternalAiResult result_;
    rules::PlayerId handledPlayer_{rules::PlayerId::Ai1};
    bool pending_{false};
};

} // namespace

TEST_CASE("game state can finish a full three player autoplay round") {
    game::GameState state;
    state.StartNewRound("Tester", 20260606u);
    state.ToggleAutoplay();

    for (int i = 0; i < 600 && !state.IsRoundOver(); ++i) {
        state.Update(0.5f);
        state.ClearEvents();
    }

    CHECK(state.IsRoundOver());
    const stats::RoundRecord& record = state.LastRoundRecord();
    CHECK((record.winner == rules::PlayerId::Player ||
           record.winner == rules::PlayerId::Ai1 ||
           record.winner == rules::PlayerId::Ai2));
    CHECK(record.remainingCards[rules::PlayerIndex(record.winner)] == 0);
    CHECK(record.scores[0] + record.scores[1] + record.scores[2] == 0);
}

TEST_CASE("disabled ai fairness smoke over 100 all-basic autoplay rounds" * doctest::skip(!RunAiFairnessTest())) {
    constexpr int roundCount = 100;
    constexpr int minExpectedWins = 20;
    constexpr int maxExpectedWins = 47;

    game::GameState state;
    const std::array<int, 3> wins = RunAutoplayRounds(state, 20260619u, roundCount);

    INFO("All-basic wins: Player=" << wins[0] << ", AI1=" << wins[1] << ", AI2=" << wins[2]);
    CHECK(wins[0] + wins[1] + wins[2] == roundCount);
    CHECK(wins[0] >= minExpectedWins);
    CHECK(wins[0] <= maxExpectedWins);
    CHECK(wins[1] >= minExpectedWins);
    CHECK(wins[1] <= maxExpectedWins);
    CHECK(wins[2] >= minExpectedWins);
    CHECK(wins[2] <= maxExpectedWins);
}

TEST_CASE("disabled ai fairness smoke confirms injected weak AI2 wins less" * doctest::skip(!RunAiFairnessTest())) {
    constexpr int roundCount = 100;
    constexpr int maxWeakAiWins = 24;

    game::GameState basicState;
    const std::array<int, 3> basicWins = RunAutoplayRounds(basicState, 20260619u, roundCount);

    game::GameState weakState;
    weakState.SetLocalAiStrategy(rules::PlayerId::Ai2, std::make_unique<tests::TestWeakAiStrategy>());
    const std::array<int, 3> weakWins = RunAutoplayRounds(weakState, 20260619u, roundCount);

    INFO("All-basic wins: Player=" << basicWins[0] << ", AI1=" << basicWins[1] << ", AI2=" << basicWins[2]
        << "; weak-AI2 wins: Player=" << weakWins[0] << ", AI1=" << weakWins[1] << ", AI2=" << weakWins[2]);
    CHECK(basicWins[0] + basicWins[1] + basicWins[2] == roundCount);
    CHECK(weakWins[0] + weakWins[1] + weakWins[2] == roundCount);
    CHECK(weakWins[2] < basicWins[2]);
    CHECK(weakWins[2] <= maxWeakAiWins);
    CHECK(weakWins[2] < weakWins[0]);
    CHECK(weakWins[2] < weakWins[1]);
}

TEST_CASE("first round starts from the spade three holder") {
    game::GameState state;
    state.StartNewRound("Tester", 20260606u);

    const rules::PlayerId starter = state.CurrentPlayer();
    CHECK(HasCard(state.Players()[rules::PlayerIndex(starter)].hand, C(rules::Rank::Three, rules::Suit::Spades)));
    REQUIRE_FALSE(state.Events().empty());
    CHECK(state.Events().front().type == game::GameEventType::RoundStarted);
    CHECK(state.Events().front().player == starter);
    CHECK(state.Events().front().message == "黑桃 3 玩家先出");
}

TEST_CASE("next round starts from previous winner and later winner overrides it") {
    const unsigned nonPlayerSeed = SeedWhereFirstLeaderIsNot(rules::PlayerId::Player);
    const unsigned nonAi1Seed = SeedWhereFirstLeaderIsNot(rules::PlayerId::Ai1);
    REQUIRE(nonPlayerSeed != 0);
    REQUIRE(nonAi1Seed != 0);

    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Four)},
            rules::Cards{C(rules::Rank::Five)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Player);

    state.TogglePlayerCard(0);
    REQUIRE(state.PlaySelected());
    REQUIRE(state.IsRoundOver());
    CHECK(state.LastRoundRecord().winner == rules::PlayerId::Player);

    state.StartNewRound("Tester", nonPlayerSeed);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Player);
    REQUIRE_FALSE(state.Events().empty());
    CHECK(state.Events().back().type == game::GameEventType::RoundStarted);
    CHECK(state.Events().back().message == "上局赢家先出");

    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Four)},
            rules::Cards{C(rules::Rank::Five)}
        },
        rules::PlayerId::Ai1,
        std::nullopt,
        rules::PlayerId::Ai1);

    state.Update(1.0f);
    REQUIRE(state.IsRoundOver());
    CHECK(state.LastRoundRecord().winner == rules::PlayerId::Ai1);

    state.StartNewRound("Tester", nonAi1Seed);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Ai1);
    REQUIRE_FALSE(state.Events().empty());
    CHECK(state.Events().back().type == game::GameEventType::RoundStarted);
    CHECK(state.Events().back().message == "上局赢家先出");
}

TEST_CASE("turn order is counterclockwise so the left-hand player is upstream") {
    game::GameState state;
    bool foundAi2Start = false;
    for (unsigned seed = 1; seed < 300 && !foundAi2Start; ++seed) {
        state.StartNewRound("Tester", seed);
        if (state.CurrentPlayer() == rules::PlayerId::Ai2) {
            foundAi2Start = true;
        }
    }
    REQUIRE(foundAi2Start);

    state.Update(0.2f);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Ai2);
    state.Update(1.0f);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Ai1);
}

TEST_CASE("AI1 can use external LLM controller and records the decision") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<MockExternalAiController>(game::ExternalAiResult{
        true,
        game::GameAction{"play", {"5"}, "我先压一张。"},
        "选择最小单张 5 压过 4。",
        "call_test",
        "play_cards",
        "{\"ranks\":[\"5\"],\"talk\":\"我先压一张。\"}",
        {},
        {},
        {}
    });

    game::GameState state;
    state.SetExternalAiController(controller);
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Five), C(rules::Rank::Six)},
            rules::Cards{C(rules::Rank::Seven)}
        },
        rules::PlayerId::Ai1,
        fourLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    CHECK(state.ExternalAiPending());
    REQUIRE(controller->startCount == 1);
    REQUIRE(controller->lastRequest.has_value());
    CHECK(controller->lastRequest->history.empty());

    state.Update(0.1f);
    CHECK_FALSE(state.ExternalAiPending());
    REQUIRE(state.TurnRecords().size() == 1);
    const game::TurnRecord& record = state.TurnRecords().back();
    CHECK(record.actor == rules::PlayerId::Ai1);
    CHECK(record.source == game::TurnDecisionSource::LlmAi);
    CHECK(record.accepted);
    CHECK(record.finalAction.ranks == std::vector<std::string>{"5"});
    CHECK(state.LastCards().front().rank == rules::Rank::Five);
}

TEST_CASE("AI1 only legal move is recorded without calling external LLM") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<MockExternalAiController>(game::ExternalAiResult{});

    game::GameState state;
    state.SetExternalAiController(controller);
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Five)},
            rules::Cards{C(rules::Rank::Seven)}
        },
        rules::PlayerId::Ai1,
        fourLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    CHECK(controller->startCount == 0);
    REQUIRE(state.TurnRecords().size() == 1);
    CHECK(state.TurnRecords().back().source == game::TurnDecisionSource::LocalAi);
    CHECK(state.TurnRecords().back().reason == game::TurnDecisionReason::OnlyLegalMove);
    CHECK_FALSE(state.TurnRecords().back().trace.toolCallId.empty());
    CHECK(state.TurnRecords().back().trace.toolName == "record_forced_move");
}

TEST_CASE("local AI2 actions are recorded but not external controlled") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<MockExternalAiController>(game::ExternalAiResult{});

    game::GameState state;
    state.SetExternalAiController(controller);
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Six)},
            rules::Cards{C(rules::Rank::Five)}
        },
        rules::PlayerId::Ai2,
        fourLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    CHECK(controller->startCount == 0);
    REQUIRE(state.TurnRecords().size() == 1);
    CHECK(state.TurnRecords().back().actor == rules::PlayerId::Ai2);
    CHECK(state.TurnRecords().back().source == game::TurnDecisionSource::LocalAi);
    CHECK(state.TurnRecords().back().trace.toolCallId.empty());
}

TEST_CASE("AI2 forced move is recorded when AI2 is external controlled") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<MockExternalAiController>(
        game::ExternalAiResult{},
        rules::PlayerId::Ai2);

    game::GameState state;
    state.SetExternalAiController(controller);
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Six)},
            rules::Cards{C(rules::Rank::Five)}
        },
        rules::PlayerId::Ai2,
        fourLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    CHECK(controller->startCount == 0);
    REQUIRE(state.TurnRecords().size() == 1);
    const game::TurnRecord& record = state.TurnRecords().back();
    CHECK(record.actor == rules::PlayerId::Ai2);
    CHECK(record.reason == game::TurnDecisionReason::OnlyLegalMove);
    CHECK(record.trace.toolName == "record_forced_move");
    CHECK_FALSE(record.trace.toolCallId.empty());
}

TEST_CASE("AI2 can use external LLM controller and records the decision") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<MockExternalAiController>(
        game::ExternalAiResult{
            true,
            game::GameAction{"play", {"5"}, "我来接一下。"},
            "选择最小单张 5 压过 4。",
            "call_ai2",
            "play_cards",
            "{\"ranks\":[\"5\"],\"talk\":\"我来接一下。\"}",
            {},
            {},
            {}
        },
        rules::PlayerId::Ai2);

    game::GameState state;
    state.SetExternalAiController(controller);
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Seven)},
            rules::Cards{C(rules::Rank::Five), C(rules::Rank::Six)}
        },
        rules::PlayerId::Ai2,
        fourLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    CHECK(state.ExternalAiPending());
    REQUIRE(controller->startCount == 1);
    REQUIRE(controller->lastRequest.has_value());
    CHECK(controller->lastRequest->player == rules::PlayerId::Ai2);

    state.Update(0.1f);
    CHECK_FALSE(state.ExternalAiPending());
    REQUIRE(state.TurnRecords().size() == 1);
    const game::TurnRecord& record = state.TurnRecords().back();
    CHECK(record.actor == rules::PlayerId::Ai2);
    CHECK(record.source == game::TurnDecisionSource::LlmAi);
    CHECK(record.accepted);
    CHECK(record.trace.toolName == "play_cards");
    CHECK(record.finalAction.ranks == std::vector<std::string>{"5"});
}

TEST_CASE("invalid LLM decision falls back to local AI silently") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<MockExternalAiController>(game::ExternalAiResult{
        true,
        game::GameAction{"pass", {}, {}},
        "错误地选择不要。",
        "call_bad",
        "play_cards",
        "{\"action\":\"pass\",\"ranks\":[]}",
        {},
        {},
        {}
    });

    game::GameState state;
    state.SetExternalAiController(controller);
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Five), C(rules::Rank::Six)},
            rules::Cards{C(rules::Rank::Seven)}
        },
        rules::PlayerId::Ai1,
        fourLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    state.Update(0.1f);
    REQUIRE(state.TurnRecords().size() == 1);
    const game::TurnRecord& record = state.TurnRecords().back();
    CHECK(record.source == game::TurnDecisionSource::LlmAi);
    CHECK(record.reason == game::TurnDecisionReason::LlmFallback);
    CHECK_FALSE(record.accepted);
    CHECK(record.finalAction.action == "play");
    CHECK_FALSE(state.LastCards().empty());
}

TEST_CASE("hint passes directly when player cannot beat and pass is blocked when player can beat") {
    const auto aceLead = rules::IdentifyPattern({C(rules::Rank::Ace)}).pattern;
    game::GameState cannotBeat;
    cannotBeat.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::King)},
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Four)}
        },
        rules::PlayerId::Player,
        aceLead,
        rules::PlayerId::Ai1);

    CHECK(cannotBeat.CanCurrentPlayerPass());
    CHECK(cannotBeat.ApplyHint());
    REQUIRE_FALSE(cannotBeat.Events().empty());
    CHECK(cannotBeat.Events().back().type == game::GameEventType::Passed);
    CHECK(cannotBeat.CurrentPlayer() == rules::PlayerId::Ai2);

    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    game::GameState canBeat;
    canBeat.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Five)},
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Six)}
        },
        rules::PlayerId::Player,
        fourLead,
        rules::PlayerId::Ai1);

    CHECK_FALSE(canBeat.CanCurrentPlayerPass());
    CHECK_FALSE(canBeat.PassHuman());
    REQUIRE_FALSE(canBeat.Events().empty());
    CHECK(canBeat.Events().back().type == game::GameEventType::InvalidMove);
}

TEST_CASE("hint switches to recommendation or toggles it off when already selected") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three), C(rules::Rank::Five)},
            rules::Cards{C(rules::Rank::Six)},
            rules::Cards{C(rules::Rank::Seven)}
        },
        rules::PlayerId::Player,
        fourLead,
        rules::PlayerId::Ai1);

    state.TogglePlayerCard(0);
    REQUIRE(state.SelectedIndices().contains(0));

    REQUIRE(state.ApplyHint());
    CHECK_FALSE(state.SelectedIndices().contains(0));
    CHECK(state.SelectedIndices().contains(1));
    CHECK(state.HintIndices().size() == 1);
    CHECK(state.HintIndices()[0] == 1);

    REQUIRE(state.ApplyHint());
    CHECK(state.SelectedIndices().empty());
    CHECK(state.HintIndices().empty());
}

TEST_CASE("hint follows triple with two using five cards") {
    const auto previous = rules::IdentifyPattern({
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Seven),
        C(rules::Rank::Eight)
    }).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Four),
                C(rules::Rank::Four, rules::Suit::Hearts),
                C(rules::Rank::Four, rules::Suit::Diamonds),
                C(rules::Rank::Five),
                C(rules::Rank::Six)
            },
            rules::Cards{C(rules::Rank::Eight)},
            rules::Cards{C(rules::Rank::Nine)}
        },
        rules::PlayerId::Player,
        previous,
        rules::PlayerId::Ai1);

    REQUIRE(state.ApplyHint());
    CHECK(state.SelectedIndices().size() == 5);
    REQUIRE_FALSE(state.Events().empty());
    const game::GameEvent& event = state.Events().back();
    CHECK(event.type == game::GameEventType::Hint);
    CHECK(event.cards.size() == 5);
    CHECK(CountRank(event.cards, rules::Rank::Four) == 3);
}

TEST_CASE("upstream AI follows with high singleton when player has reported single") {
    const auto previous = rules::IdentifyPattern({C(rules::Rank::Seven)}).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Eight), C(rules::Rank::King)},
            rules::Cards{C(rules::Rank::Four)}
        },
        rules::PlayerId::Ai1,
        previous,
        rules::PlayerId::Ai2);

    state.Update(1.0f);

    REQUIRE(state.LastCards().size() == 1);
    CHECK(state.LastCards().front().rank == rules::Rank::King);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Player);
}

TEST_CASE("game state tracks played cards and clears observations for a fresh test round") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three), C(rules::Rank::Four)},
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Player);

    state.TogglePlayerCard(0);
    REQUIRE(state.PlaySelected());
    REQUIRE(state.PlayedCards().size() == 1);
    CHECK(state.PlayedCards().front().rank == rules::Rank::Three);

    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Player);

    CHECK(state.PlayedCards().empty());
    for (const auto& observation : state.PassObservations()) {
        CHECK_FALSE(observation.has_value());
    }
}

TEST_CASE("game state records pass observations from mandatory-play rule") {
    const auto queenLead = rules::IdentifyPattern({C(rules::Rank::Queen)}).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Jack)},
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Four)}
        },
        rules::PlayerId::Player,
        queenLead,
        rules::PlayerId::Ai1);

    REQUIRE(state.PassHuman());
    const auto& observation = state.PassObservations()[rules::PlayerIndex(rules::PlayerId::Player)];
    REQUIRE(observation.has_value());
    CHECK(observation->pattern.type == rules::PatternType::Single);
    CHECK(observation->pattern.mainRank == rules::Rank::Queen);
    CHECK(observation->remainingCards == 1);
}

TEST_CASE("ai pass observations survive the relaunch lead") {
    const auto queenLead = rules::IdentifyPattern({C(rules::Rank::Queen)}).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::Jack)},
            rules::Cards{C(rules::Rank::Ten)}
        },
        rules::PlayerId::Ai2,
        queenLead,
        rules::PlayerId::Player);

    state.Update(1.0f);
    state.Update(1.0f);

    CHECK(state.CurrentPlayer() == rules::PlayerId::Player);
    CHECK_FALSE(state.LastPattern().has_value());
    const auto& ai1Observation = state.PassObservations()[rules::PlayerIndex(rules::PlayerId::Ai1)];
    const auto& ai2Observation = state.PassObservations()[rules::PlayerIndex(rules::PlayerId::Ai2)];
    REQUIRE(ai1Observation.has_value());
    REQUIRE(ai2Observation.has_value());
    CHECK(ai1Observation->pattern.mainRank == rules::Rank::Queen);
    CHECK(ai2Observation->pattern.mainRank == rules::Rank::Queen);
}

TEST_CASE("ai bomb talk ignores existing normal talk cooldown") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five), C(rules::Rank::Seven), C(rules::Rank::Nine)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Ai1,
        std::nullopt,
        rules::PlayerId::Ai1);
    state.Update(1.0f);
    REQUIRE_FALSE(TalkMessages(state).empty());

    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Three, rules::Suit::Hearts),
                C(rules::Rank::Three, rules::Suit::Diamonds),
                C(rules::Rank::Three, rules::Suit::Clubs)
            },
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Ai1,
        std::nullopt,
        rules::PlayerId::Ai1);
    state.Update(1.0f);

    CHECK(HasTalkContaining(state, "炸"));
}

TEST_CASE("ai talks when forced to break a good group") {
    const auto previous = rules::IdentifyPattern({C(rules::Rank::Ace)}).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Two), C(rules::Rank::Two, rules::Suit::Hearts)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Ai1,
        previous,
        rules::PlayerId::Player);

    state.Update(1.0f);

    CHECK((HasTalkContaining(state, "拆") || HasTalkContaining(state, "心疼") || HasTalkContaining(state, "好牌")));
}

TEST_CASE("ai reacts when it cannot beat a seven card move") {
    const auto previous = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five), C(rules::Rank::Six),
        C(rules::Rank::Seven), C(rules::Rank::Eight), C(rules::Rank::Nine)
    }).pattern;
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::King)},
            rules::Cards{C(rules::Rank::Ace)}
        },
        rules::PlayerId::Ai1,
        previous,
        rules::PlayerId::Player);

    state.Update(1.0f);

    CHECK((HasTalkContaining(state, "一串") || HasTalkContaining(state, "顶") || HasTalkContaining(state, "接不住")));
}

TEST_CASE("ai taunts when it plays seven or more cards") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Two)},
            rules::Cards{
                C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five), C(rules::Rank::Six),
                C(rules::Rank::Seven), C(rules::Rank::Eight), C(rules::Rank::Nine)
            },
            rules::Cards{C(rules::Rank::Ace)}
        },
        rules::PlayerId::Ai1,
        std::nullopt,
        rules::PlayerId::Ai1);

    state.Update(1.0f);

    CHECK((HasTalkContaining(state, "一大把") || HasTalkContaining(state, "帅") || HasTalkContaining(state, "清爽")));
}

TEST_CASE("ai comments on human good moves and avoids immediate repeated wording") {
    game::GameState state;
    auto dealHumanStraight = [&]() {
        state.TestSetRound(
            std::array<rules::Cards, 3>{
                rules::Cards{
                    C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five), C(rules::Rank::Six),
                    C(rules::Rank::Seven), C(rules::Rank::Eight), C(rules::Rank::Nine)
                },
                rules::Cards{C(rules::Rank::Ace)},
                rules::Cards{C(rules::Rank::King)}
            },
            rules::PlayerId::Player,
            std::nullopt,
            rules::PlayerId::Player);
        for (int i = 0; i < 7; ++i) {
            state.TogglePlayerCard(i);
        }
        REQUIRE(state.PlaySelected());
        const auto messages = TalkMessages(state);
        REQUIRE_FALSE(messages.empty());
        return messages.front();
    };

    const std::string first = dealHumanStraight();
    const std::string second = dealHumanStraight();

    CHECK((first.find("顺子") != std::string::npos || first.find("一串") != std::string::npos));
    CHECK((second.find("顺子") != std::string::npos || second.find("一串") != std::string::npos));
    CHECK(first != second);
}

TEST_CASE("round end talk prefers leftover plane") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{
                C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
                C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts), C(rules::Rank::Five, rules::Suit::Diamonds)
            },
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Player);
    state.TogglePlayerCard(0);
    REQUIRE(state.PlaySelected());

    CHECK(HasTalkContaining(state, "飞机"));
}
