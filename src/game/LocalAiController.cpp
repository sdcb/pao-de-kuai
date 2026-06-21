#include "game/LocalAiController.h"

#include <thread>
#include <utility>

namespace pdk::game {
namespace {

std::unique_ptr<AiStrategy> MakeStrategy(LocalAiKind kind) {
    switch (kind) {
    case LocalAiKind::Basic:
        return std::make_unique<BasicAiStrategy>();
    case LocalAiKind::Strong:
        return std::make_unique<StrongAiStrategy>();
    }
    return std::make_unique<BasicAiStrategy>();
}

} // namespace

struct LocalAiController::SharedState {
    mutable std::mutex mutex;
    int generation{0};
    bool pending{false};
    std::optional<ExternalAiResult> result;
};

LocalAiController::LocalAiController() : state_(std::make_shared<SharedState>()) {}

LocalAiController::~LocalAiController() {
    Cancel();
}

void LocalAiController::SetStrategy(rules::PlayerId player, LocalAiKind kind) {
    strategies_[player] = kind;
}

bool LocalAiController::CanHandle(rules::PlayerId player) const {
    return strategies_.contains(player);
}

bool LocalAiController::HasPending() const {
    std::lock_guard lock(state_->mutex);
    return state_->pending;
}

void LocalAiController::Start(ExternalAiRequest request) {
    std::shared_ptr<SharedState> state = state_;
    const auto strategyIt = strategies_.find(request.player);
    if (strategyIt == strategies_.end()) {
        ExternalAiResult failed;
        failed.source = TurnDecisionSource::LocalAi;
        failed.errorMessage = "No local AI strategy configured for player";
        std::lock_guard lock(state->mutex);
        state->pending = false;
        state->result = std::move(failed);
        return;
    }

    const LocalAiKind kind = strategyIt->second;
    int generation = 0;
    {
        std::lock_guard lock(state->mutex);
        generation = ++state->generation;
        state->pending = true;
        state->result.reset();
    }

    std::thread([state, kind, request = std::move(request), generation]() mutable {
        ExternalAiResult result;
        result.source = TurnDecisionSource::LocalAi;
        try {
            std::unique_ptr<AiStrategy> strategy = MakeStrategy(kind);
            result.localChoice = strategy->ChooseMove(
                request.snapshot.hands[static_cast<std::size_t>(rules::PlayerIndex(request.player))],
                request.context);
            result.ok = true;
        } catch (...) {
            result.ok = false;
            result.errorMessage = "Local AI strategy failed";
        }

        std::lock_guard lock(state->mutex);
        if (state->generation == generation) {
            state->pending = false;
            state->result = std::move(result);
        }
    }).detach();
}

std::optional<ExternalAiResult> LocalAiController::TryGetResult() {
    std::lock_guard lock(state_->mutex);
    if (!state_->result) {
        return std::nullopt;
    }
    std::optional<ExternalAiResult> result = std::move(state_->result);
    state_->result.reset();
    return result;
}

void LocalAiController::Cancel() {
    std::lock_guard lock(state_->mutex);
    ++state_->generation;
    state_->pending = false;
    state_->result.reset();
}

} // namespace pdk::game
