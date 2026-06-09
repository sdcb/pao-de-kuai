#include <doctest/doctest.h>

#include "ai/LlmOffscreenExperiment.h"
#include "ai/PdkAiClient.h"
#include "ai/WinHttpJsonClient.h"
#include "stats/AppSettings.h"
#include "rules/Card.h"
#include "rules/HandPattern.h"
#include "rules/RuleText.h"

#include <cJSON.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace pdk;

namespace {

struct JsonDeleter {
    void operator()(cJSON* value) const {
        cJSON_Delete(value);
    }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "pao_de_kuai_ai_client_tests";
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

ai::PdkAiRequest SampleRequest() {
    stats::AiProviderSettings provider;
    provider.type = "openai";
    provider.endpoint = "https://example.invalid/v1/chat/completions";
    provider.apiKey = "fake-secret-key";
    provider.model = "mimo-test";
    return ai::PdkAiRequest{
        provider,
        {
            ai::PdkAiMessage{"system", "你是跑得快 AI。", {}, {}, {}, {}},
            ai::PdkAiMessage{"user", "请出牌。", {}, {}, {}, {}}
        },
        {},
        {}
    };
}

rules::Card C(rules::Rank rank, rules::Suit suit = rules::Suit::Spades) {
    return {rank, suit};
}

ai::TurnSnapshot SnapshotFor(
    rules::Cards ai1Hand,
    std::optional<rules::HandPattern> lastPattern = std::nullopt,
    rules::Cards lastCards = {},
    rules::PlayerId lastMovePlayer = rules::PlayerId::Player) {
    ai::TurnSnapshot snapshot;
    snapshot.hands = {
        rules::Cards{C(rules::Rank::Three)},
        std::move(ai1Hand),
        rules::Cards{C(rules::Rank::Five)}
    };
    snapshot.lastPattern = lastPattern;
    snapshot.lastCards = std::move(lastCards);
    snapshot.lastMovePlayer = lastMovePlayer;
    snapshot.currentPlayer = rules::PlayerId::Ai1;
    return snapshot;
}

ai::TurnRecord Record(
    int turnNo,
    rules::PlayerId actor,
    ai::TurnDecisionSource source,
    ai::TurnDecisionReason reason,
    ai::PdkAiMove move,
    const ai::TurnSnapshot& before) {
    ai::TurnRecord record;
    record.turnNo = turnNo;
    record.actor = actor;
    record.source = source;
    record.reason = reason;
    record.before = before;
    record.after = before;
    record.finalAction = move;
    record.requestedAction = move;
    record.trace.reasoningContent = "reasoning turn " + std::to_string(turnNo);
    const bool llmChoice = source == ai::TurnDecisionSource::LlmAi;
    const std::string toolName = llmChoice ? "play_cards" : "record_forced_move";
    std::string arguments;
    if (llmChoice) {
        arguments = "{\"ranks\":[";
        for (std::size_t i = 0; i < move.ranks.size(); ++i) {
            if (i != 0) {
                arguments += ",";
            }
            arguments += "\"" + move.ranks[i] + "\"";
        }
        arguments += "]}";
    } else {
        arguments = std::string("{\"reason\":\"") +
            (reason == ai::TurnDecisionReason::CannotBeat ? "cannot_beat" : "only_legal_move") +
            "\",\"ranks\":[]}";
    }
    record.trace.assistantMessage = ai::PdkAiMessage{
        "assistant",
        {},
        record.trace.reasoningContent,
        {},
        {},
        {ai::PdkAiToolCall{"call_" + std::to_string(turnNo), toolName, arguments}}
    };
    record.trace.toolMessage = ai::PdkAiMessage{
        "tool",
        "{\"accepted\":true}",
        {},
        "call_" + std::to_string(turnNo),
        toolName,
        {}
    };
    return record;
}

std::string FakeResponse(const char* reasoningKey = "reasoning_content") {
    std::string response = R"({
        "choices": [
            {
                "message": {
                    ")";
    response += reasoningKey;
    response += R"(": "我选择单张 7 测试工具调用。",
                    "tool_calls": [
                        {
                            "id": "call_1",
                            "type": "function",
                            "function": {
                                "name": "play_cards",
                                "arguments": "{\"ranks\":[\"7\"],\"talk\":\"先走一张。\"}"
                            }
                        }
                    ]
                }
            }
        ]
    })";
    return response;
}

} // namespace

