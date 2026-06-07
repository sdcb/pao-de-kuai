#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/GameState.h"

using namespace pdk;
using tests::C;

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
