#pragma once

#include "game/ExternalAiController.h"

#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace pdk::game {

enum class LocalAiKind {
    Basic,
    Strong
};

class LocalAiController final : public ExternalAiController {
public:
    LocalAiController();
    ~LocalAiController() override;

    void SetStrategy(rules::PlayerId player, LocalAiKind kind);

    bool CanHandle(rules::PlayerId player) const override;
    bool HasPending() const override;
    void Start(ExternalAiRequest request) override;
    std::optional<ExternalAiResult> TryGetResult() override;
    void Cancel() override;

private:
    struct SharedState;

    std::map<rules::PlayerId, LocalAiKind> strategies_;
    std::shared_ptr<SharedState> state_;
};

} // namespace pdk::game
