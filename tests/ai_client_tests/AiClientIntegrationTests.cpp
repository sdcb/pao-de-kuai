#include <doctest/doctest.h>

#include "ai/LlmOffscreenExperiment.h"
#include "stats/AppSettings.h"

#include <filesystem>

using namespace pdk;

TEST_CASE("LLM offscreen experiment completes with tool-call history") {
    const stats::AppSettings settings = stats::LoadAppSettings("appsettings.json");
    REQUIRE_MESSAGE(settings.aiProviders.count("mimo") == 1, "missing aiProviders.mimo in appsettings.json");
    stats::AiProviderSettings provider = settings.aiProviders.at("mimo");
    REQUIRE_MESSAGE(provider.type == "openai", "aiProviders.mimo.type must be openai");
    REQUIRE_MESSAGE(!provider.endpoint.empty(), "aiProviders.mimo.endpoint is empty");
    REQUIRE_MESSAGE(!provider.apiKey.empty(), "aiProviders.mimo.apiKey is empty");
    REQUIRE_MESSAGE(!provider.model.empty(), "aiProviders.mimo.model is empty");

    const auto root = std::filesystem::current_path() / "build" / "vs2026-release" / "ai-client-runs";
    const ai::LlmOffscreenExperimentResult result = ai::RunLlmOffscreenExperiment(ai::LlmOffscreenExperimentConfig{
        provider,
        root.string(),
        {20260608u, 20260609u, 20260610u, 20260611u, 20260612u, 20260613u},
        300,
        80
    });

    INFO("logRoot: " << result.logRoot);
    INFO("message: " << result.message);
    CHECK(result.ok);
    CHECK(result.completedRound);
    CHECK(result.llmCalls > 0);
    CHECK(result.invalidLlmMoves == 0);
    CHECK(result.observedCannotBeatSynthetic);
    CHECK(result.llmPlayedAfterCannotBeatSynthetic);
}
