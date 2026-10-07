#include "app/App.h"

#include "audio/SoundIds.h"
#include "graphics/WicImageLoader.h"
#include "overlays/AboutOverlay.h"
#include "overlays/ConfirmExitDialog.h"
#include "overlays/InvalidMoveToast.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
#include "overlays/SettingsOverlay.h"
#include "overlays/TalkBubbleOverlay.h"
#include "overlays/TipOverlay.h"
#include "resources/ResourceIds.h"
#include "resources/ResourceLoader.h"
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
    if (core::Scene* scene = sceneManager_.Current()) {
        scene->Update(dt);
    }
    for (auto& overlay : overlays_) {
        overlay->Update(dt);
    }
    overlays_.erase(
        std::remove_if(overlays_.begin(), overlays_.end(), [](const std::unique_ptr<core::Overlay>& overlay) {
            if (const auto* toast = dynamic_cast<const overlays::InvalidMoveToast*>(overlay.get())) {
                return toast->Expired();
            }
            if (const auto* talk = dynamic_cast<const overlays::TalkBubbleOverlay*>(overlay.get())) {
                return talk->Expired();
            }
            return false;
        }),
        overlays_.end());
    SyncIme();
}

core::Overlay* App::TopOverlay() const {
    return overlays_.empty() ? nullptr : overlays_.back().get();
}

bool App::WantsTextInput() const {
    const core::Overlay* top = TopOverlay();
    return top && top->WantsTextInput();
}

void App::SyncIme() {
    const bool wants = WantsTextInput();
    ime_.SetEnabled(wants);
    Rect caret;
    if (wants && TopOverlay()->TextCaretRect(caret)) {
        ime_.SetCaret(caret, renderContext_.View());
    }
}

bool App::OnKeyDown(const core::KeyEvent& key) {
    core::Overlay* top = TopOverlay();
    return top && top->OnKeyDown(key);
}

bool App::OnText(const std::wstring& text) {
    core::Overlay* top = TopOverlay();
    return top && top->OnText(text);
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
            top->OnImeComposition({}, 0);
            top->OnText(text);
        }
        int cursor = 0;
        if ((lParam & GCS_COMPSTR) && ime_.ReadComposition(text, cursor)) {
            top->OnImeComposition(text, cursor);
        }
        SyncIme();
        return true;
    }
    case WM_IME_ENDCOMPOSITION:
        top->OnImeComposition({}, 0);
        return true;
    default:
        return false;
    }
}

void App::Render() {
    renderContext_.BeginFrame();
    if (core::Scene* scene = sceneManager_.Current()) {
        scene->Render(renderContext_);
    } else {
        renderContext_.Clear(D2D1::ColorF(0.03f, 0.18f, 0.13f));
    }
    for (auto& overlay : overlays_) {
        overlay->Render(renderContext_);
    }
    if (sceneFade_ > 0.0f) {
        const float t = sceneFade_ * sceneFade_ * (3.0f - 2.0f * sceneFade_);
        renderContext_.FillRect({0.0f, 0.0f, LogicalWidth, LogicalHeight}, D2D1::ColorF(0.012f, 0.047f, 0.035f, t));
    }
    if (!renderContext_.EndFrame()) {
        ReleaseGameResources();
        if (core::Scene* scene = sceneManager_.Current()) {
            scene->OnD2DResourcesLost();
        }
    }
}

void App::Resize(int width, int height) {
    renderContext_.Resize(width, height);
    UpdateWindowSize(width, height);
}

bool App::OnMouseMove(float x, float y) {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        if ((*it)->OnMouseMove(x, y) || (*it)->BlocksInputBelow()) {
            return true;
        }
    }
    return sceneManager_.Current() && sceneManager_.Current()->OnMouseMove(x, y);
}

bool App::OnMouseDown(float x, float y) {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        core::Overlay* overlay = it->get();
        if (overlay->OnMouseDown(x, y)) {
            return true;
        }
        if (overlay->BlocksInputBelow()) {
            return true;
        }
    }
    return sceneManager_.Current() && sceneManager_.Current()->OnMouseDown(x, y);
}

