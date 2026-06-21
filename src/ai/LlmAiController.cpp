#include "ai/LlmAiController.h"

#include "core/StringUtil.h"
#include "core/WinFile.h"
#include "game/AiStrategy.h"
#include "rules/RuleText.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <thread>

namespace pdk::ai {
namespace {

int Index(rules::PlayerId player) {
    return rules::PlayerIndex(player);
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

std::string MoveText(const game::GameAction& action) {
    return action.action == "pass" ? "不要" : "出 " + RankList(action.ranks);
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

void RecordPassObservation(game::AiContext& context, const game::TurnRecord& record) {
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

rules::PlayerId NextPlayer(rules::PlayerId player) {
    return rules::PlayerFromIndex((Index(player) + 2) % 3);
}

game::AiContext PromptAiContext(
    const game::TurnSnapshot& snapshot,
    const std::vector<game::TurnRecord>& records,
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
    for (const game::TurnRecord& record : records) {
        context.playedCards.insert(context.playedCards.end(), record.finalCards.begin(), record.finalCards.end());
        RecordPassObservation(context, record);
    }
    return context;
}

std::string RecommendationText(
    const game::TurnSnapshot& snapshot,
    const std::vector<game::TurnRecord>& records,
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

std::string SystemPrompt() {
    std::string out =
        "你正在扮演三人跑得快中的一名 AI 玩家。\n"
        "真实决策时只调用 play_cards 工具。"
        "record_forced_move 只会出现在历史中，用来记录规则强制动作，不是本次可调用的策略选择。\n"
        "固定规则如下：\n";
    out += rules::SharedGameRulesText();
    out += "\n"
        "play_cards 只返回点数，不返回花色；对子、三张、炸弹需要重复点数。"
        "如果你返回非法牌或本地不存在的牌，本地裁判会改用本地 AI。";
    return out;
}

std::string CurrentPrompt(const game::ExternalAiRequest& request) {
    const game::TurnSnapshot& snapshot = request.snapshot;
    const PromptPerspective perspective{request.player, request.humanName};
    std::string out = "现在轮到你决策。\n";
    out += "你的手牌: ";
    out += CardsText(snapshot.hands[Index(request.player)]);
    out += "\n剩余张数: ";
    out += PromptPlayerLabel(rules::PlayerId::Player, perspective);
    out += "=";
    core::AppendNumber(out, snapshot.hands[0].size());
    out += ", 你=";
    core::AppendNumber(out, snapshot.hands[Index(request.player)].size());
    out += ", 另一名 AI=";
    core::AppendNumber(out, snapshot.hands[Index(request.player == rules::PlayerId::Ai1 ? rules::PlayerId::Ai2 : rules::PlayerId::Ai1)].size());
    out += "\n";
    if (snapshot.lastPattern) {
        out += "当前要压的牌: ";
        out += PromptPlayerLabel(snapshot.lastMovePlayer, perspective);
        out += " ";
        out += CardsText(snapshot.lastCards);
        out += "，牌型 ";
        out += PatternText(snapshot.lastPattern);
        out += "\n";
    } else {
        out += "当前是领出，可以主动选择任意合法牌型。\n";
    }
    out += RecommendationText(snapshot, request.history, perspective);

    std::size_t start = 0;
    for (std::size_t i = request.history.size(); i > 0; --i) {
        if (request.history[i - 1].actor == request.player && !request.history[i - 1].trace.toolCallId.empty()) {
            start = i;
            break;
        }
    }
    out += "从上次你 tool_call 到现在发生的全部公开行动:\n";
    if (start >= request.history.size()) {
        out += "- 无，你连续决策。\n";
    }
    for (std::size_t i = start; i < request.history.size(); ++i) {
        const game::TurnRecord& record = request.history[i];
        out += "- ";
        core::AppendNumber(out, record.turnNo);
        out += " ";
        out += PromptPlayerLabel(record.actor, perspective);
        out += " ";
        out += MoveText(record.finalAction);
        out += "，来源 ";
        out += game::SourceLabel(record.source);
        out += "，原因 ";
        out += game::ReasonLabel(record.reason);
        if (record.finalPattern) {
            out += "，牌型 ";
            out += PatternText(record.finalPattern);
        }
        out += "\n";
    }
    out += "请结合完整历史和当前手牌，只调用 play_cards。";
    return out;
}

std::vector<PdkAiMessage> BuildMessages(const game::ExternalAiRequest& request) {
    std::vector<PdkAiMessage> messages;
    messages.push_back(PdkAiMessage{"system", SystemPrompt(), {}, {}, {}, {}});
    for (const game::TurnRecord& record : request.history) {
        if (record.actor != request.player || record.trace.toolCallId.empty()) {
            continue;
        }

        game::ExternalAiRequest historical = request;
        const auto count = static_cast<std::size_t>(&record - request.history.data());
        historical.snapshot = record.before;
        historical.history.assign(request.history.begin(), request.history.begin() + static_cast<std::ptrdiff_t>(count));
        messages.push_back(PdkAiMessage{"user", CurrentPrompt(historical), {}, {}, {}, {}});
        messages.push_back(PdkAiMessage{
            "assistant",
            {},
            record.trace.reasoningContent,
            {},
            {},
            {PdkAiToolCall{record.trace.toolCallId, record.trace.toolName.empty() ? "play_cards" : record.trace.toolName, record.trace.toolArgumentsJson}}
        });
    }
    messages.push_back(PdkAiMessage{"user", CurrentPrompt(request), {}, {}, {}, {}});
    return messages;
}

std::string CallPath(const std::string& root, int requestId, const char* suffix) {
    std::string name;
    core::AppendPaddedNumber(name, requestId, 4);
    name += "_";
    name += suffix;
    name += ".json";
    return core::JoinPath(root, name);
}

game::ExternalAiResult ConvertResult(const PdkAiResponse& response) {
    game::ExternalAiResult result;
    result.ok = response.ok;
    result.reasoningContent = response.reasoningContent;
    result.errorMessage = response.errorMessage;
    result.requestedAction = game::GameAction{
        response.move.action,
        response.move.ranks,
        response.move.talk
    };
    if (!response.assistantMessage.toolCalls.empty()) {
        const PdkAiToolCall& call = response.assistantMessage.toolCalls.front();
        result.toolCallId = call.id;
        result.toolName = call.name;
        result.toolArgumentsJson = call.argumentsJson;
    }
    return result;
}

} // namespace

struct LlmAiController::SharedState {
    mutable std::mutex mutex;
    int generation{0};
    bool pending{false};
    std::optional<game::ExternalAiResult> result;
};

LlmAiController::LlmAiController(std::map<rules::PlayerId, stats::AiProviderSettings> providers)
    : providers_(std::move(providers)),
      state_(std::make_shared<SharedState>()),
      runRoot_(core::JoinPath("ai-ui-runs", NowStamp())) {}

LlmAiController::~LlmAiController() {
    Cancel();
}

bool LlmAiController::CanHandle(rules::PlayerId player) const {
    const auto it = providers_.find(player);
    if (it == providers_.end()) {
        return false;
    }
    const stats::AiProviderSettings& provider = it->second;
    return provider.type == "openai" &&
        !provider.endpoint.empty() &&
        !provider.apiKey.empty() &&
        !provider.model.empty();
}

bool LlmAiController::IsRemote(rules::PlayerId player) const {
    return CanHandle(player);
}

bool LlmAiController::HasPending() const {
    std::lock_guard lock(state_->mutex);
    return state_->pending;
}

void LlmAiController::Start(game::ExternalAiRequest request) {
    std::shared_ptr<SharedState> state = state_;
    const auto providerIt = providers_.find(request.player);
    if (providerIt == providers_.end()) {
        game::ExternalAiResult failed;
        failed.errorMessage = "No remote AI provider configured for player";
        std::lock_guard lock(state->mutex);
        state->pending = false;
        state->result = std::move(failed);
        return;
    }
    stats::AiProviderSettings provider = providerIt->second;
    const std::string requestLog = CallPath(runRoot_, request.turnNo, "request");
    const std::string responseLog = CallPath(runRoot_, request.turnNo, "response");
    int generation = 0;
    {
        std::lock_guard lock(state->mutex);
        generation = ++state->generation;
        state->pending = true;
        state->result.reset();
    }

    std::thread([state, provider = std::move(provider), request = std::move(request), requestLog, responseLog, generation]() mutable {
        PdkAiResponse response = PdkAiClient().ChooseMove(PdkAiRequest{
            provider,
            BuildMessages(request),
            requestLog,
            responseLog,
            30000
        });
        game::ExternalAiResult result = ConvertResult(response);
        result.requestLogPath = requestLog;
        result.responseLogPath = responseLog;

        std::lock_guard lock(state->mutex);
        if (state->generation == generation) {
            state->pending = false;
            state->result = std::move(result);
        }
    }).detach();
}

std::optional<game::ExternalAiResult> LlmAiController::TryGetResult() {
    std::lock_guard lock(state_->mutex);
    if (!state_->result) {
        return std::nullopt;
    }
    std::optional<game::ExternalAiResult> result = std::move(state_->result);
    state_->result.reset();
    return result;
}

void LlmAiController::Cancel() {
    std::lock_guard lock(state_->mutex);
    ++state_->generation;
    state_->pending = false;
    state_->result.reset();
}

} // namespace pdk::ai
