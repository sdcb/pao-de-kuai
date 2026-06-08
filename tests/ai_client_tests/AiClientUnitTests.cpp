#include <doctest/doctest.h>

#include "ai/PdkAiClient.h"
#include "ai/WinHttpJsonClient.h"
#include "stats/AppSettings.h"

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

std::string FakeResponseWithReasoning(const char* reasoningKey) {
    std::string response = R"({
        "choices": [
            {
                "message": {
                    ")";
    response += reasoningKey;
    response += R"(": "because option 1 keeps the hand flexible",
                    "tool_calls": [
                        {
                            "type": "function",
                            "function": {
                                "name": "choose_move",
                                "arguments": "{\"index\":1,\"talk\":\"ship it\"}"
                            }
                        }
                    ]
                }
            }
        ]
    })";
    return response;
}

std::vector<ai::PdkAiOption> SampleOptions() {
    return {
        {0, "Pass"},
        {1, "Play single 7"},
        {2, "Play pair 9"}
    };
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
    CHECK(loaded.aiProviders.at("mimo").endpoint == "https://example.invalid/v1/chat/completions");
    CHECK(loaded.aiProviders.at("mimo").apiKey == "fake-secret-key");
    CHECK(loaded.aiProviders.at("mimo").model == "mimo-test");

    REQUIRE(stats::SaveAppSettings(loaded, path.string()));
    const std::string saved = ReadFile(path);
    CHECK(saved.find("\"aiProviders\"") != std::string::npos);
    CHECK(saved.find("\"mimo\"") != std::string::npos);
    CHECK(saved.find("\"apiKey\"") != std::string::npos);
    CHECK(saved.find("cardScale") == std::string::npos);
    CHECK(saved.find("animationSpeed") == std::string::npos);
}

TEST_CASE("pdk AI request JSON uses chat completions tool call schema") {
    stats::AiProviderSettings provider;
    provider.type = "openai";
    provider.endpoint = "https://example.invalid/v1/chat/completions";
    provider.apiKey = "fake-secret-key";
    provider.model = "mimo-test";

    const ai::PdkAiRequest request{
        provider,
        "You are a Pao De Kuai AI.",
        "Current hand: 3 4 5.",
        SampleOptions(),
        {}
    };
    const std::string body = ai::PdkAiClient::BuildRequestJson(request);
    REQUIRE_FALSE(body.empty());
    CHECK(body == ai::PdkAiClient::BuildRequestJson(request));
    CHECK_FALSE(body.find(provider.apiKey) != std::string::npos);

    JsonPtr root(cJSON_Parse(body.c_str()));
    REQUIRE(root != nullptr);
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(root.get(), "model")->valuestring) == "mimo-test");
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root.get(), "stream")));
    CHECK(cJSON_GetObjectItemCaseSensitive(root.get(), "temperature")->valuedouble == doctest::Approx(0.0));

    const cJSON* messages = cJSON_GetObjectItemCaseSensitive(root.get(), "messages");
    REQUIRE(cJSON_IsArray(messages));
    CHECK(cJSON_GetArraySize(messages) == 2);

    const cJSON* tools = cJSON_GetObjectItemCaseSensitive(root.get(), "tools");
    REQUIRE(cJSON_IsArray(tools));
    const cJSON* tool = cJSON_GetArrayItem(tools, 0);
    const cJSON* function = cJSON_GetObjectItemCaseSensitive(tool, "function");
    REQUIRE(cJSON_IsObject(function));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(function, "name")->valuestring) == "choose_move");

    const cJSON* toolChoice = cJSON_GetObjectItemCaseSensitive(root.get(), "tool_choice");
    REQUIRE(cJSON_IsObject(toolChoice));
    const cJSON* choiceFunction = cJSON_GetObjectItemCaseSensitive(toolChoice, "function");
    REQUIRE(cJSON_IsObject(choiceFunction));
    CHECK(std::string(cJSON_GetObjectItemCaseSensitive(choiceFunction, "name")->valuestring) == "choose_move");
}

TEST_CASE("pdk AI response parser returns selected index talk and reasoning content") {
    const ai::PdkAiResponse response =
        ai::PdkAiClient::ParseResponse(FakeResponseWithReasoning("reasoning_content"), SampleOptions());
    REQUIRE(response.ok);
    CHECK(response.selectedIndex == 1);
    CHECK(response.talk == "ship it");
    CHECK(response.reasoningContent == "because option 1 keeps the hand flexible");
}

TEST_CASE("pdk AI response parser accepts camelCase reasoning fallback") {
    const ai::PdkAiResponse response =
        ai::PdkAiClient::ParseResponse(FakeResponseWithReasoning("reasoningContent"), SampleOptions());
    REQUIRE(response.ok);
    CHECK(response.reasoningContent == "because option 1 keeps the hand flexible");
}

TEST_CASE("pdk AI response parser rejects missing or invalid tool choices") {
    const ai::PdkAiResponse missing = ai::PdkAiClient::ParseResponse(R"({
        "choices": [{"message": {"content": "no call"}}]
    })", SampleOptions());
    CHECK_FALSE(missing.ok);
    CHECK_FALSE(missing.errorMessage.empty());

    const ai::PdkAiResponse outOfRange = ai::PdkAiClient::ParseResponse(R"({
        "choices": [{
            "message": {
                "tool_calls": [{
                    "type": "function",
                    "function": {
                        "name": "choose_move",
                        "arguments": "{\"index\":99}"
                    }
                }]
            }
        }]
    })", SampleOptions());
    CHECK_FALSE(outOfRange.ok);
    CHECK_FALSE(outOfRange.errorMessage.empty());
}

TEST_CASE("winhttp debug log redacts bearer token and stays parseable") {
    const auto root = TestRoot();
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto logPath = root / "redaction-log.json";
    const std::string secret = "fake-secret-key";

    const ai::HttpJsonResponse response = ai::WinHttpJsonClient().Post(ai::HttpJsonRequest{
        "not-a-valid-url",
        secret,
        R"({"hello":"world"})",
        logPath,
        1000
    });
    CHECK_FALSE(response.ok);
    CHECK_FALSE(response.errorMessage.find(secret) != std::string::npos);

    const std::string log = ReadFile(logPath);
    CHECK(log.find("Bearer ***") != std::string::npos);
    CHECK_FALSE(log.find(secret) != std::string::npos);

    JsonPtr rootJson(cJSON_Parse(log.c_str()));
    CHECK(rootJson != nullptr);
}
