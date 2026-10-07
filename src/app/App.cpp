#include "app/App.h"

#include "audio/SoundIds.h"
#include "graphics/CppCompat.h"
#include "overlays/AboutOverlay.h"
#include "overlays/ConfirmExitDialog.h"
#include "overlays/InvalidMoveToast.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
#include "overlays/SettingsOverlay.h"
#include "overlays/TalkBubbleOverlay.h"
#include "overlays/TipOverlay.h"
#include "resources/ResourceIds.h"
#include "resources/CppCompat.h"
#include "scenes/GameScene.h"
#include "scenes/HelpScene.h"
#include "scenes/LoadingScene.h"
#include "scenes/StartScene.h"
#include "scenes/StatsScene.h"

#include <algorithm>

#include <imm.h>

namespace pdk::app {

bool App::Initialize(HWND hwnd, bool viewerMode, bool offscreen) {
    hwnd_ = hwnd;
    viewerMode_ = viewerMode;
    ime_.Attach(hwnd_);
    settings_ = stats::LoadAppSettings();
    audio_.Initialize();
    audio_.SetMasterVolume(settings_.masterVolume);
    if (!renderContext_.Initialize(hwnd_, offscreen)) {
        return false;
    }
    if (!viewerMode_) {
        ShowStart();
    }
    return true;
}

void App::Update(float dt) {
    sceneFade_ = std::max(0.0f, sceneFade_ - dt / 0.28f);
    if (core::Scene* scene = SceneManager_Current(&sceneManager_)) {
        Scene_Update(scene, dt);
    }
    for (int i = 0; i < overlayCount_; ++i) {
        Overlay_Update(&overlays_[i], dt);
    }
    // Drop the expired ones (the toasts and the AI bubbles), keeping the rest in order.  The old
    // code told those two types apart with dynamic_cast; now the vtable answers for them, which
    // also means a future expiring overlay needs no change here.
    for (int i = 0; i < overlayCount_;) {
        if (!Overlay_Expired(&overlays_[i])) {
            ++i;
            continue;
        }
        Overlay_Release(&overlays_[i]);
        for (int j = i + 1; j < overlayCount_; ++j) {
            overlays_[j - 1] = overlays_[j];
        }
        --overlayCount_;
    }
    SyncIme();
}

core::Overlay* App::TopOverlay() const {
    // A const method handing out a mutable overlay, exactly as the old vector<unique_ptr> did.
    return overlayCount_ > 0 ? const_cast<core::Overlay*>(&overlays_[overlayCount_ - 1]) : nullptr;
}

bool App::WantsTextInput() const {
    const core::Overlay* top = TopOverlay();
    return top != nullptr && Overlay_WantsTextInput(top);
}

void App::SyncIme() {
    const bool wants = WantsTextInput();
    core::Overlay* top = TopOverlay();
    Rect caret;

    ime_.SetEnabled(wants);
    if (wants && top != nullptr && Overlay_TextCaretRect(top, &caret)) {
        ime_.SetCaret(caret, renderContext_.View());
    }
}

bool App::OnKeyDown(const core::KeyEvent& key) {
    core::Overlay* top = TopOverlay();
    return top != nullptr && Overlay_OnKeyDown(top, &key);
}

bool App::OnText(const std::wstring& text) {
    core::Overlay* top = TopOverlay();
    return top != nullptr && Overlay_OnText(top, text.c_str());
}

bool App::HandleImeMessage(UINT message, WPARAM wParam, LPARAM& lParam) {
    (void)wParam;
    if (!WantsTextInput()) {
        return false;
    }
    core::Overlay* top = TopOverlay();
    switch (message) {
    case WM_IME_SETCONTEXT:
        // The field draws the composition string itself; keep only the candidate list.
        lParam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
        return false;
    case WM_IME_STARTCOMPOSITION:
        SyncIme();
        return true;
    case WM_IME_COMPOSITION: {
        std::wstring text;
        if ((lParam & GCS_RESULTSTR) && ime_.ReadResult(text) && !text.empty()) {
            Overlay_OnImeComposition(top, L"", 0);
            Overlay_OnText(top, text.c_str());
        }
        int cursor = 0;
        if ((lParam & GCS_COMPSTR) && ime_.ReadComposition(text, cursor)) {
            Overlay_OnImeComposition(top, text.c_str(), cursor);
        }
        SyncIme();
        return true;
    }
    case WM_IME_ENDCOMPOSITION:
        Overlay_OnImeComposition(top, L"", 0);
        return true;
    default:
        return false;
    }
}

void App::Render() {
    core::Scene* scene = SceneManager_Current(&sceneManager_);

    renderContext_.BeginFrame();
    if (scene != nullptr) {
        Scene_Render(scene, renderContext_.Native());
    } else {
        renderContext_.Clear(D2D1::ColorF(0.03f, 0.18f, 0.13f));
    }
    for (int i = 0; i < overlayCount_; ++i) {
        Overlay_Render(&overlays_[i], renderContext_.Native());
    }
    if (sceneFade_ > 0.0f) {
        const float t = sceneFade_ * sceneFade_ * (3.0f - 2.0f * sceneFade_);
        renderContext_.FillRect({0.0f, 0.0f, LogicalWidth, LogicalHeight}, D2D1::ColorF(0.012f, 0.047f, 0.035f, t));
    }
    if (!renderContext_.EndFrame()) {
        ReleaseGameResources();
        if (scene != nullptr) {
            Scene_OnD2DResourcesLost(scene);
        }
    }
}

void App::Resize(int width, int height) {
    renderContext_.Resize(width, height);
    UpdateWindowSize(width, height);
}

bool App::OnMouseMove(float x, float y) {
    for (int i = overlayCount_ - 1; i >= 0; --i) {
        if (Overlay_OnMouseMove(&overlays_[i], x, y) || Overlay_BlocksInputBelow(&overlays_[i])) {
            return true;
        }
    }
    core::Scene* scene = SceneManager_Current(&sceneManager_);
    return scene != nullptr && Scene_OnMouseMove(scene, x, y);
}

bool App::OnMouseDown(float x, float y) {
    for (int i = overlayCount_ - 1; i >= 0; --i) {
        core::Overlay* overlay = &overlays_[i];

        if (Overlay_OnMouseDown(overlay, x, y)) {
            return true;
        }
        if (Overlay_BlocksInputBelow(overlay)) {
            return true;
        }
    }
    core::Scene* scene = SceneManager_Current(&sceneManager_);
    return scene != nullptr && Scene_OnMouseDown(scene, x, y);
}

bool App::OnMouseUp(float x, float y) {
    for (int i = overlayCount_ - 1; i >= 0; --i) {
        if (Overlay_OnMouseUp(&overlays_[i], x, y) || Overlay_BlocksInputBelow(&overlays_[i])) {
            return true;
        }
    }
    core::Scene* scene = SceneManager_Current(&sceneManager_);
    return scene != nullptr && Scene_OnMouseUp(scene, x, y);
}

void App::ShowStart() {
    ChangeScene(core::Transfer(new scenes::StartScene(*this)));
}

void App::StartGame(bool mock) {
    if (mock || GameResourcesReady()) {
        ChangeScene(core::Transfer(new scenes::GameScene(*this, mock)));
    } else {
        ChangeScene(core::Transfer(new scenes::LoadingScene(*this, scenes::LoadingTarget::Game)));
    }
}

void App::RestartCurrentGame() {
    core::Scene* scene = SceneManager_Current(&sceneManager_);

    // The scene decides for itself now: only the game scene restarts a round, and it reports that
    // through the vtable instead of App casting to a concrete type.
    if (scene != nullptr && Scene_RestartRound(scene)) {
        ClearOverlays();
        return;
    }
    StartGame();
}

void App::ShowStats() {
    ChangeScene(core::Transfer(new scenes::StatsScene(*this)));
}

void App::ShowSettings() {
    PushOverlay(core::Transfer(new overlays::SettingsOverlay(*this)));
}

void App::ShowHelp() {
    ChangeScene(core::Transfer(new scenes::HelpScene(*this)));
}

void App::ShowViewerScene(const std::string& scene, const std::string& overlay, const std::string& mock) {
    if (scene == "game" && mock == "midgame") {
        ChangeScene(core::Transfer(new scenes::GameScene(*this, true, true)));
    } else if (scene == "game") {
        StartGame(true);
    } else if (scene == "stats") {
        ShowStats();
    } else if (scene == "settings") {
        ShowStart();
        ShowSettings();
    } else if (scene == "help") {
        ShowHelp();
    } else if (scene == "loading") {
        ChangeScene(core::Transfer(new scenes::LoadingScene(*this, scenes::LoadingTarget::Game)));
    } else {
        ShowStart();
    }

    if (overlay == "confirm-exit") {
        PushOverlay(core::Transfer(new overlays::ConfirmExitDialog(*this)));
    } else if (overlay == "about") {
        PushOverlay(core::Transfer(new overlays::AboutOverlay(*this)));
    } else if (overlay == "tip") {
        PushOverlay(core::Transfer(new overlays::TipOverlay(*this, "推荐先走顺子，少留散牌")));
    } else if (overlay == "invalid") {
        PushOverlay(core::Transfer(new overlays::InvalidMoveToast("牌型或点数压不过上家")));
    } else if (overlay == "talk") {
        PushOverlay(core::Transfer(new overlays::TalkBubbleOverlay(PLAYER_AI1, "哇，李姐你太强了！")));
    } else if (overlay == "return-menu") {
        PushOverlay(core::Transfer(new overlays::ReturnToMenuOverlay(*this)));
    } else if (overlay == "result-win") {
        stats::RoundRecord record;
        record.winner = PLAYER_HUMAN;
        record.playerName = settings_.playerName;
        record.scores = {18, -8, -10};
        record.remainingCards = {0, 8, 10};
        record.bombs = {rules::BombScoreEvent{PLAYER_HUMAN, 20}};
        PushOverlay(core::Transfer(new overlays::RoundResultOverlay(*this, record)));
    }
}

void App::ChangeScene(core::Scene scene) {
    ClearOverlays();
    sceneFade_ = 1.0f;
    SceneManager_Change(&sceneManager_, scene);
}

void App::PushOverlay(core::Overlay overlay) {
    if (overlayCount_ >= OverlayCapacity || overlay.vtbl == nullptr) {
        Overlay_Release(&overlay);
        return;
    }
    overlays_[overlayCount_++] = overlay;
}

void App::CloseTopOverlay() {
    if (overlayCount_ > 0) {
        Overlay_Release(&overlays_[overlayCount_ - 1]);
        --overlayCount_;
    }
}

void App::ClearOverlays() {
    for (int i = 0; i < overlayCount_; ++i) {
        Overlay_Release(&overlays_[i]);
    }
    overlayCount_ = 0;
}

App::~App() {
    ClearOverlays();
    SceneManager_Release(&sceneManager_);
}

void App::RequestClose() {
    if (viewerMode_) {
        ConfirmExit();
        return;
    }
    PushOverlay(core::Transfer(new overlays::ConfirmExitDialog(*this)));
    audio_.Play(SOUND_PAUSE);
}

void App::ConfirmExit() {
    SaveSettings();
    shouldQuit_ = true;
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

bool App::LoadGameResources() {
    if (!audioLoaded_) {
        audio_.SetMasterVolume(settings_.masterVolume);
        audio_.LoadAllFromResources();
        audioLoaded_ = true;
    }
    return LoadCardAtlas();
}

bool App::LoadCardAtlas() {
    if (cardAtlas_.Loaded()) {
        return true;
    }
    renderContext_.EnsureDeviceResources();
    const auto cardBytes = resources::LoadResourceBytes(IDR_POKER_CARDS);
    auto bitmap = graphics::LoadBitmapFromMemory(renderContext_.Target(), renderContext_.WicFactory(), cardBytes);
    if (bitmap) {
        cardAtlas_.SetBitmap(std::move(bitmap));
        for (const float scale : {0.5f, 0.25f}) {
            cardAtlas_.AddLevel(graphics::LoadBitmapFromMemory(renderContext_.Target(), renderContext_.WicFactory(), cardBytes, scale), scale);
        }
    }
    return cardAtlas_.Loaded();
}

void App::ReleaseGameResources() {
    cardAtlas_.Reset();
}

void App::SaveSettings() {
    stats::SaveAppSettings(settings_);
}

void App::UpdateWindowSize(int width, int height) {
    settings_.windowWidth = std::max(1280, width);
    settings_.windowHeight = std::max(720, height);
}

void App::PlaySceneEventAudio(const std::string& eventName) {
    (void)eventName;
}

} // namespace pdk::app
