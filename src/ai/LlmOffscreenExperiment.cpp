#include "ai/LlmOffscreenExperiment.h"

#include "core/StringUtil.h"
#include "core/WinFile.h"
#include "game/AiStrategy.h"
#include "rules/Deck.h"
#include "rules/MoveValidator.h"
#include "rules/RuleText.h"

#include <cJSON.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>

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
    char text[32]{};
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &tm);
    return text;
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
    std::string out;
    for (std::size_t i = 0; i < ranks.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += ranks[i];
    }
    return out;
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

struct PromptPerspective {
    rules::PlayerId self{rules::PlayerId::Ai1};
    std::string humanName{"玩家"};
};

std::string PromptPlayerLabel(rules::PlayerId player, const PromptPerspective& perspective) {
    if (player == perspective.self) {
        return "你";
    }
    if (player == rules::PlayerId::Player) {
        return perspective.humanName.empty() ? "玩家" : perspective.humanName;
    }
    switch (player) {
    case rules::PlayerId::Ai1:
    case rules::PlayerId::Ai2:
        return "另一名 AI";
    case rules::PlayerId::Player:
        break;
    }
    return "未知玩家";
}

std::string CardsText(const rules::Cards& cards) {
    if (cards.empty()) {
        return "无";
    }
    std::string out;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += rules::RankName(cards[i].rank);
    }
    return out;
}

void RecordPassObservation(game::AiContext& context, const TurnRecord& record) {
    if (record.finalAction.action != "pass" || !record.before.lastPattern) {
        return;
    }

    const int index = Index(record.actor);
    game::PassObservation observation{
        *record.before.lastPattern,
        static_cast<int>(record.before.hands[static_cast<std::size_t>(index)].size())
    };
    std::optional<game::PassObservation>& existing = context.passObservations[static_cast<std::size_t>(index)];
    if (existing && existing->pattern.type == rules::PatternType::Single &&
        observation.pattern.type == rules::PatternType::Single &&
        rules::RankValue(observation.pattern.mainRank) < rules::RankValue(existing->pattern.mainRank)) {
        existing = observation;
    } else if (!existing) {
        existing = observation;
    }
}

game::AiContext PromptAiContext(
    const TurnSnapshot& snapshot,
    const std::vector<TurnRecord>& records,
    rules::PlayerId self) {
    game::AiContext context;
    context.leading = !snapshot.lastPattern.has_value();
    if (snapshot.lastPattern) {
        context.previous = *snapshot.lastPattern;
    }
    const int selfIndex = Index(self);
    context.currentPlayerIndex = selfIndex;
    context.ownRemainingCards = static_cast<int>(snapshot.hands[static_cast<std::size_t>(selfIndex)].size());
    for (int i = 0; i < 3; ++i) {
        context.remainingCards[static_cast<std::size_t>(i)] = static_cast<int>(snapshot.hands[static_cast<std::size_t>(i)].size());
    }
    context.nextPlayerRemainingCards = static_cast<int>(snapshot.hands[static_cast<std::size_t>(Index(NextPlayer(self)))].size());
    context.minOpponentRemainingCards = 99;
    for (int i = 0; i < 3; ++i) {
        if (i != selfIndex) {
            context.minOpponentRemainingCards = std::min(context.minOpponentRemainingCards, context.remainingCards[static_cast<std::size_t>(i)]);
        }
    }
    for (const TurnRecord& record : records) {
        context.playedCards.insert(context.playedCards.end(), record.finalCards.begin(), record.finalCards.end());
        RecordPassObservation(context, record);
    }
    return context;
}

