#include "app/App.h"

#include "audio/SoundIds.h"
#include "graphics/WicImageLoader.h"
#include "overlays/AboutOverlay.h"
#include "overlays/ConfirmExitDialog.h"
#include "overlays/InvalidMoveToast.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
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

namespace pdk::app {

bool App::Initialize(HWND hwnd, bool viewerMode) {
    hwnd_ = hwnd;
    viewerMode_ = viewerMode;
    settings_ = stats::LoadAppSettings();
    audio_.Initialize();
    audio_.SetMasterVolume(settings_.masterVolume);
    if (!renderContext_.Initialize(hwnd_)) {
        return false;
    }
    if (!viewerMode_) {
        ShowStart();
    }
    return true;
}

void App::Update(float dt) {
    audio_.Update();
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
    if (mock || cardAtlas_.Loaded()) {
        ChangeScene(std::make_unique<scenes::GameScene>(*this, mock));
    } else {
        ChangeScene(std::make_unique<scenes::LoadingScene>(*this, scenes::LoadingTarget::Game));
    }
}

void App::ShowStats() {
    ChangeScene(std::make_unique<scenes::StatsScene>(*this));
}

void App::ShowSettings() {
    if (!settingsDialog_) {
        settingsDialog_ = std::make_unique<dialogs::SettingsDialog>(*this);
    }
    if (!settingsDialog_->Show(hwnd_)) {
        settingsDialog_.reset();
    }
}

void App::ShowHelp() {
    ChangeScene(std::make_unique<scenes::HelpScene>(*this));
}

void App::ShowViewerScene(const std::string& scene, const std::string& overlay, const std::string& mock) {
    if (scene == "game") {
        StartGame(mock.empty() ? true : true);
    } else if (scene == "stats") {
        ShowStats();
    } else if (scene == "settings") {
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
    renderContext_.EnsureDeviceResources();
    audio_.SetMasterVolume(settings_.masterVolume);
    audio_.LoadAllFromResources();

    const auto cardBytes = resources::LoadResourceBytes(IDR_POKER_CARDS);
    auto bitmap = graphics::LoadBitmapFromMemory(renderContext_.Target(), renderContext_.WicFactory(), cardBytes);
    if (bitmap) {
        cardAtlas_.SetBitmap(std::move(bitmap));
    }
    return cardAtlas_.Loaded();
}

void App::ReleaseGameResources() {
    cardAtlas_.Reset();
}

void App::SaveSettings() {
    stats::SaveAppSettings(settings_);
}

bool App::ProcessDialogMessage(MSG* msg) {
    if (settingsDialog_ && settingsDialog_->ProcessMessage(msg)) {
        return true;
    }
    return false;
}

HWND App::SettingsDialogHwnd() const {
    return settingsDialog_ && settingsDialog_->IsOpen() ? settingsDialog_->Hwnd() : nullptr;
}

void App::UpdateWindowSize(int width, int height) {
    settings_.windowWidth = std::max(1280, width);
    settings_.windowHeight = std::max(720, height);
}

void App::PlaySceneEventAudio(const std::string& eventName) {
    (void)eventName;
}

} // namespace pdk::app
