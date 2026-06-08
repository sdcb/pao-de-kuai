#include <doctest/doctest.h>

#include "ai/PdkAiClient.h"
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

std::vector<ai::PdkAiOption> SampleOptions() {
    return {
        {0, "Pass"},
        {1, "Play single 7"},
        {2, "Play pair 9"}
    };
}

} // namespace

TEST_CASE("Mimo chat completions integration chooses one option") {
    const stats::AppSettings settings = stats::LoadAppSettings("appsettings.json");
    REQUIRE_MESSAGE(settings.aiProviders.count("mimo") == 1, "missing aiProviders.mimo in appsettings.json");
    const stats::AiProviderSettings provider = settings.aiProviders.at("mimo");
    REQUIRE_MESSAGE(provider.type == "openai", "aiProviders.mimo.type must be openai");
    REQUIRE_MESSAGE(!provider.endpoint.empty(), "aiProviders.mimo.endpoint is empty");
    REQUIRE_MESSAGE(!provider.apiKey.empty(), "aiProviders.mimo.apiKey is empty");
    REQUIRE_MESSAGE(!provider.model.empty(), "aiProviders.mimo.model is empty");

    const auto logPath = TestRoot() / "mimo-chat-completions-last.json";
    const ai::PdkAiResponse response = ai::PdkAiClient().Choose(ai::PdkAiRequest{
        provider,
        "You choose one candidate move for a Pao De Kuai player.",
        "This is a protocol smoke test. Choose the lowest legal index.",
        SampleOptions(),
        logPath
    });
    CHECK_MESSAGE(response.ok, response.errorMessage);
    CHECK(response.selectedIndex >= 0);
    CHECK_FALSE(response.reasoningContent.empty());
    CHECK_FALSE(response.rawResponse.empty());

    const std::string log = ReadFile(logPath);
    CHECK(log.find("Bearer ***") != std::string::npos);
    CHECK_FALSE(log.find(provider.apiKey) != std::string::npos);

    JsonPtr logJson(cJSON_Parse(log.c_str()));
    REQUIRE(logJson != nullptr);
    const cJSON* responseBody = cJSON_GetObjectItemCaseSensitive(logJson.get(), "responseBody");
    REQUIRE(cJSON_IsObject(responseBody));
    const cJSON* choices = cJSON_GetObjectItemCaseSensitive(responseBody, "choices");
    REQUIRE(cJSON_IsArray(choices));
}
