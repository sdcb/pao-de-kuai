#pragma once

#include "ai/PdkAiClient.h"
#include "ai/TurnRecord.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pdk::ai {

enum class ToolHistoryMode {
    Loose,
    Strict
};

struct LlmOffscreenExperimentConfig {
    stats::AiProviderSettings provider;
    std::filesystem::path runRoot;
    std::vector<unsigned> seeds{20260608u, 20260609u, 20260610u, 20260611u, 20260612u, 20260613u};
    int maxTurnsPerRound{300};
    int maxLlmCalls{80};
};

struct LlmOffscreenExperimentResult {
    bool ok{false};
    ToolHistoryMode mode{ToolHistoryMode::Loose};
    unsigned seed{0};
    int turns{0};
    int llmCalls{0};
    int invalidLlmMoves{0};
    bool observedCannotBeatSynthetic{false};
    bool llmPlayedAfterCannotBeatSynthetic{false};
    bool completedRound{false};
    std::string message;
    std::filesystem::path logRoot;
    std::vector<TurnRecord> records;
};

std::string ToolHistoryModeLabel(ToolHistoryMode mode);
LlmOffscreenExperimentResult RunLlmOffscreenExperiment(const LlmOffscreenExperimentConfig& config);
std::string ToJson(const LlmOffscreenExperimentResult& result);

std::vector<PdkAiMessage> BuildExperimentMessagesForTest(
    const std::vector<TurnRecord>& records,
    const std::string& currentUserPrompt,
    ToolHistoryMode mode);
std::string BuildCurrentPromptForTest(
    const std::vector<TurnRecord>& records,
    const std::array<rules::Cards, 3>& hands,
    const rules::Cards& lastCards,
    const std::optional<rules::HandPattern>& lastPattern,
    rules::PlayerId lastMovePlayer);

} // namespace pdk::ai