std::string RecommendationText(
    const TurnSnapshot& snapshot,
    const std::vector<TurnRecord>& records,
    const PromptPerspective& perspective) {
    game::BasicAiStrategy localAi;
    const game::AiContext context = PromptAiContext(snapshot, records, perspective.self);
    const std::vector<game::AiMoveChoice> recommendations =
        localAi.RecommendMoves(snapshot.hands[static_cast<std::size_t>(Index(perspective.self))], context, 3);

    std::string out = "本地决策树 AI 的前 3 个建议（仅供参考，最终由你决定）：\n";
    if (recommendations.empty()) {
        out += "- 无。\n";
        return out;
    }
    for (std::size_t i = 0; i < recommendations.size(); ++i) {
        const game::AiMoveChoice& choice = recommendations[i];
        out += "- ";
        core::AppendNumber(out, i + 1);
        out += ". ";
        if (choice.pass) {
            out += "不要";
        } else {
            out += "出 ";
            out += CardsText(choice.cards);
            out += "，牌型 ";
            out += PatternText(choice.pattern);
        }
        out += "，";
        out += choice.reason;
        out += "\n";
    }
    return out;
}

std::string SystemPrompt() {
    std::string out =
        "你正在扮演三人跑得快中的一名 AI 玩家。\n"
        "真实决策时只调用 play_cards 工具。"
        "record_forced_move 只会出现在历史中，用来记录规则强制动作，不是本次可调用的策略选择。\n"
        "固定规则如下：\n";
    out += rules::SharedGameRulesText();
    out += "\n"
        "play_cards 只返回点数，不返回花色；对子、三张、炸弹需要重复点数。"
        "如果你返回非法牌或本地不存在的牌，本地裁判会判定失败。";
    return out;
}

std::string CurrentPrompt(
    const std::vector<TurnRecord>& records,
    const std::array<rules::Cards, 3>& hands,
    const rules::Cards& lastCards,
    const std::optional<rules::HandPattern>& lastPattern,
    rules::PlayerId lastMovePlayer,
    const PromptPerspective& perspective = {}) {
    std::string out = "现在轮到你决策。\n";
    out += "你的手牌: ";
    out += CardsText(hands[Index(rules::PlayerId::Ai1)]);
    out += "\n剩余张数: 玩家=";
    core::AppendNumber(out, hands[0].size());
    out += ", 你=";
    core::AppendNumber(out, hands[1].size());
    out += ", 另一名 AI=";
    core::AppendNumber(out, hands[2].size());
    out += "\n";
    if (lastPattern) {
        out += "当前要压的牌: ";
        out += PromptPlayerLabel(lastMovePlayer, perspective);
        out += " ";
        out += CardsText(lastCards);
        out += "，牌型 ";
        out += PatternText(lastPattern);
        out += "\n";
    } else {
        out += "当前是领出，可以主动选择任意合法牌型。\n";
    }
    TurnSnapshot snapshot;
    snapshot.hands = hands;
    snapshot.lastCards = lastCards;
    snapshot.lastPattern = lastPattern;
    snapshot.lastMovePlayer = lastMovePlayer;
    snapshot.currentPlayer = perspective.self;
    out += RecommendationText(snapshot, records, perspective);

    std::size_t start = 0;
    for (std::size_t i = records.size(); i > 0; --i) {
        if (records[i - 1].actor == rules::PlayerId::Ai1) {
            start = i;
            break;
        }
    }
    out += "从上次你 tool_call 到现在发生的全部公开行动:\n";
    if (start >= records.size()) {
        out += "- 无，你连续决策。\n";
    }
    for (std::size_t i = start; i < records.size(); ++i) {
        const TurnRecord& record = records[i];
        out += "- ";
        core::AppendNumber(out, record.turnNo);
        out += " ";
        out += PromptPlayerLabel(record.actor, perspective);
        out += " ";
        out += MoveText(record.finalAction);
        out += "，来源 ";
        out += SourceLabel(record.source);
        out += "，原因 ";
        out += ReasonLabel(record.reason);
        if (record.finalPattern) {
            out += "，牌型 ";
            out += PatternText(record.finalPattern);
        }
        out += "\n";
    }
    out += "请结合完整历史和当前手牌，只调用 play_cards。";
    return out;
}

