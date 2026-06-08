#include "ai/LlmOffscreenExperiment.h"

#include "game/AiStrategy.h"
#include "rules/Deck.h"
#include "rules/MoveValidator.h"

#include <cJSON.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <random>
#include <sstream>

namespace pdk::ai {
namespace {

struct Candidate {
    rules::Cards cards;
    rules::HandPattern pattern;
};

struct SimulationState {
    std::array<rules::Cards, 3> hands;
    rules::PlayerId currentPlayer{rules::PlayerId::Player};
    rules::PlayerId lastMovePlayer{rules::PlayerId::Player};
    rules::Cards lastCards;
    std::optional<rules::HandPattern> lastPattern;
    rules::Cards playedCards;
    std::array<std::optional<game::PassObservation>, 3> passObservations{};
    int passCount{0};
    bool roundOver{false};
};

struct AttemptResult {
    LlmOffscreenExperimentResult result;
    bool retryWithStrict{false};
};

std::string NowStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return out.str();
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

int Index(rules::PlayerId player) {
    return rules::PlayerIndex(player);
}

rules::PlayerId NextPlayer(rules::PlayerId player) {
    return rules::PlayerFromIndex((Index(player) + 2) % 3);
}

TurnSnapshot Snapshot(const SimulationState& state) {
    return TurnSnapshot{
        state.hands,
        state.lastCards,
        state.lastPattern,
        state.lastMovePlayer,
        state.currentPlayer,
        state.passCount
    };
}

std::string RankList(const std::vector<std::string>& ranks) {
    std::ostringstream out;
    for (std::size_t i = 0; i < ranks.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << ranks[i];
    }
    return out.str();
}

std::vector<std::string> RanksOf(const rules::Cards& cards) {
    std::vector<std::string> ranks;
    ranks.reserve(cards.size());
    for (rules::Card card : cards) {
        ranks.push_back(rules::RankName(card.rank));
    }
    return ranks;
}

PdkAiMove MoveFromCards(const rules::Cards& cards, bool pass = false) {
    PdkAiMove move;
    move.action = pass ? "pass" : "play";
    if (!pass) {
        move.ranks = RanksOf(cards);
    }
    return move;
}

std::string MoveText(const PdkAiMove& move) {
    if (move.action == "pass") {
        return "不要";
    }
    return "出 " + RankList(move.ranks);
}

std::string PatternText(const std::optional<rules::HandPattern>& pattern) {
    return pattern ? rules::PatternDescription(*pattern) : "无";
}

std::string CardsText(const rules::Cards& cards) {
    if (cards.empty()) {
        return "无";
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << rules::RankName(cards[i].rank);
    }
    return out.str();
}

std::string SystemPrompt() {
    return
        "你是三人跑得快的 AI1。你必须用中文思考，并且每次只调用 choose_move 工具。\n"
        "固定规则：一副 48 张牌，去掉大小王，只保留黑桃2，去掉梅花A，所以 A 有 3 张，2 只有 1 张。"
        "三家各 16 张，持有黑桃3的玩家先出，第一手不强制包含黑桃3。出牌顺序为 player -> ai2 -> ai1 -> player 的逆时针顺序。\n"
        "牌点从小到大：3 4 5 6 7 8 9 10 J Q K A 2。花色不参与大小比较，你只需要返回点数。\n"
        "允许牌型：单张、对子、顺子(至少5张，不能含2)、连对(至少2连对，不能含2)、三带一、三带二(带牌可以是任意散牌)、"
        "飞机(至少2组连续三张，可带N到2N张散牌，带牌不参与比较)、炸弹(3到K的四张，A和2不能做炸弹)。\n"
        "跟牌必须同类同长度比较主点数，炸弹可以压任意非炸弹；炸弹之间比主点数。要得起必须打，要不起才能 pass。"
        "如果你返回非法牌、本地不存在的牌、或者该打却 pass，本地裁判会判定失败。";
}

std::string CurrentPrompt(
    const std::vector<TurnRecord>& records,
    const std::array<rules::Cards, 3>& hands,
    const rules::Cards& lastCards,
    const std::optional<rules::HandPattern>& lastPattern,
    rules::PlayerId lastMovePlayer) {
    std::ostringstream out;
    out << "现在轮到 AI1 决策。\n";
    out << "AI1 手牌: " << CardsText(hands[Index(rules::PlayerId::Ai1)]) << "\n";
    out << "剩余张数: player=" << hands[0].size()
        << ", ai1=" << hands[1].size()
        << ", ai2=" << hands[2].size() << "\n";
    if (lastPattern) {
        out << "当前要压的牌: " << PlayerLabel(lastMovePlayer) << " "
            << CardsText(lastCards) << "，牌型 " << PatternText(lastPattern) << "\n";
    } else {
        out << "当前是领出，可以主动选择任意合法牌型。\n";
    }

    std::size_t start = 0;
    for (std::size_t i = records.size(); i > 0; --i) {
        if (records[i - 1].actor == rules::PlayerId::Ai1) {
            start = i;
            break;
        }
    }
    out << "从上次 AI1 tool_call 到现在发生的全部公开行动:\n";
    if (start >= records.size()) {
        out << "- 无，AI1 连续决策。\n";
    }
    for (std::size_t i = start; i < records.size(); ++i) {
        const TurnRecord& record = records[i];
        out << "- " << record.turnNo << " " << PlayerLabel(record.actor) << " "
            << MoveText(record.finalAction) << "，来源 " << SourceLabel(record.source)
            << "，原因 " << ReasonLabel(record.reason);
        if (record.finalPattern) {
            out << "，牌型 " << PatternText(record.finalPattern);
        }
        out << "\n";
    }
    out << "请结合完整历史和当前手牌，只调用 choose_move。";
    return out.str();
}

std::string CurrentPrompt(const SimulationState& state, const std::vector<TurnRecord>& records) {
    return CurrentPrompt(records, state.hands, state.lastCards, state.lastPattern, state.lastMovePlayer);
}

std::string ArgumentsJson(const PdkAiMove& move) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "action", move.action.c_str());
    cJSON* ranks = cJSON_CreateArray();
    for (const std::string& rank : move.ranks) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(rank.c_str()));
    }
    cJSON_AddItemToObject(root, "ranks", ranks);
    if (!move.talk.empty()) {
        cJSON_AddStringToObject(root, "talk", move.talk.c_str());
    }
    char* text = cJSON_PrintUnformatted(root);
    std::string result = text ? text : "";
    cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