TEST_CASE("appsettings loads and preserves AI provider configuration") {
    const auto root = TestRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto path = root / "settings.json";

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << R"({
            "playerName": "Tester",
            "masterVolume": 0.25,
            "windowWidth": 1600,
            "windowHeight": 900,
            "ai1": "local",
            "ai2": "mimo",
            "aiProviders": {
                "mimo": {
                    "type": "openai",
                    "endpoint": "https://example.invalid/v1/chat/completions",
                    "apiKey": "fake-secret-key",
                    "model": "mimo-test"
                }
            }
        })";
    }

    const stats::AppSettings loaded = stats::LoadAppSettings(path.string());
    REQUIRE(loaded.aiProviders.count("mimo") == 1);
    CHECK(loaded.aiProviders.at("mimo").type == "openai");
    CHECK(loaded.aiProviders.at("mimo").apiKey == "fake-secret-key");
    CHECK(loaded.ai1 == "local");
    CHECK(loaded.ai2 == "mimo");

    REQUIRE(stats::SaveAppSettings(loaded, path.string()));
    const std::string saved = ReadFile(path);
    CHECK(saved.find("\"aiProviders\"") != std::string::npos);
    CHECK(saved.find("\"ai1\"") != std::string::npos);
    CHECK(saved.find("\"ai2\"") != std::string::npos);
    CHECK(saved.find("cardScale") == std::string::npos);
    CHECK(saved.find("animationSpeed") == std::string::npos);
}

TEST_CASE("pdk AI request JSON uses play cards and forced history tools") {
    const ai::PdkAiRequest request = SampleRequest();
    const std::string body = ai::PdkAiClient::BuildRequestJson(request);
    REQUIRE_FALSE(body.empty());
    CHECK(body == ai::PdkAiClient::BuildRequestJson(request));
    CHECK_FALSE(body.find(request.provider.apiKey) != std::string::npos);

    JsonPtr root(cJSON_Parse(body.c_str()));
    REQUIRE(root != nullptr);
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(root.get(), "model")->valuestring) == "mimo-test");
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root.get(), "stream")));

    const cJSON* tools = cJSON_GetObjectItemCaseSensitive(root.get(), "tools");
    REQUIRE(cJSON_IsArray(tools));
    REQUIRE(cJSON_GetArraySize(tools) == 2);

    const cJSON* playFunction = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(tools, 0), "function");
    REQUIRE(cJSON_IsObject(playFunction));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(playFunction, "name")->valuestring) == "play_cards");
    const cJSON* playParameters = cJSON_GetObjectItemCaseSensitive(playFunction, "parameters");
    const cJSON* playProperties = cJSON_GetObjectItemCaseSensitive(playParameters, "properties");
    CHECK(cJSON_GetObjectItemCaseSensitive(playProperties, "action") == nullptr);
    CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(playProperties, "ranks")));
    const cJSON* talk = cJSON_GetObjectItemCaseSensitive(playProperties, "talk");
    REQUIRE(cJSON_IsObject(talk));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(talk, "description")->valuestring).find("不要透露你的手牌") != std::string::npos);
    CHECK(cJSON_GetObjectItemCaseSensitive(talk, "maxLength")->valueint == 24);

    const cJSON* forcedFunction = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(tools, 1), "function");
    REQUIRE(cJSON_IsObject(forcedFunction));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(forcedFunction, "name")->valuestring) == "record_forced_move");
    const cJSON* forcedParameters = cJSON_GetObjectItemCaseSensitive(forcedFunction, "parameters");
    const cJSON* forcedProperties = cJSON_GetObjectItemCaseSensitive(forcedParameters, "properties");
    CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(forcedProperties, "reason")));
    CHECK(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(forcedProperties, "ranks")));

    const cJSON* toolChoice = cJSON_GetObjectItemCaseSensitive(root.get(), "tool_choice");
    const cJSON* choiceFunction = cJSON_GetObjectItemCaseSensitive(toolChoice, "function");
    REQUIRE(cJSON_IsObject(choiceFunction));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(choiceFunction, "name")->valuestring) == "play_cards");
}