std::string CurrentPrompt(const SimulationState& state, const std::vector<TurnRecord>& records) {
    return CurrentPrompt(records, state.hands, state.lastCards, state.lastPattern, state.lastMovePlayer);
}

std::string ArgumentsJson(const PdkAiMove& move) {
    cJSON* root = cJSON_CreateObject();
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

std::string ForcedArgumentsJson(const TurnRecord& record) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "reason", record.reason == TurnDecisionReason::CannotBeat ? "cannot_beat" : "only_legal_move");
    cJSON* ranks = cJSON_CreateArray();
    for (const std::string& rank : record.finalAction.ranks) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(rank.c_str()));
    }
    cJSON_AddItemToObject(root, "ranks", ranks);
    char* text = cJSON_PrintUnformatted(root);
    std::string result = text ? text : "";
    cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

std::string SyntheticReasoning(const TurnRecord& record) {
    std::string out = "本地记录：";
    if (record.reason == TurnDecisionReason::CannotBeat) {
        out += PlayerLabel(record.actor);
        out += " 按规则要不起，只能不要。";
    } else if (record.reason == TurnDecisionReason::OnlyLegalMove) {
        out += PlayerLabel(record.actor);
        out += " 只有一种合法选择，直接执行。";
    } else {
        out += PlayerLabel(record.actor);
        out += " 使用本地基础策略执行 ";
        out += MoveText(record.finalAction);
        out += "。";
    }
    return out;
}

PdkAiMessage SyntheticAssistantMessage(const TurnRecord& record) {
    const std::string id = "synthetic_turn_" + std::to_string(record.turnNo);
    return PdkAiMessage{
        "assistant",
        {},
        record.trace.reasoningContent,
        {},
        {},
        {PdkAiToolCall{id, "record_forced_move", ForcedArgumentsJson(record)}}
    };
}

PdkAiMessage ToolMessageFor(const PdkAiMessage& assistant, const PdkAiMove& move, bool accepted, const std::string& reason) {
    const std::string id = assistant.toolCalls.empty() ? "" : assistant.toolCalls.front().id;
    return PdkAiMessage{
        "tool",
        PdkAiClient::BuildToolResultJson(move, accepted, reason),
        {},
        id,
        assistant.toolCalls.empty() ? "play_cards" : assistant.toolCalls.front().name,
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

std::string CallPath(const std::string& root, int callNo, const char* suffix) {
    std::string name;
    core::AppendPaddedNumber(name, callNo, 4);
    name += "_";
    name += suffix;
    name += ".json";
    return core::JoinPath(core::JoinPath(root, "calls"), name);
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
    rules::Cards deck = rules::CreatePaoDeKuaiDeck();
    rules::Shuffle(deck, seed);
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
    config.runRoot = core::JoinPath(originalConfig.runRoot, ToolHistoryModeLabel(mode) + "-seed-" + std::to_string(seed));
    core::CreateDirectories(core::JoinPath(config.runRoot, "calls"));

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
    core::WriteTextFile(core::JoinPath(attempt.result.logRoot, "turn_records.json"), ToJson(attempt.result.records));
    core::WriteTextFile(core::JoinPath(attempt.result.logRoot, "summary.json"), ToJson(attempt.result));
    return attempt;
}

} // namespace

std::string ToolHistoryModeLabel(ToolHistoryMode mode) {
    return mode == ToolHistoryMode::Loose ? "loose" : "strict";
}

LlmOffscreenExperimentResult RunLlmOffscreenExperiment(const LlmOffscreenExperimentConfig& config) {
    LlmOffscreenExperimentConfig runConfig = config;
    if (runConfig.runRoot.empty()) {
        runConfig.runRoot = core::JoinPath(core::JoinPath(core::JoinPath("build", "vs2026-release"), "ai-client-runs"), NowStamp());
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
    cJSON_AddStringToObject(root, "logRoot", result.logRoot.c_str());
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