std::string SyntheticReasoning(const TurnRecord& record) {
    std::ostringstream out;
    out << "本地记录：";
    if (record.reason == TurnDecisionReason::CannotBeat) {
        out << PlayerLabel(record.actor) << " 按规则要不起，只能不要。";
    } else if (record.reason == TurnDecisionReason::OnlyLegalMove) {
        out << PlayerLabel(record.actor) << " 只有一种合法选择，直接执行。";
    } else {
        out << PlayerLabel(record.actor) << " 使用本地基础策略执行 " << MoveText(record.finalAction) << "。";
    }
    return out.str();
}

PdkAiMessage SyntheticAssistantMessage(const TurnRecord& record) {
    const std::string id = "synthetic_turn_" + std::to_string(record.turnNo);
    return PdkAiMessage{
        "assistant",
        {},
        record.trace.reasoningContent,
        {},
        {},
        {PdkAiToolCall{id, "choose_move", ArgumentsJson(record.finalAction)}}
    };
}

PdkAiMessage ToolMessageFor(const PdkAiMessage& assistant, const PdkAiMove& move, bool accepted, const std::string& reason) {
    const std::string id = assistant.toolCalls.empty() ? "" : assistant.toolCalls.front().id;
    return PdkAiMessage{
        "tool",
        PdkAiClient::BuildToolResultJson(move, accepted, reason),
        {},
        id,
        "choose_move",
        {}
    };
}

