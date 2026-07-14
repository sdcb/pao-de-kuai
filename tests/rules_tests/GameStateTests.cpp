#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/AiStrategy.h"
#include "game/GameState.h"
#include "game/LocalAiController.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

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

int AiFairnessRoundCount() {
    const char* value = std::getenv("PDK_AI_FAIRNESS_ROUNDS");
    if (value == nullptr || value[0] == '\0') {
        return 1000;
    }
    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : 1000;
}

bool RunContinuousFairnessRounds() {
    const char* value = std::getenv("PDK_AI_FAIRNESS_CONTINUOUS");
    return value == nullptr || value[0] == '\0' || value[0] != '0';
}

bool RunStrongFairnessStrategy() {
    const char* value = std::getenv("PDK_AI_FAIRNESS_STRATEGY");
    return value == nullptr || std::string(value) != "basic";
}

bool RunAiCompareDiagnostics() {
    const char* value = std::getenv("PDK_AI_COMPARE_BASIC");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

bool RunStrongBaselineDiagnostics() {
    const char* value = std::getenv("PDK_AI_STRONG_BASELINE_DIAGNOSTICS");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

int StrongTimingRoundCount() {
    const char* value = std::getenv("PDK_AI_STRONG_TIMING_ROUNDS");
    if (value == nullptr || value[0] == '\0') {
        return 30;
    }
    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : 30;
}

bool ListAi1LeaderLosses() {
    const char* value = std::getenv("PDK_AI_LIST_A1_LEADER_LOSSES");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

std::optional<rules::PlayerId> ListLeaderLossesFor() {
    const char* value = std::getenv("PDK_AI_LIST_LEADER_LOSSES");
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    const std::string name(value);
    if (name == "P" || name == "Player") {
        return rules::PlayerId::Player;
    }
    if (name == "A1" || name == "Ai1") {
        return rules::PlayerId::Ai1;
    }
    if (name == "A2" || name == "Ai2") {
        return rules::PlayerId::Ai2;
    }
    return std::nullopt;
}

int ListAi1LeaderLossesFromRound() {
    const char* value = std::getenv("PDK_AI_LIST_A1_LEADER_LOSSES_FROM");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    const int parsed = std::atoi(value);
    return parsed > 0 ? parsed : 0;
}

unsigned AiCompareTraceSeed() {
    const char* value = std::getenv("PDK_AI_COMPARE_TRACE_SEED");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    const int parsed = std::atoi(value);
    return parsed > 0 ? static_cast<unsigned>(parsed) : 0u;
}

unsigned AiCompareSeedBase() {
    const char* value = std::getenv("PDK_AI_COMPARE_SEED_BASE");
    if (value == nullptr || value[0] == '\0') {
        return 20260619u;
    }
    const int parsed = std::atoi(value);
    return parsed > 0 ? static_cast<unsigned>(parsed) : 20260619u;
}

std::optional<rules::PlayerId> AiCompareForcedLeader() {
    const char* value = std::getenv("PDK_AI_COMPARE_FORCE_LEADER");
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    const std::string name(value);
    if (name == "P" || name == "Player") {
        return rules::PlayerId::Player;
    }
    if (name == "A1" || name == "Ai1") {
        return rules::PlayerId::Ai1;
    }
    if (name == "A2" || name == "Ai2") {
        return rules::PlayerId::Ai2;
    }
    return std::nullopt;
}

int AiFairnessTraceRound() {
    const char* value = std::getenv("PDK_AI_FAIRNESS_TRACE_ROUND");
    if (value == nullptr || value[0] == '\0') {
        return -1;
    }
    const int parsed = std::atoi(value);
    return parsed >= 0 ? parsed : -1;
}

rules::PlayerId NextSimPlayer(rules::PlayerId player) {
    return rules::PlayerFromIndex((rules::PlayerIndex(player) + 2) % 3);
}

void RecordSimPassObservation(
    std::array<std::optional<game::PassObservation>, 3>& observations,
    std::array<std::vector<game::PassObservation>, 3>& history,
    rules::PlayerId player,
    const rules::HandPattern& pattern,
    int remainingCards) {
    std::optional<game::PassObservation>& existing = observations[static_cast<std::size_t>(rules::PlayerIndex(player))];
    game::PassObservation observation{pattern, remainingCards};
    history[static_cast<std::size_t>(rules::PlayerIndex(player))].push_back(observation);

    if (existing && existing->pattern.type == rules::PatternType::Single && pattern.type == rules::PatternType::Single) {
        if (rules::RankValue(pattern.mainRank) < rules::RankValue(existing->pattern.mainRank)) {
            existing = observation;
        }
        return;
    }
    if (existing && existing->pattern.type == rules::PatternType::Single && pattern.type != rules::PatternType::Single) {
        return;
    }
    existing = observation;
}

void RemoveSimCards(rules::Cards& hand, const rules::Cards& cards) {
    for (rules::Card card : cards) {
        const auto it = std::find_if(hand.begin(), hand.end(), [card](rules::Card owned) {
            return owned.rank == card.rank && owned.suit == card.suit;
        });
        REQUIRE(it != hand.end());
        hand.erase(it);
    }
}

game::AiContext MakeSimContext(
    const std::array<rules::Cards, 3>& hands,
    rules::PlayerId current,
    const std::optional<rules::HandPattern>& lastPattern,
    rules::PlayerId lastMovePlayer,
    rules::PlayerId trickLeader,
    rules::PlayerId roundLeader,
    int passCount,
    const rules::Cards& playedCards,
    const std::array<std::optional<game::PassObservation>, 3>& observations,
    const std::array<std::vector<game::PassObservation>, 3>& history) {
    const int currentIndex = rules::PlayerIndex(current);
    game::AiContext context;
    context.leading = !lastPattern.has_value();
    if (lastPattern) {
        context.previous = *lastPattern;
    }
    context.ownRemainingCards = static_cast<int>(hands[static_cast<std::size_t>(currentIndex)].size());
    context.currentPlayerIndex = currentIndex;
    context.lastMovePlayerIndex = rules::PlayerIndex(lastMovePlayer);
    context.trickLeaderIndex = rules::PlayerIndex(lastPattern ? trickLeader : current);
    context.roundLeaderIndex = rules::PlayerIndex(roundLeader);
    context.currentTrickPassCount = passCount;
    for (int i = 0; i < 3; ++i) {
        context.remainingCards[static_cast<std::size_t>(i)] = static_cast<int>(hands[static_cast<std::size_t>(i)].size());
    }
    context.nextPlayerRemainingCards =
        static_cast<int>(hands[static_cast<std::size_t>(rules::PlayerIndex(NextSimPlayer(current)))].size());
    context.minOpponentRemainingCards = 100;
    for (int i = 0; i < 3; ++i) {
        if (i != currentIndex) {
            context.minOpponentRemainingCards = std::min(context.minOpponentRemainingCards, context.remainingCards[i]);
        }
    }
    context.playedCards = playedCards;
    context.passObservations = observations;
    context.passHistory = history;
    return context;
}

struct SimRoundResult {
    rules::PlayerId winner{rules::PlayerId::Player};
    rules::PlayerId leader{rules::PlayerId::Player};
};

std::string SimPlayerName(rules::PlayerId player) {
    switch (player) {
    case rules::PlayerId::Player: return "P";
    case rules::PlayerId::Ai1: return "A1";
    case rules::PlayerId::Ai2: return "A2";
    }
    return "?";
}

std::string SimMoveText(rules::PlayerId player, const game::AiMoveChoice& choice, int handSizeBefore) {
    std::string text = SimPlayerName(player);
    text += '[';
    text += std::to_string(handSizeBefore);
    text += "] ";
    if (choice.pass) {
        text += "pass";
        return text;
    }
    text += rules::PatternName(choice.pattern.type);
    text += ' ';
    text += rules::RankName(choice.pattern.mainRank);
    text += " cards=";
    text += std::to_string(choice.cards.size());
    text += " [";
    for (std::size_t i = 0; i < choice.cards.size(); ++i) {
        if (i != 0) {
            text += ' ';
        }
        text += rules::RankName(choice.cards[i].rank);
    }
    text += ']';
    return text;
}

std::string SimCardsText(const rules::Cards& cards) {
    std::string text;
    for (rules::Card card : cards) {
        if (!text.empty()) {
            text += ' ';
        }
        text += rules::RankName(card.rank);
    }
    return text;
}

SimRoundResult RunSimRound(
    const std::array<game::AiStrategy*, 3>& strategies,
    unsigned seed,
    std::optional<rules::PlayerId> requestedLeader,
    std::vector<std::string>* trace = nullptr) {
    rules::Cards deck = rules::CreatePaoDeKuaiDeck();
    rules::Shuffle(deck, seed);

    std::array<rules::Cards, 3> hands;
    for (std::size_t i = 0; i < deck.size(); ++i) {
        hands[i % 3].push_back(deck[i]);
    }

    std::vector<rules::Cards> leaderHands{hands[0], hands[1], hands[2]};
    rules::PlayerId current = requestedLeader.value_or(
        rules::PlayerFromIndex(rules::FindFirstPlayerBySpadeThree(leaderHands)));
    const rules::PlayerId leader = current;
    rules::PlayerId lastMovePlayer = current;
    rules::PlayerId trickLeader = current;
    std::optional<rules::HandPattern> lastPattern;
    rules::Cards playedCards;
    std::array<std::optional<game::PassObservation>, 3> observations{};
    std::array<std::vector<game::PassObservation>, 3> passHistory{};
    int passCount = 0;

    if (trace != nullptr) {
        trace->push_back("initial P: " + SimCardsText(hands[0]));
        trace->push_back("initial A1: " + SimCardsText(hands[1]));
        trace->push_back("initial A2: " + SimCardsText(hands[2]));
    }

    for (int turn = 0; turn < 600; ++turn) {
        const int currentIndex = rules::PlayerIndex(current);
        game::AiMoveChoice choice = strategies[static_cast<std::size_t>(currentIndex)]->ChooseMove(
            hands[static_cast<std::size_t>(currentIndex)],
            MakeSimContext(hands, current, lastPattern, lastMovePlayer, trickLeader, leader, passCount, playedCards, observations, passHistory));
        if (trace != nullptr) {
            trace->push_back(SimMoveText(current, choice, static_cast<int>(hands[static_cast<std::size_t>(currentIndex)].size())));
        }

        if (choice.pass) {
            REQUIRE(lastPattern.has_value());
            CHECK_FALSE(rules::HasAnyFollowMove(
                hands[static_cast<std::size_t>(currentIndex)],
                *lastPattern,
                static_cast<int>(hands[static_cast<std::size_t>(currentIndex)].size())));
            passCount++;
            RecordSimPassObservation(
                observations,
                passHistory,
                current,
                *lastPattern,
                static_cast<int>(hands[static_cast<std::size_t>(currentIndex)].size()));
            if (passCount >= 2) {
                current = lastMovePlayer;
                lastPattern.reset();
                trickLeader = current;
                passCount = 0;
            } else {
                current = NextSimPlayer(current);
            }
            continue;
        }

        const int handSizeBefore = static_cast<int>(hands[static_cast<std::size_t>(currentIndex)].size());
        const auto validation = lastPattern
            ? rules::ValidateFollow(choice.cards, *lastPattern, handSizeBefore)
            : rules::ValidateLead(choice.cards, handSizeBefore);
        REQUIRE(validation.ok);
        if (!lastPattern) {
            trickLeader = current;
        }
        RemoveSimCards(hands[static_cast<std::size_t>(currentIndex)], choice.cards);
        playedCards.insert(playedCards.end(), choice.cards.begin(), choice.cards.end());
        lastPattern = validation.pattern;
        lastMovePlayer = current;
        passCount = 0;

        if (hands[static_cast<std::size_t>(currentIndex)].empty()) {
            return SimRoundResult{current, leader};
        }
        current = NextSimPlayer(current);
    }

    FAIL("simulated round did not finish");
    return SimRoundResult{rules::PlayerId::Player, leader};
}

struct SimSummary {
    std::array<int, 3> wins{0, 0, 0};
    std::array<int, 3> starts{0, 0, 0};
    std::array<std::array<int, 3>, 3> winsByLeader{};
    std::vector<std::array<int, 3>> bucketWins;
    std::vector<std::array<int, 3>> bucketStarts;
    std::vector<std::array<std::array<int, 3>, 3>> bucketWinsByLeader;
};

struct AiTimingStats {
    std::int64_t calls{0};
    std::int64_t totalMicros{0};
    std::int64_t maxMicros{0};
    std::vector<std::int64_t> samples;

    void Add(std::int64_t micros) {
        calls++;
        totalMicros += micros;
        maxMicros = std::max(maxMicros, micros);
        samples.push_back(micros);
    }

    double AverageMillis() const {
        return calls == 0 ? 0.0 : static_cast<double>(totalMicros) / static_cast<double>(calls) / 1000.0;
    }

    double MaxMillis() const {
        return static_cast<double>(maxMicros) / 1000.0;
    }

    double PercentileMillis(double percentile) {
        if (samples.empty()) {
            return 0.0;
        }
        std::sort(samples.begin(), samples.end());
        const double index = percentile * static_cast<double>(samples.size() - 1);
        return static_cast<double>(samples[static_cast<std::size_t>(index + 0.5)]) / 1000.0;
    }
};

class TimedAiStrategy final : public game::AiStrategy {
public:
    explicit TimedAiStrategy(std::unique_ptr<game::AiStrategy> inner) : inner_(std::move(inner)) {}

    game::AiMoveChoice ChooseMove(const rules::Cards& hand, const game::AiContext& context) override {
        const auto start = std::chrono::steady_clock::now();
        game::AiMoveChoice choice = inner_->ChooseMove(hand, context);
        const auto end = std::chrono::steady_clock::now();
        stats.Add(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        return choice;
    }

    AiTimingStats stats;

private:
    std::unique_ptr<game::AiStrategy> inner_;
};

void PrintTiming(const char* name, AiTimingStats& stats) {
    std::cout << name
        << " calls=" << stats.calls
        << " avg_ms=" << stats.AverageMillis()
        << " p95_ms=" << stats.PercentileMillis(0.95)
        << " p99_ms=" << stats.PercentileMillis(0.99)
        << " max_ms=" << stats.MaxMillis()
        << "\n";
}

void RunRotatedStrongTimingDiagnostic(const char* title) {
    const int roundCount = StrongTimingRoundCount();
    const int roundsPerRotation = std::max(1, roundCount / 3);
    TimedAiStrategy timed0(std::make_unique<game::StrongAiStrategy>());
    TimedAiStrategy timed1(std::make_unique<game::StrongAiStrategy>());
    TimedAiStrategy timed2(std::make_unique<game::StrongAiStrategy>());
    std::array<TimedAiStrategy*, 3> timed{&timed0, &timed1, &timed2};
    SimSummary summary;
    std::array<int, 3> strategyWins{0, 0, 0};

    const auto start = std::chrono::steady_clock::now();
    for (int rotation = 0; rotation < 3; ++rotation) {
        for (int round = 0; round < roundsPerRotation; ++round) {
            std::array<TimedAiStrategy*, 3> seated{
                timed[static_cast<std::size_t>(rotation % 3)],
                timed[static_cast<std::size_t>((rotation + 1) % 3)],
                timed[static_cast<std::size_t>((rotation + 2) % 3)]
            };
            std::array<game::AiStrategy*, 3> strategies{seated[0], seated[1], seated[2]};
            const SimRoundResult result = RunSimRound(
                strategies,
                20260714u + static_cast<unsigned>(round),
                std::nullopt);
            const int winnerIndex = rules::PlayerIndex(result.winner);
            const int leaderIndex = rules::PlayerIndex(result.leader);
            ++summary.wins[static_cast<std::size_t>(winnerIndex)];
            ++summary.starts[static_cast<std::size_t>(leaderIndex)];
            ++summary.winsByLeader[static_cast<std::size_t>(leaderIndex)][static_cast<std::size_t>(winnerIndex)];
            TimedAiStrategy* winnerStrategy = seated[static_cast<std::size_t>(winnerIndex)];
            for (std::size_t i = 0; i < timed.size(); ++i) {
                if (timed[i] == winnerStrategy) {
                    ++strategyWins[i];
                    break;
                }
            }
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const double elapsedSeconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;

    const std::array<int, 3>& wins = summary.wins;
    std::cout << title
        << " rotated_rounds=" << roundsPerRotation * 3
        << " rounds_per_rotation=" << roundsPerRotation
        << " elapsed_s=" << elapsedSeconds
        << " wins: Player=" << wins[0]
        << ", AI1=" << wins[1]
        << ", AI2=" << wins[2]
        << "; strategy_wins: Strong0=" << strategyWins[0]
        << ", Strong1=" << strategyWins[1]
        << ", StrongSlot2=" << strategyWins[2]
        << "; leaders: Player=" << summary.starts[0]
        << ", AI1=" << summary.starts[1]
        << ", AI2=" << summary.starts[2]
        << "\n";
    PrintTiming("Strong0", timed0.stats);
    PrintTiming("Strong1", timed1.stats);
    PrintTiming("StrongSlot2", timed2.stats);

    CHECK(wins[0] + wins[1] + wins[2] == roundsPerRotation * 3);
    CHECK(timed0.stats.calls > 0);
    CHECK(timed1.stats.calls > 0);
    CHECK(timed2.stats.calls > 0);
    CHECK(strategyWins[0] + strategyWins[1] + strategyWins[2] == roundsPerRotation * 3);
}

SimSummary RunAutoplayRounds(std::array<game::AiStrategy*, 3> strategies, unsigned seedBase, int roundCount) {
    SimSummary summary;
    std::optional<rules::PlayerId> nextLeader;
    const bool continuous = RunContinuousFairnessRounds();
    int listedAi1LeaderLosses = 0;
    int listedLeaderLosses = 0;
    const std::optional<rules::PlayerId> listedLeader = ListLeaderLossesFor();
    const int listLossesFrom = ListAi1LeaderLossesFromRound();
    const int traceRound = AiFairnessTraceRound();

    for (int round = 0; round < roundCount; ++round) {
        std::vector<std::string> trace;
        std::vector<std::string>* tracePtr = round == traceRound ? &trace : nullptr;
        const SimRoundResult result = RunSimRound(
            strategies,
            seedBase + static_cast<unsigned>(round),
            continuous ? nextLeader : std::nullopt,
            tracePtr);
        const int winnerIndex = rules::PlayerIndex(result.winner);
        const int leaderIndex = rules::PlayerIndex(result.leader);
        if (round == traceRound) {
            std::cout << "fairness trace round=" << round
                << " seed=" << seedBase + static_cast<unsigned>(round)
                << " leader=" << SimPlayerName(result.leader)
                << " winner=" << SimPlayerName(result.winner) << "\n";
            for (const std::string& line : trace) {
                std::cout << "  " << line << "\n";
            }
        }
        if (ListAi1LeaderLosses() &&
            round >= listLossesFrom &&
            result.leader == rules::PlayerId::Ai1 &&
            result.winner != rules::PlayerId::Ai1 &&
            listedAi1LeaderLosses < 24) {
            std::cout << "AI1 leader loss round=" << round
                << " seed=" << seedBase + static_cast<unsigned>(round)
                << " winner=" << SimPlayerName(result.winner) << "\n";
            listedAi1LeaderLosses++;
        }
        if (listedLeader &&
            round >= listLossesFrom &&
            result.leader == *listedLeader &&
            result.winner != rules::PlayerId::Ai1 &&
            listedLeaderLosses < 24) {
            std::cout << "AI1 loss leader=" << SimPlayerName(*listedLeader)
                << " round=" << round
                << " seed=" << seedBase + static_cast<unsigned>(round)
                << " winner=" << SimPlayerName(result.winner) << "\n";
            listedLeaderLosses++;
        }
        ++summary.wins[static_cast<std::size_t>(winnerIndex)];
        ++summary.starts[static_cast<std::size_t>(leaderIndex)];
        ++summary.winsByLeader[static_cast<std::size_t>(leaderIndex)][static_cast<std::size_t>(winnerIndex)];
        if (roundCount >= 200) {
            const std::size_t bucket = static_cast<std::size_t>(round / 100);
            if (summary.bucketWins.size() <= bucket) {
                summary.bucketWins.resize(bucket + 1);
                summary.bucketStarts.resize(bucket + 1);
                summary.bucketWinsByLeader.resize(bucket + 1);
            }
            ++summary.bucketWins[bucket][static_cast<std::size_t>(winnerIndex)];
            ++summary.bucketStarts[bucket][static_cast<std::size_t>(leaderIndex)];
            ++summary.bucketWinsByLeader[bucket][static_cast<std::size_t>(leaderIndex)][static_cast<std::size_t>(winnerIndex)];
        }
        if (continuous) {
            nextLeader = result.winner;
        }
    }

    return summary;
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

    bool IsRemote(rules::PlayerId player) const override {
        return CanHandle(player);
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

void WaitForAsyncAi(game::GameState& state) {
    for (int i = 0; i < 200 && state.ExternalAiPending(); ++i) {
        state.Update(0.05f);
        if (state.ExternalAiPending()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
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

TEST_CASE("round trace writes json with human move state when enabled") {
    const auto root = std::filesystem::temp_directory_path() / "pao_de_kuai_round_trace_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    game::GameState state;
    state.SetRoundTraceEnabled(true);
    state.SetRoundTraceRoot(root.string());
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Three, rules::Suit::Spades)},
            rules::Cards{C(rules::Rank::Four, rules::Suit::Spades)},
            rules::Cards{C(rules::Rank::Five, rules::Suit::Spades)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Player);

    state.TogglePlayerCard(0);
    REQUIRE(state.PlaySelected());
    REQUIRE(state.IsRoundOver());
    REQUIRE_FALSE(state.LastRoundTracePath().empty());

    std::ifstream file(state.LastRoundTracePath(), std::ios::binary);
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CHECK(json.find("pdk_round_trace") != std::string::npos);
    CHECK(json.find("\"initialHands\"") != std::string::npos);
    CHECK(json.find("\"turns\"") != std::string::npos);
    CHECK(json.find("\"source\":\t\"human\"") != std::string::npos);
    CHECK(json.find("\"finalCards\"") != std::string::npos);
    CHECK(json.find("\"3S\"") != std::string::npos);
    CHECK(json.find("\"winner\":\t\"player\"") != std::string::npos);

    file.close();
    std::filesystem::remove_all(root);
}

TEST_CASE("disabled strong ai beats basic over 1000 autoplay rounds" * doctest::skip(!RunAiFairnessTest())) {
    const int roundCount = AiFairnessRoundCount();
    const int minStrongWins = (roundCount * 47 + 99) / 100;

    game::BasicAiStrategy playerAi;
    game::BasicAiStrategy ai1Basic;
    game::StrongAiStrategy strongAi;
    game::BasicAiStrategy ai2;
    game::AiStrategy* ai1 = RunStrongFairnessStrategy() ? static_cast<game::AiStrategy*>(&strongAi) : &ai1Basic;
    const SimSummary summary = RunAutoplayRounds(
        std::array<game::AiStrategy*, 3>{&playerAi, ai1, &ai2},
        20260619u,
        roundCount);
    const std::array<int, 3>& wins = summary.wins;

    std::cout << "AI1 vs Basic wins: Player=" << wins[0] << ", AI1=" << wins[1] << ", AI2=" << wins[2]
        << "; leaders: Player=" << summary.starts[0] << ", AI1=" << summary.starts[1] << ", AI2=" << summary.starts[2]
        << "; by leader P=(" << summary.winsByLeader[0][0] << ", " << summary.winsByLeader[0][1] << ", " << summary.winsByLeader[0][2]
        << ") A1=(" << summary.winsByLeader[1][0] << ", " << summary.winsByLeader[1][1] << ", " << summary.winsByLeader[1][2]
        << ") A2=(" << summary.winsByLeader[2][0] << ", " << summary.winsByLeader[2][1] << ", " << summary.winsByLeader[2][2] << ")\n";
    for (std::size_t i = 0; i < summary.bucketWins.size(); ++i) {
        const auto& bucket = summary.bucketWins[i];
        const auto& starts = summary.bucketStarts[i];
        const auto& byLeader = summary.bucketWinsByLeader[i];
        std::cout << "  bucket " << (i * 100) << '-' << (i * 100 + 99)
            << ": Player=" << bucket[0] << ", AI1=" << bucket[1] << ", AI2=" << bucket[2]
            << "; leaders P=" << starts[0] << ", A1=" << starts[1] << ", A2=" << starts[2]
            << "; rows P=(" << byLeader[0][0] << ", " << byLeader[0][1] << ", " << byLeader[0][2]
            << ") A1=(" << byLeader[1][0] << ", " << byLeader[1][1] << ", " << byLeader[1][2]
            << ") A2=(" << byLeader[2][0] << ", " << byLeader[2][1] << ", " << byLeader[2][2] << ")\n";
    }

    INFO("AI1 vs Basic wins: Player=" << wins[0] << ", AI1=" << wins[1] << ", AI2=" << wins[2]
        << "; leaders: Player=" << summary.starts[0] << ", AI1=" << summary.starts[1] << ", AI2=" << summary.starts[2]
        << "; by leader P=(" << summary.winsByLeader[0][0] << ", " << summary.winsByLeader[0][1] << ", " << summary.winsByLeader[0][2]
        << ") A1=(" << summary.winsByLeader[1][0] << ", " << summary.winsByLeader[1][1] << ", " << summary.winsByLeader[1][2]
        << ") A2=(" << summary.winsByLeader[2][0] << ", " << summary.winsByLeader[2][1] << ", " << summary.winsByLeader[2][2] << ")");
    CHECK(wins[0] + wins[1] + wins[2] == roundCount);
    CHECK(wins[rules::PlayerIndex(rules::PlayerId::Ai1)] >= minStrongWins);
}

TEST_CASE("disabled strong baseline timing diagnostics over rotated 30 rounds" * doctest::skip(!RunStrongBaselineDiagnostics())) {
    RunRotatedStrongTimingDiagnostic("Strong timing diagnostics");
}

TEST_CASE("disabled compare strong ai decisions against basic seeds" * doctest::skip(!RunAiCompareDiagnostics())) {
    const int roundCount = AiFairnessRoundCount();
    const unsigned seedBase = AiCompareSeedBase();
    const unsigned traceSeed = AiCompareTraceSeed();
    const std::optional<rules::PlayerId> forcedLeader = AiCompareForcedLeader();

    game::BasicAiStrategy basicPlayer;
    game::BasicAiStrategy basicAi1;
    game::BasicAiStrategy basicAi2;
    game::StrongAiStrategy strongAi;

    int strongGains = 0;
    int strongRegressions = 0;
    int sameWins = 0;
    for (int round = 0; round < roundCount; ++round) {
        const unsigned seed = seedBase + static_cast<unsigned>(round);
        std::vector<std::string> basicTrace;
        std::vector<std::string> strongTrace;
        std::vector<std::string>* basicTracePtr = traceSeed == seed ? &basicTrace : nullptr;
        std::vector<std::string>* strongTracePtr = traceSeed == seed ? &strongTrace : nullptr;

        const SimRoundResult basic = RunSimRound(
            std::array<game::AiStrategy*, 3>{&basicPlayer, &basicAi1, &basicAi2},
            seed,
            forcedLeader,
            basicTracePtr);
        const SimRoundResult strong = RunSimRound(
            std::array<game::AiStrategy*, 3>{&basicPlayer, &strongAi, &basicAi2},
            seed,
            forcedLeader,
            strongTracePtr);

        const bool basicAi1Win = basic.winner == rules::PlayerId::Ai1;
        const bool strongAi1Win = strong.winner == rules::PlayerId::Ai1;
        if (basicAi1Win && !strongAi1Win) {
            strongRegressions++;
            if (strongRegressions <= 12) {
                std::cout << "regression seed=" << seed
                    << " basicWinner=" << SimPlayerName(basic.winner)
                    << " strongWinner=" << SimPlayerName(strong.winner)
                    << " leader=" << SimPlayerName(strong.leader) << "\n";
            }
        } else if (!basicAi1Win && strongAi1Win) {
            strongGains++;
            if (strongGains <= 12) {
                std::cout << "gain seed=" << seed
                    << " basicWinner=" << SimPlayerName(basic.winner)
                    << " strongWinner=" << SimPlayerName(strong.winner)
                    << " leader=" << SimPlayerName(strong.leader) << "\n";
            }
        } else if (basicAi1Win && strongAi1Win) {
            sameWins++;
        }

        if (traceSeed == seed) {
            std::cout << "basic trace seed=" << seed << " winner=" << SimPlayerName(basic.winner) << "\n";
            for (const std::string& line : basicTrace) {
                std::cout << "  " << line << "\n";
            }
            std::cout << "strong trace seed=" << seed << " winner=" << SimPlayerName(strong.winner) << "\n";
            for (const std::string& line : strongTrace) {
                std::cout << "  " << line << "\n";
            }
        }
    }

    std::cout << "compare strong-vs-basic independent seeds: gains=" << strongGains
        << ", regressions=" << strongRegressions
        << ", sameAi1Wins=" << sameWins << "\n";
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

TEST_CASE("starting a new round cancels autoplay") {
    game::GameState state;
    state.StartNewRound("Tester", 20260606u);
    state.ToggleAutoplay();
    REQUIRE(state.Autoplay());

    state.StartNewRound("Tester", 20260607u);
    CHECK_FALSE(state.Autoplay());
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
    CHECK(state.RemoteAiPending());
    REQUIRE(controller->startCount == 1);
    REQUIRE(controller->lastRequest.has_value());
    CHECK(controller->lastRequest->history.empty());
    CHECK(controller->lastRequest->context.currentPlayerIndex == rules::PlayerIndex(rules::PlayerId::Ai1));

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

TEST_CASE("AI1 can use local async strong controller and records a local decision") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto controller = std::make_shared<game::LocalAiController>();
    controller->SetStrategy(rules::PlayerId::Ai1, game::LocalAiKind::Strong);

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
    CHECK_FALSE(state.RemoteAiPending());
    WaitForAsyncAi(state);

    CHECK_FALSE(state.ExternalAiPending());
    REQUIRE(state.TurnRecords().size() == 1);
    const game::TurnRecord& record = state.TurnRecords().back();
    CHECK(record.actor == rules::PlayerId::Ai1);
    CHECK(record.source == game::TurnDecisionSource::LocalAi);
    CHECK(record.accepted);
    CHECK(record.finalAction.action == "play");
    REQUIRE_FALSE(state.LastCards().empty());
    CHECK(rules::RankValue(state.LastCards().front().rank) > rules::RankValue(rules::Rank::Four));
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
    CHECK(state.RemoteAiPending());
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

TEST_CASE("AI2 can use local async basic controller through multi controller routing") {
    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    auto remoteController = std::make_shared<MockExternalAiController>(
        game::ExternalAiResult{},
        rules::PlayerId::Ai1);
    auto localController = std::make_shared<game::LocalAiController>();
    localController->SetStrategy(rules::PlayerId::Ai2, game::LocalAiKind::Basic);

    game::GameState state;
    state.SetExternalAiControllers({
        remoteController,
        localController
    });
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
    CHECK_FALSE(state.RemoteAiPending());
    CHECK(remoteController->startCount == 0);
    WaitForAsyncAi(state);

    CHECK_FALSE(state.ExternalAiPending());
    REQUIRE(state.TurnRecords().size() == 1);
    const game::TurnRecord& record = state.TurnRecords().back();
    CHECK(record.actor == rules::PlayerId::Ai2);
    CHECK(record.source == game::TurnDecisionSource::LocalAi);
    CHECK(record.accepted);
    REQUIRE_FALSE(state.LastCards().empty());
    CHECK(rules::RankValue(state.LastCards().front().rank) > rules::RankValue(rules::Rank::Four));
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
