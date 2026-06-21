#pragma once

#include "ai/PdkAiClient.h"
#include "game/ExternalAiController.h"

#include <memory>
#include <map>
#include <mutex>
#include <optional>

namespace pdk::ai {

class LlmAiController final : public game::ExternalAiController {
public:
    explicit LlmAiController(std::map<rules::PlayerId, stats::AiProviderSettings> providers);
    ~LlmAiController() override;

    bool CanHandle(rules::PlayerId player) const override;
    bool IsRemote(rules::PlayerId player) const override;
    bool HasPending() const override;
    void Start(game::ExternalAiRequest request) override;
    std::optional<game::ExternalAiResult> TryGetResult() override;
    void Cancel() override;

private:
    struct SharedState;

    std::map<rules::PlayerId, stats::AiProviderSettings> providers_;
    std::shared_ptr<SharedState> state_;
    std::string runRoot_;
};

} // namespace pdk::ai