std::vector<PdkAiMessage> BuildMessages(
    const std::vector<TurnRecord>& records,
    const std::string& currentUserPrompt,
    ToolHistoryMode mode) {
    std::vector<PdkAiMessage> messages;
    messages.push_back(PdkAiMessage{"system", SystemPrompt(), {}, {}, {}, {}});
    for (const TurnRecord& record : records) {
        if (record.actor != rules::PlayerId::Ai1) {
            continue;
        }
        const std::vector<TurnRecord> prefix(records.begin(), records.begin() + static_cast<std::ptrdiff_t>(&record - records.data()));
        messages.push_back(PdkAiMessage{
            "user",
            CurrentPrompt(prefix, record.before.hands, record.before.lastCards, record.before.lastPattern, record.before.lastMovePlayer),
            {},
            {},
            {},
            {}
        });
        messages.push_back(record.trace.assistantMessage);
        if (mode == ToolHistoryMode::Strict && record.trace.toolMessage) {
            messages.push_back(*record.trace.toolMessage);
        }
    }
    messages.push_back(PdkAiMessage{"user", currentUserPrompt, {}, {}, {}, {}});
    return messages;
}

std::vector<Candidate> LegalCandidates(const SimulationState& state, rules::PlayerId player) {
    const rules::Cards& hand = state.hands[Index(player)];
    const int n = static_cast<int>(hand.size());
    std::vector<Candidate> candidates;
    if (n <= 0 || n >= 63) {
        return candidates;
    }
    const std::uint64_t limit = 1ull << n;
    for (std::uint64_t mask = 1; mask < limit; ++mask) {
        rules::Cards cards;
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                cards.push_back(hand[static_cast<std::size_t>(i)]);
            }
        }
        const auto validation = state.lastPattern
            ? rules::ValidateFollow(cards, *state.lastPattern, n)
            : rules::ValidateLead(cards, n);
        if (validation.ok) {
            candidates.push_back(Candidate{cards, validation.pattern});
        }
    }
    return candidates;
}

game::AiContext MakeAiContext(const SimulationState& state, rules::PlayerId player) {
    game::AiContext context;
    context.leading = !state.lastPattern.has_value();
    if (state.lastPattern) {
        context.previous = *state.lastPattern;
    }
    const int currentIndex = Index(player);
    context.ownRemainingCards = static_cast<int>(state.hands[currentIndex].size());
    context.currentPlayerIndex = currentIndex;
    for (int i = 0; i < 3; ++i) {
        context.remainingCards[i] = static_cast<int>(state.hands[static_cast<std::size_t>(i)].size());
    }
    context.nextPlayerRemainingCards = static_cast<int>(state.hands[Index(NextPlayer(player))].size());
    context.minOpponentRemainingCards = 99;
    for (int i = 0; i < 3; ++i) {
        if (i != currentIndex) {
            context.minOpponentRemainingCards = std::min(context.minOpponentRemainingCards, context.remainingCards[i]);
        }
    }
    context.playedCards = state.playedCards;
    context.passObservations = state.passObservations;
    return context;
}

void RemoveCards(rules::Cards& hand, const rules::Cards& cards) {
    for (rules::Card card : cards) {
        const auto it = std::find(hand.begin(), hand.end(), card);
        if (it != hand.end()) {
            hand.erase(it);
        }
    }
}

void RecordPassObservation(SimulationState& state, rules::PlayerId player) {
    if (!state.lastPattern) {
        return;
    }
    const int index = Index(player);
    state.passObservations[static_cast<std::size_t>(index)] = game::PassObservation{
        *state.lastPattern,
        static_cast<int>(state.hands[static_cast<std::size_t>(index)].size())
    };
}

void ApplyPlay(SimulationState& state, rules::PlayerId player, const rules::Cards& cards, const rules::HandPattern& pattern) {
    RemoveCards(state.hands[Index(player)], cards);
    state.playedCards.insert(state.playedCards.end(), cards.begin(), cards.end());
    state.lastCards = cards;
    state.lastPattern = pattern;
    state.lastMovePlayer = player;
    state.passCount = 0;
    if (state.hands[Index(player)].empty()) {
        state.roundOver = true;
        return;
    }
    state.currentPlayer = NextPlayer(player);
}