TEST_CASE("pdk AI request can replay strict tool history") {
    ai::PdkAiRequest request = SampleRequest();
    request.messages.insert(request.messages.begin() + 1, ai::PdkAiMessage{
        "user",
        "记录上一手：AI2 不要。",
        {},
        {},
        {},
        {}
    });
    request.messages.insert(request.messages.begin() + 2, ai::PdkAiMessage{
        "assistant",
        {},
        "本地系统判断 AI2 要不起。",
        {},
        {},
        {ai::PdkAiToolCall{"synthetic_1", "record_forced_move", "{\"reason\":\"cannot_beat\",\"ranks\":[]}"}}
    });
    request.messages.insert(request.messages.begin() + 3, ai::PdkAiMessage{
        "tool",
        "{\"accepted\":true}",
        {},
        "synthetic_1",
        "record_forced_move",
        {}
    });

    JsonPtr root(cJSON_Parse(ai::PdkAiClient::BuildRequestJson(request).c_str()));
    REQUIRE(root != nullptr);
    const cJSON* messages = cJSON_GetObjectItemCaseSensitive(root.get(), "messages");
    REQUIRE(cJSON_IsArray(messages));
    CHECK(cJSON_GetArraySize(messages) == 5);
    const cJSON* assistant = cJSON_GetArrayItem(messages, 2);
    CHECK(cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(assistant, "tool_calls")));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(assistant, "reasoning_content")->valuestring) == "本地系统判断 AI2 要不起。");
}

TEST_CASE("experiment history replays only AI1 tool calls") {
    const auto before = SnapshotFor({C(rules::Rank::Seven), C(rules::Rank::Eight)});
    std::vector<ai::TurnRecord> records{
        Record(1, rules::PlayerId::Ai1, ai::TurnDecisionSource::LlmAi, ai::TurnDecisionReason::NormalChoice, {"play", {"7"}, {}}, before),
        Record(2, rules::PlayerId::Ai2, ai::TurnDecisionSource::LocalAi, ai::TurnDecisionReason::CannotBeat, {"pass", {}, {}}, before),
        Record(3, rules::PlayerId::Player, ai::TurnDecisionSource::LocalAi, ai::TurnDecisionReason::NormalChoice, {"play", {"9"}, {}}, before)
    };

    const std::vector<ai::PdkAiMessage> messages =
        ai::BuildExperimentMessagesForTest(records, "当前轮到你。", ai::ToolHistoryMode::Loose);

    int assistantToolCalls = 0;
    for (const ai::PdkAiMessage& message : messages) {
        if (message.role == "assistant") {
            assistantToolCalls += static_cast<int>(message.toolCalls.size());
        }
    }
    CHECK(assistantToolCalls == 1);
    CHECK(messages.front().role == "system");
    CHECK(messages[1].role == "user");
    CHECK(messages[2].role == "assistant");
    CHECK(messages.back().content == "当前轮到你。");
}

TEST_CASE("experiment system prompt uses shared help rule text") {
    const std::vector<ai::PdkAiMessage> messages =
        ai::BuildExperimentMessagesForTest({}, "当前轮到你。", ai::ToolHistoryMode::Loose);

    REQUIRE_FALSE(messages.empty());
    CHECK(messages.front().role == "system");
    CHECK(messages.front().content.find(std::string(rules::SharedGameRulesText())) != std::string::npos);
    CHECK(messages.front().content.find(std::string(rules::HumanHelpText())) == std::string::npos);
    CHECK(messages.front().content.find("中文思考") == std::string::npos);
    CHECK(messages.front().content.find("帮助菜单") == std::string::npos);
    CHECK(messages.front().content.find("choose_move") == std::string::npos);
    CHECK(messages.front().content.find("play_cards") != std::string::npos);
}