bool App::OnMouseUp(float x, float y) {
    for (auto it = overlays_.rbegin(); it != overlays_.rend(); ++it) {
        if ((*it)->OnMouseUp(x, y) || (*it)->BlocksInputBelow()) {
            return true;
        }
    }
    return sceneManager_.Current() && sceneManager_.Current()->OnMouseUp(x, y);
}

void App::ShowStart() {
    ChangeScene(std::make_unique<scenes::StartScene>(*this));
}

void App::StartGame(bool mock) {
    if (mock || GameResourcesReady()) {
        ChangeScene(std::make_unique<scenes::GameScene>(*this, mock));
    } else {
        ChangeScene(std::make_unique<scenes::LoadingScene>(*this, scenes::LoadingTarget::Game));
    }
}

void App::RestartCurrentGame() {
    if (auto* gameScene = dynamic_cast<scenes::GameScene*>(sceneManager_.Current())) {
        ClearOverlays();
        gameScene->StartNextRound();
        return;
    }
    StartGame();
}

void App::ShowStats() {
    ChangeScene(std::make_unique<scenes::StatsScene>(*this));
}

void App::ShowSettings() {
    PushOverlay(std::make_unique<overlays::SettingsOverlay>(*this));
}

void App::ShowHelp() {
    ChangeScene(std::make_unique<scenes::HelpScene>(*this));
}

void App::ShowViewerScene(const std::string& scene, const std::string& overlay, const std::string& mock) {
    if (scene == "game" && mock == "midgame") {
        ChangeScene(std::make_unique<scenes::GameScene>(*this, true, true));
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
        ChangeScene(std::make_unique<scenes::LoadingScene>(*this, scenes::LoadingTarget::Game));
    } else {
        ShowStart();
    }

    if (overlay == "confirm-exit") {
        PushOverlay(std::make_unique<overlays::ConfirmExitDialog>(*this));
    } else if (overlay == "about") {
        PushOverlay(std::make_unique<overlays::AboutOverlay>(*this));
    } else if (overlay == "tip") {
        PushOverlay(std::make_unique<overlays::TipOverlay>(*this, "推荐先走顺子，少留散牌"));
    } else if (overlay == "invalid") {
        PushOverlay(std::make_unique<overlays::InvalidMoveToast>("牌型或点数压不过上家"));
    } else if (overlay == "talk") {
        PushOverlay(std::make_unique<overlays::TalkBubbleOverlay>(rules::PlayerId::Ai1, "哇，李姐你太强了！"));
    } else if (overlay == "return-menu") {
        PushOverlay(std::make_unique<overlays::ReturnToMenuOverlay>(*this));
    } else if (overlay == "result-win") {
        stats::RoundRecord record;
        record.winner = rules::PlayerId::Player;
        record.playerName = settings_.playerName;
        record.scores = {18, -8, -10};
        record.remainingCards = {0, 8, 10};
        record.bombs = {rules::BombScoreEvent{rules::PlayerId::Player, 20}};
        PushOverlay(std::make_unique<overlays::RoundResultOverlay>(*this, record));
    }
}

void App::ChangeScene(std::unique_ptr<core::Scene> scene) {
    ClearOverlays();
    sceneFade_ = 1.0f;
    sceneManager_.Change(std::move(scene));
}

void App::PushOverlay(std::unique_ptr<core::Overlay> overlay) {
    overlays_.push_back(std::move(overlay));
}

void App::CloseTopOverlay() {
    if (!overlays_.empty()) {
        overlays_.pop_back();
    }
}

void App::ClearOverlays() {
    overlays_.clear();
}

void App::RequestClose() {
    if (viewerMode_) {
        ConfirmExit();
        return;
    }
    PushOverlay(std::make_unique<overlays::ConfirmExitDialog>(*this));
    audio_.Play(audio::SoundId::Pause);
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
