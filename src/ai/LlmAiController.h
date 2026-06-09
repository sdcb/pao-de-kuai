#pragma once

#include "ai/PdkAiClient.h"
#include "game/ExternalAiController.h"

#include <memory>
#include <mutex>
#include <optional>

namespace pdk::ai {

class LlmAiController final : public game::ExternalAiController {
public:
    explicit LlmAiController(stats::AiProviderSettings provider);
    ~LlmAiController() override;

    bool CanHandle(rules::PlayerId player) const override;
    bool HasPending() const override;
    void Start(game::ExternalAiRequest request) override;
    std::optional<game::ExternalAiResult> TryGetResult() override;
    void Cancel() override;

private:
    struct SharedState;

    stats::AiProviderSettings provider_;
    std::shared_ptr<SharedState> state_;
    std::filesystem::path runRoot_;
};

} // namespace pdk::ai
