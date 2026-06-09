#include "ai/LlmAiController.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
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
    std::ostringstream out;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << rules::RankName(cards[i].rank);
    }
    return out.str();
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

std::string MoveText(const game::GameAction& action) {
    return action.action == "pass" ? "不要" : "出 " + RankList(action.ranks);
}

std::string PatternText(const std::optional<rules::HandPattern>& pattern) {
    return pattern ? rules::PatternDescription(*pattern) : "无";
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
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d-%H%M%S");
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
        "如果你返回非法牌、本地不存在的牌、或者该打却 pass，本地裁判会改用本地 AI。";
}

std::string CurrentPrompt(const game::ExternalAiRequest& request) {
    const game::TurnSnapshot& snapshot = request.snapshot;
    std::ostringstream out;
    out << "现在轮到 AI1 决策。\n";
    out << "AI1 手牌: " << CardsText(snapshot.hands[Index(rules::PlayerId::Ai1)]) << "\n";
    out << "剩余张数: player=" << snapshot.hands[0].size()
        << ", ai1=" << snapshot.hands[1].size()
        << ", ai2=" << snapshot.hands[2].size() << "\n";
    if (snapshot.lastPattern) {
        out << "当前要压的牌: " << game::PlayerLabel(snapshot.lastMovePlayer) << " "
            << CardsText(snapshot.lastCards) << "，牌型 " << PatternText(snapshot.lastPattern) << "\n";
    } else {
        out << "当前是领出，可以主动选择任意合法牌型。\n";
    }

    std::size_t start = 0;
    for (std::size_t i = request.history.size(); i > 0; --i) {
        if (request.history[i - 1].actor == rules::PlayerId::Ai1) {
            start = i;
            break;
        }
    }
    out << "从上次 AI1 tool_call 到现在发生的全部公开行动:\n";
    if (start >= request.history.size()) {
        out << "- 无，AI1 连续决策。\n";
    }
    for (std::size_t i = start; i < request.history.size(); ++i) {
        const game::TurnRecord& record = request.history[i];
        out << "- " << record.turnNo << " " << game::PlayerLabel(record.actor) << " "
            << MoveText(record.finalAction) << "，来源 " << game::SourceLabel(record.source)
            << "，原因 " << game::ReasonLabel(record.reason);
        if (record.finalPattern) {
            out << "，牌型 " << PatternText(record.finalPattern);
        }
        out << "\n";
    }
    out << "请结合完整历史和当前手牌，只调用 choose_move。";
    return out.str();
}

std::vector<PdkAiMessage> BuildMessages(const game::ExternalAiRequest& request) {
    std::vector<PdkAiMessage> messages;
    messages.push_back(PdkAiMessage{"system", SystemPrompt(), {}, {}, {}, {}});
    for (const game::TurnRecord& record : request.history) {
        if (record.actor != rules::PlayerId::Ai1 || record.trace.toolCallId.empty()) {
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
            {PdkAiToolCall{record.trace.toolCallId, "choose_move", record.trace.toolArgumentsJson}}
        });
    }
    messages.push_back(PdkAiMessage{"user", CurrentPrompt(request), {}, {}, {}, {}});
    return messages;
}

std::filesystem::path CallPath(const std::filesystem::path& root, int requestId, const char* suffix) {
    std::ostringstream name;
    name << std::setw(4) << std::setfill('0') << requestId << "_" << suffix << ".json";
    return root / "calls" / name.str();
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

LlmAiController::LlmAiController(stats::AiProviderSettings provider)
    : provider_(std::move(provider)),
      state_(std::make_shared<SharedState>()),
      runRoot_(std::filesystem::path("build") / "vs2026-release" / "ai-ui-runs" / NowStamp()) {}

LlmAiController::~LlmAiController() {
    Cancel();
}

bool LlmAiController::CanHandle(rules::PlayerId player) const {
    return player == rules::PlayerId::Ai1 &&
        provider_.type == "openai" &&
        !provider_.endpoint.empty() &&
        !provider_.apiKey.empty() &&
        !provider_.model.empty();
}

bool LlmAiController::HasPending() const {
    std::lock_guard lock(state_->mutex);
    return state_->pending;
}

void LlmAiController::Start(game::ExternalAiRequest request) {
    std::shared_ptr<SharedState> state = state_;
    stats::AiProviderSettings provider = provider_;
    const std::filesystem::path requestLog = CallPath(runRoot_, request.turnNo, "request");
    const std::filesystem::path responseLog = CallPath(runRoot_, request.turnNo, "response");
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
