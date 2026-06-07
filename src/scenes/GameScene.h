#pragma once

#include "core/Scene.h"
#include "game/GameState.h"
#include "scenes/SceneCommon.h"

#include <array>
#include <vector>

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class GameScene final : public core::Scene {
public:
    explicit GameScene(app::App& app, bool mock = false);
    void OnEnter() override;
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y) override;

private:
    void DrawPlayerHand(graphics::RenderContext& context);
    void DrawPlayedCards(graphics::RenderContext& context);
    void DrawAiArea(graphics::RenderContext& context, rules::PlayerId player, const core::Rect& area);
    void DrawDealPile(graphics::RenderContext& context);
    void UpdateActionButtons();
    bool InteractionReady() const;
    int HitPlayerCard(float x, float y) const;
    core::Rect CardRect(int index) const;
    core::Rect CardRectFor(int index, int count) const;
    core::Rect AiCardRectFor(int index, int count, const core::Rect& area) const;
    void LayoutActionButtons();
    bool HitBackButton(float x, float y) const;
    void ConsumeEvents();
    void UpdateRoundResultDelay(float dt);
    void ShowRoundResultOverlay();

    app::App& app_;
    game::GameState game_;
    std::vector<Button> buttons_;
    std::vector<int> dragPath_;
    int hoverCard_{-1};
    int dragStartCard_{-1};
    bool dragSelecting_{false};
    bool dragMoved_{false};
    bool backButtonHover_{false};
    bool recordedRound_{false};
    bool roundResultPending_{false};
    bool mock_{false};
    bool actionButtonsDirty_{true};
    bool lastInteractionReady_{false};
    std::array<int, 3> todayScores_{0, 0, 0};
    std::array<rules::Cards, 3> handsBeforeSort_;
    bool handsSorted_{true};
    float dealElapsed_{0.0f};
    int dealSoundCount_{0};
    float sortAnimation_{0.0f};
    float playAnimation_{0.0f};
    float bombAnimation_{0.0f};
    float roundResultDelay_{0.0f};
    rules::PlayerId lastAnimatedPlayer_{rules::PlayerId::Player};
};

} // namespace pdk::scenes
