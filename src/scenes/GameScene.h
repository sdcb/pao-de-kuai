#pragma once

#include "core/CppCompat.h"
#include "game/GameState.h"
#include "scenes/SceneCommon.h"

#include <array>
#include <string>
#include <vector>

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class GameScene final : public core::SceneClass {
public:
    // midgame is a scene_viewer mock: skips the deal and scripts a couple of moves.
    explicit GameScene(app::App& app, bool mock = false, bool midgame = false);
    void OnEnter() override;
    void StartNextRound();
    // App asks through the scene vtable instead of casting to this concrete type.
    bool RestartRound() override { StartNextRound(); return true; }
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y) override;

private:
    void DrawTable(graphics::RenderContext& context);
    void DrawTurnChip(graphics::RenderContext& context);
    void DrawPlayerHand(graphics::RenderContext& context);
    void DrawPlayedCards(graphics::RenderContext& context);
    void DrawAiSeat(graphics::RenderContext& context, rules::PlayerId player);
    void DrawPlayerPlate(graphics::RenderContext& context);
    void DrawPassChips(graphics::RenderContext& context);
    void DrawToast(graphics::RenderContext& context);
    void DrawDealPile(graphics::RenderContext& context);
    void DrawBombEffect(graphics::RenderContext& context);
    void UpdateActionButtons();
    void UpdateHandAnimation(float dt);
    void UpdateMidgameMock();
    bool InteractionReady() const;
    int HitPlayerCard(float x, float y) const;
    Rect CardRect(int index) const;
    Rect CardRectFor(int index, int count) const;
    Rect AiCardRectFor(rules::PlayerId player, int index, int count) const;
    void LayoutActionButtons();
    void InitializeExternalAi();
    void ConsumeEvents();
    void UpdateRoundResultDelay(float dt);
    void ShowRoundResultOverlay();

    app::App& app_;
    game::GameState game_;
    Button backButton_;
    std::vector<Button> buttons_;
    std::vector<int> dragPath_;
    std::vector<float> handLift_;
    std::vector<float> handHover_;
    int hoverCard_{-1};
    int dragStartCard_{-1};
    bool dragSelecting_{false};
    bool dragMoved_{false};
    bool recordedRound_{false};
    bool roundResultPending_{false};
    bool mock_{false};
    bool midgameMock_{false};
    bool mockPlayed_{false};
    bool mockHinted_{false};
    bool actionButtonsDirty_{true};
    bool lastInteractionReady_{false};
    std::array<int, 3> todayScores_{0, 0, 0};
    std::array<rules::Cards, 3> handsBeforeSort_;
    std::array<float, 3> passTimers_{0.0f, 0.0f, 0.0f};
    bool handsSorted_{true};
    float time_{0.0f};
    float dealElapsed_{0.0f};
    int dealSoundCount_{0};
    float sortAnimation_{0.0f};
    float playAnimation_{0.0f};
    float bombAnimation_{0.0f};
    float roundResultDelay_{0.0f};
    std::string toastText_;
    float toastAge_{0.0f};
    rules::PlayerId lastAnimatedPlayer_{PLAYER_HUMAN};
};

} // namespace pdk::scenes