void ApplyPass(SimulationState& state, rules::PlayerId player) {
    RecordPassObservation(state, player);
    state.passCount++;
    if (state.passCount >= 2) {
        state.currentPlayer = state.lastMovePlayer;
        state.lastCards.clear();
        state.lastPattern.reset();
        state.passCount = 0;
    } else {
        state.currentPlayer = NextPlayer(player);
    }
}

std::optional<rules::Rank> ParseRank(const std::string& rank) {
    static const std::map<std::string, rules::Rank> ranks{
        {"3", rules::Rank::Three}, {"4", rules::Rank::Four}, {"5", rules::Rank::Five},
        {"6", rules::Rank::Six}, {"7", rules::Rank::Seven}, {"8", rules::Rank::Eight},
        {"9", rules::Rank::Nine}, {"10", rules::Rank::Ten}, {"J", rules::Rank::Jack},
        {"Q", rules::Rank::Queen}, {"K", rules::Rank::King}, {"A", rules::Rank::Ace},
        {"2", rules::Rank::Two}
    };
    const auto it = ranks.find(rank);
    return it == ranks.end() ? std::nullopt : std::optional<rules::Rank>(it->second);
}

bool CardsFromRanks(const rules::Cards& hand, const std::vector<std::string>& ranks, rules::Cards& out, std::string& reason) {
    std::vector<bool> used(hand.size(), false);
    for (const std::string& rankText : ranks) {
        const auto rank = ParseRank(rankText);
        if (!rank) {
            reason = "未知点数: " + rankText;
            return false;
        }
        bool found = false;
        for (std::size_t i = 0; i < hand.size(); ++i) {
            if (!used[i] && hand[i].rank == *rank) {
                used[i] = true;
                out.push_back(hand[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            reason = "手牌中没有足够的点数: " + rankText;
            return false;
        }
    }
    return true;
}

TurnRecord MakeLocalRecord(SimulationState& state, int turnNo, rules::PlayerId actor, game::BasicAiStrategy& localAi) {
    TurnRecord record;
    record.turnNo = turnNo;
    record.actor = actor;
    record.source = TurnDecisionSource::LocalAi;
    record.before = Snapshot(state);
    const std::vector<Candidate> legal = LegalCandidates(state, actor);
    if (legal.empty() && state.lastPattern) {
        record.source = TurnDecisionSource::System;
        record.reason = TurnDecisionReason::CannotBeat;
        record.finalAction = PdkAiMove{"pass", {}, {}};
        record.requestedAction = record.finalAction;
        ApplyPass(state, actor);
    } else {
        if (legal.size() == 1) {
            record.reason = TurnDecisionReason::OnlyLegalMove;
        }
        const game::AiMoveChoice choice = localAi.ChooseMove(state.hands[Index(actor)], MakeAiContext(state, actor));
        if (choice.pass) {
            record.reason = TurnDecisionReason::CannotBeat;
            record.finalAction = PdkAiMove{"pass", {}, {}};
            record.requestedAction = record.finalAction;
            ApplyPass(state, actor);
        } else {
            record.finalAction = MoveFromCards(choice.cards);
            record.requestedAction = record.finalAction;
            record.finalCards = choice.cards;
            record.finalPattern = choice.pattern;
            ApplyPlay(state, actor, choice.cards, choice.pattern);
        }
    }
    record.after = Snapshot(state);
    record.trace.reasoningContent = SyntheticReasoning(record);
    record.trace.assistantMessage = SyntheticAssistantMessage(record);
    record.trace.toolMessage = ToolMessageFor(record.trace.assistantMessage, record.finalAction, true, record.trace.reasoningContent);
    record.validationMessage = record.trace.reasoningContent;
    return record;
}

TurnRecord MakeForcedAi1Record(
    SimulationState& state,
    int turnNo,
    const std::vector<Candidate>& legal,
    const std::vector<TurnRecord>& records,
    game::BasicAiStrategy& localAi) {
    TurnRecord record = MakeLocalRecord(state, turnNo, rules::PlayerId::Ai1, localAi);
    if (legal.empty()) {
        record.source = TurnDecisionSource::System;
        record.reason = TurnDecisionReason::CannotBeat;
    } else {
        record.source = TurnDecisionSource::LocalAi;
        record.reason = TurnDecisionReason::OnlyLegalMove;
    }
    record.trace.reasoningContent = SyntheticReasoning(record);
    record.trace.assistantMessage = SyntheticAssistantMessage(record);
    record.trace.toolMessage = ToolMessageFor(record.trace.assistantMessage, record.finalAction, true, record.trace.reasoningContent);
    return record;
}

bool ValidateLlmMove(
    const SimulationState& state,
    const PdkAiMove& move,
    rules::Cards& cards,
    rules::HandPattern& pattern,
    std::string& reason) {
    if (move.action == "pass") {
        if (!state.lastPattern) {
            reason = "领出时不能 pass";
            return false;
        }
        if (!LegalCandidates(state, rules::PlayerId::Ai1).empty()) {
            reason = "要得起必须打";
            return false;
        }
        return true;
    }
    if (move.action != "play") {
        reason = "未知 action";
        return false;
    }
    if (!CardsFromRanks(state.hands[Index(rules::PlayerId::Ai1)], move.ranks, cards, reason)) {
        return false;
    }
    const int n = static_cast<int>(state.hands[Index(rules::PlayerId::Ai1)].size());
    const auto validation = state.lastPattern
        ? rules::ValidateFollow(cards, *state.lastPattern, n)
        : rules::ValidateLead(cards, n);
    if (!validation.ok) {
        reason = validation.reason;
        return false;
    }
    pattern = validation.pattern;
    return true;
}

std::filesystem::path CallPath(const std::filesystem::path& root, int callNo, const char* suffix) {
    std::ostringstream name;
    name << std::setw(4) << std::setfill('0') << callNo << "_" << suffix << ".json";
    return root / "calls" / name.str();
}

TurnRecord MakeLlmRecord(
    SimulationState& state,
    int turnNo,
    int callNo,
    const LlmOffscreenExperimentConfig& config,
    ToolHistoryMode mode,
    const std::vector<TurnRecord>& records,
    game::BasicAiStrategy& localAi,
    int& invalidLlmMoves,
    std::string& errorMessage) {
    TurnRecord record;
    record.turnNo = turnNo;
    record.actor = rules::PlayerId::Ai1;
    record.source = TurnDecisionSource::LlmAi;
    record.reason = TurnDecisionReason::NormalChoice;
    record.before = Snapshot(state);
    record.trace.requestLogPath = CallPath(config.runRoot, callNo, "request");
    record.trace.responseLogPath = CallPath(config.runRoot, callNo, "response");

    const std::string currentPrompt = CurrentPrompt(state, records);
    std::vector<PdkAiMessage> messages = BuildMessages(records, currentPrompt, mode);

    const PdkAiResponse response = PdkAiClient().ChooseMove(PdkAiRequest{
        config.provider,
        messages,
        record.trace.requestLogPath,
        record.trace.responseLogPath,
        45000
    });
    if (!response.ok) {
        errorMessage = response.errorMessage;
        record.accepted = false;
        record.validationMessage = response.errorMessage;
        return record;
    }

    record.trace.reasoningContent = response.reasoningContent;
    record.trace.assistantMessage = response.assistantMessage;
    record.requestedAction = response.move;

    rules::Cards cards;
    rules::HandPattern pattern;
    std::string validationReason;
    if (ValidateLlmMove(state, response.move, cards, pattern, validationReason)) {
        record.finalAction = response.move;
        record.finalCards = cards;
        if (response.move.action == "pass") {
            record.reason = TurnDecisionReason::CannotBeat;
            ApplyPass(state, rules::PlayerId::Ai1);
        } else {
            record.finalPattern = pattern;
            ApplyPlay(state, rules::PlayerId::Ai1, cards, pattern);
        }
        record.accepted = true;
        record.validationMessage = "LLM 出牌合法";
    } else {
        invalidLlmMoves++;
        record.accepted = false;
        record.reason = TurnDecisionReason::LlmFallback;
        record.validationMessage = validationReason;
        const game::AiMoveChoice fallback = localAi.ChooseMove(state.hands[Index(rules::PlayerId::Ai1)], MakeAiContext(state, rules::PlayerId::Ai1));
        if (fallback.pass) {
            record.finalAction = PdkAiMove{"pass", {}, {}};
            ApplyPass(state, rules::PlayerId::Ai1);
        } else {
            record.finalAction = MoveFromCards(fallback.cards);
            record.finalCards = fallback.cards;
            record.finalPattern = fallback.pattern;
            ApplyPlay(state, rules::PlayerId::Ai1, fallback.cards, fallback.pattern);
        }
    }
    record.after = Snapshot(state);
    record.trace.toolMessage = ToolMessageFor(record.trace.assistantMessage, record.finalAction, record.accepted, record.validationMessage);
    return record;
}

void StartRound(SimulationState& state, unsigned seed) {
    std::mt19937 rng(seed);
    rules::Cards deck = rules::CreatePaoDeKuaiDeck();
    rules::Shuffle(deck, rng);
    for (std::size_t i = 0; i < deck.size(); ++i) {
        state.hands[i % 3].push_back(deck[i]);
    }
    for (rules::Cards& hand : state.hands) {
        rules::SortByGameOrder(hand);
    }
    state.currentPlayer = rules::PlayerFromIndex(rules::FindFirstPlayerBySpadeThree({
        state.hands[0],
        state.hands[1],
        state.hands[2]
    }));
    state.lastMovePlayer = state.currentPlayer;
}

AttemptResult RunAttempt(const LlmOffscreenExperimentConfig& originalConfig, ToolHistoryMode mode, unsigned seed) {
    LlmOffscreenExperimentConfig config = originalConfig;
    config.runRoot = originalConfig.runRoot / (ToolHistoryModeLabel(mode) + "-seed-" + std::to_string(seed));
    std::filesystem::create_directories(config.runRoot / "calls");

    AttemptResult attempt;
    attempt.result.mode = mode;
    attempt.result.seed = seed;
    attempt.result.logRoot = config.runRoot;

    SimulationState state;
    StartRound(state, seed);
    game::BasicAiStrategy localAi;
    std::vector<TurnRecord> records;
    int llmCalls = 0;
    int invalidLlmMoves = 0;
    bool observedCannotBeat = false;
    bool llmAfterCannotBeat = false;

    for (int turn = 1; turn <= config.maxTurnsPerRound && !state.roundOver; ++turn) {
        TurnRecord record;
        if (state.currentPlayer != rules::PlayerId::Ai1) {
            record = MakeLocalRecord(state, turn, state.currentPlayer, localAi);
        } else {
            const std::vector<Candidate> legal = LegalCandidates(state, rules::PlayerId::Ai1);
            if (legal.size() <= 1) {
                record = MakeForcedAi1Record(state, turn, legal, records, localAi);
            } else {
                if (llmCalls >= config.maxLlmCalls) {
                    attempt.result.message = "达到 LLM 调用上限";
                    break;
                }
                std::string error;
                record = MakeLlmRecord(state, turn, ++llmCalls, config, mode, records, localAi, invalidLlmMoves, error);
                if (!error.empty()) {
                    attempt.result.message = error;
                    if (mode == ToolHistoryMode::Loose && llmCalls > 1) {
                        attempt.retryWithStrict = true;
                    }
                    break;
                }
                if (observedCannotBeat && record.source == TurnDecisionSource::LlmAi && record.accepted && record.finalAction.action == "play") {
                    llmAfterCannotBeat = true;
                }
            }
        }

        if (record.reason == TurnDecisionReason::CannotBeat && record.source != TurnDecisionSource::LlmAi) {
            observedCannotBeat = true;
        }
        records.push_back(std::move(record));
    }

    attempt.result.records = std::move(records);
    attempt.result.turns = static_cast<int>(attempt.result.records.size());
    attempt.result.llmCalls = llmCalls;
    attempt.result.invalidLlmMoves = invalidLlmMoves;
    attempt.result.observedCannotBeatSynthetic = observedCannotBeat;
    attempt.result.llmPlayedAfterCannotBeatSynthetic = llmAfterCannotBeat;
    attempt.result.completedRound = state.roundOver;
    attempt.result.ok = state.roundOver && llmCalls > 0 && invalidLlmMoves == 0 && observedCannotBeat && llmAfterCannotBeat;
    if (attempt.result.ok) {
        attempt.result.message = "LLM 离屏全流程通过";
    } else if (attempt.result.message.empty()) {
        attempt.result.message = "未满足全流程观察条件";
    }
    WriteTextFile(attempt.result.logRoot / "turn_records.json", ToJson(attempt.result.records));
    WriteTextFile(attempt.result.logRoot / "summary.json", ToJson(attempt.result));
    return attempt;
}

} // namespace

std::string ToolHistoryModeLabel(ToolHistoryMode mode) {
    return mode == ToolHistoryMode::Loose ? "loose" : "strict";
}

LlmOffscreenExperimentResult RunLlmOffscreenExperiment(const LlmOffscreenExperimentConfig& config) {
    LlmOffscreenExperimentConfig runConfig = config;
    if (runConfig.runRoot.empty()) {
        runConfig.runRoot = std::filesystem::path("build") / "vs2026-release" / "ai-client-runs" / NowStamp();
    }

    LlmOffscreenExperimentResult last;
    bool strictNeeded = false;
    for (unsigned seed : runConfig.seeds) {
        AttemptResult loose = RunAttempt(runConfig, ToolHistoryMode::Loose, seed);
        last = loose.result;
        if (loose.result.ok) {
            return loose.result;
        }
        if (loose.retryWithStrict) {
            strictNeeded = true;
            break;
        }
    }

    if (!strictNeeded) {
        strictNeeded = true;
    }
    if (strictNeeded) {
        for (unsigned seed : runConfig.seeds) {
            AttemptResult strict = RunAttempt(runConfig, ToolHistoryMode::Strict, seed);
            last = strict.result;
            if (strict.result.ok) {
                return strict.result;
            }
        }
    }
    return last;
}

std::string ToJson(const LlmOffscreenExperimentResult& result) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", result.ok);
    cJSON_AddStringToObject(root, "mode", ToolHistoryModeLabel(result.mode).c_str());
    cJSON_AddNumberToObject(root, "seed", result.seed);
    cJSON_AddNumberToObject(root, "turns", result.turns);
    cJSON_AddNumberToObject(root, "llmCalls", result.llmCalls);
    cJSON_AddNumberToObject(root, "invalidLlmMoves", result.invalidLlmMoves);
    cJSON_AddBoolToObject(root, "observedCannotBeatSynthetic", result.observedCannotBeatSynthetic);
    cJSON_AddBoolToObject(root, "llmPlayedAfterCannotBeatSynthetic", result.llmPlayedAfterCannotBeatSynthetic);
    cJSON_AddBoolToObject(root, "completedRound", result.completedRound);
    cJSON_AddStringToObject(root, "message", result.message.c_str());
    cJSON_AddStringToObject(root, "logRoot", result.logRoot.generic_string().c_str());
    char* text = cJSON_Print(root);
    std::string json = text ? text : "";
    cJSON_free(text);
    cJSON_Delete(root);
    return json;
}

std::vector<PdkAiMessage> BuildExperimentMessagesForTest(
    const std::vector<TurnRecord>& records,
    const std::string& currentUserPrompt,
    ToolHistoryMode mode) {
    return BuildMessages(records, currentUserPrompt, mode);
}

std::string BuildCurrentPromptForTest(
    const std::vector<TurnRecord>& records,
    const std::array<rules::Cards, 3>& hands,
    const rules::Cards& lastCards,
    const std::optional<rules::HandPattern>& lastPattern,
    rules::PlayerId lastMovePlayer) {
    return CurrentPrompt(records, hands, lastCards, lastPattern, lastMovePlayer);
}

} // namespace pdk::ai
