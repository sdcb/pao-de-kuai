#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/GameState.h"

using namespace pdk;
using tests::C;

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