TEST_CASE("experiment prompt includes all actions after previous self tool call only") {
    const auto before = SnapshotFor({C(rules::Rank::Seven), C(rules::Rank::Eight)});
    std::vector<ai::TurnRecord> records{
        Record(1, rules::PlayerId::Ai1, ai::TurnDecisionSource::LlmAi, ai::TurnDecisionReason::NormalChoice, {"play", {"7"}, {}}, before),
        Record(2, rules::PlayerId::Ai2, ai::TurnDecisionSource::LocalAi, ai::TurnDecisionReason::CannotBeat, {"pass", {}, {}}, before),
        Record(3, rules::PlayerId::Player, ai::TurnDecisionSource::LocalAi, ai::TurnDecisionReason::NormalChoice, {"play", {"9"}, {}}, before)
    };
    const std::string prompt = ai::BuildCurrentPromptForTest(
        records,
        before.hands,
        {},
        std::nullopt,
        rules::PlayerId::Ai1);

    CHECK(prompt.find("从上次你 tool_call 到现在发生的全部公开行动") != std::string::npos);
    CHECK(prompt.find("本地决策树 AI 的前 3 个建议") != std::string::npos);
    CHECK(prompt.find("1 你") == std::string::npos);
    CHECK(prompt.find("2 另一名 AI 不要") != std::string::npos);
    CHECK(prompt.find("3 玩家 出 9") != std::string::npos);
}

TEST_CASE("experiment replayed historical prompts are deterministic from TurnRecord prefix") {
    auto firstBefore = SnapshotFor({C(rules::Rank::Seven), C(rules::Rank::Eight)});
    auto secondBefore = SnapshotFor({C(rules::Rank::Eight)}, rules::IdentifyPattern({C(rules::Rank::Five)}).pattern, {C(rules::Rank::Five)}, rules::PlayerId::Player);
    std::vector<ai::TurnRecord> prefix{
        Record(1, rules::PlayerId::Ai1, ai::TurnDecisionSource::LlmAi, ai::TurnDecisionReason::NormalChoice, {"play", {"7"}, {}}, firstBefore),
        Record(2, rules::PlayerId::Ai2, ai::TurnDecisionSource::LocalAi, ai::TurnDecisionReason::CannotBeat, {"pass", {}, {}}, firstBefore)
    };
    std::vector<ai::TurnRecord> extended = prefix;
    extended.push_back(Record(3, rules::PlayerId::Ai1, ai::TurnDecisionSource::LlmAi, ai::TurnDecisionReason::NormalChoice, {"play", {"8"}, {}}, secondBefore));

    const std::vector<ai::PdkAiMessage> prefixMessages =
        ai::BuildExperimentMessagesForTest(prefix, "当前 prompt A", ai::ToolHistoryMode::Strict);
    const std::vector<ai::PdkAiMessage> extendedMessages =
        ai::BuildExperimentMessagesForTest(extended, "当前 prompt B", ai::ToolHistoryMode::Strict);

    REQUIRE(prefixMessages.size() >= 2);
    REQUIRE(extendedMessages.size() >= prefixMessages.size() + 3);
    CHECK(prefixMessages[1].role == "user");
    CHECK(extendedMessages[1].role == "user");
    CHECK(prefixMessages[1].content == extendedMessages[1].content);
}

TEST_CASE("pdk AI response parser returns ranks and reasoning_content") {
    const ai::PdkAiResponse response = ai::PdkAiClient::ParseResponse(FakeResponse());
    REQUIRE(response.ok);
    CHECK(response.move.action == "play");
    REQUIRE(response.move.ranks.size() == 1);
    CHECK(response.move.ranks[0] == "7");
    CHECK(response.move.talk == "先走一张。");
    CHECK(response.reasoningContent == "我选择单张 7 测试工具调用。");
    REQUIRE(response.assistantMessage.toolCalls.size() == 1);
    CHECK(response.assistantMessage.toolCalls[0].id == "call_1");
}

TEST_CASE("pdk AI response parser rejects camelCase reasoning fallback") {
    const ai::PdkAiResponse response = ai::PdkAiClient::ParseResponse(FakeResponse("reasoningContent"));
    CHECK_FALSE(response.ok);
    CHECK(response.errorMessage.find("reasoning_content") != std::string::npos);
}

TEST_CASE("winhttp debug log omits request headers and stays parseable") {
    const auto root = TestRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto logPath = root / "redaction-log.json";

    const ai::HttpJsonResponse response = ai::WinHttpJsonClient().Post(ai::HttpJsonRequest{
        "not-a-valid-url",
        "fake-secret-key",
        R"({"hello":"world"})",
        logPath,
        1000
    });
    CHECK_FALSE(response.ok);

    const std::string log = ReadFile(logPath);
    CHECK(log.find("requestHeaders") == std::string::npos);
    CHECK(log.find("Authorization") == std::string::npos);

    JsonPtr rootJson(cJSON_Parse(log.c_str()));
    CHECK(rootJson != nullptr);
}
