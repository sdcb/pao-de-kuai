#pragma once

#include "audio/AudioEngine.h"
#include "core/Overlay.h"
#include "core/SceneManager.h"
#include "dialogs/SettingsDialog.h"
#include "game/RoundRecorder.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "stats/AppSettings.h"

#include <memory>
#include <string>
#include <vector>

#include <windows.h>

namespace pdk::app {

class App {
public:
    bool Initialize(HWND hwnd, bool viewerMode = false);
    void Update(float dt);
    void Render();
    void Resize(int width, int height);
    bool OnMouseMove(float x, float y);
    bool OnMouseDown(float x, float y);
    bool OnMouseUp(float x, float y);

    void ShowStart();
    void StartGame(bool mock = false);
    void RestartCurrentGame();
    void ShowStats();
    void ShowSettings();
    void ShowHelp();
    void ShowViewerScene(const std::string& scene, const std::string& overlay, const std::string& mock);
    void ChangeScene(std::unique_ptr<core::Scene> scene);

    void PushOverlay(std::unique_ptr<core::Overlay> overlay);
    void CloseTopOverlay();
    void ClearOverlays();
    void RequestClose();
    void ConfirmExit();
    bool ShouldQuit() const { return shouldQuit_; }
    bool ProcessDialogMessage(MSG* msg);

    bool LoadGameResources();
    void ReleaseGameResources();

    graphics::RenderContext& RenderContext() { return renderContext_; }
    audio::AudioEngine& Audio() { return audio_; }
    graphics::SpriteAtlas& CardAtlas() { return cardAtlas_; }
    stats::AppSettings& Settings() { return settings_; }
    const stats::AppSettings& Settings() const { return settings_; }
    game::RoundRecorder& Recorder() { return recorder_; }
    bool ViewerMode() const { return viewerMode_; }
    HWND SettingsDialogHwnd() const;

    void SaveSettings();
    void UpdateWindowSize(int width, int height);

private:
    void PlaySceneEventAudio(const std::string& eventName);

    HWND hwnd_{};
    bool viewerMode_{false};
    bool shouldQuit_{false};
    graphics::RenderContext renderContext_;
    audio::AudioEngine audio_;
    graphics::SpriteAtlas cardAtlas_;
    core::SceneManager sceneManager_;
    std::vector<std::unique_ptr<core::Overlay>> overlays_;
    std::unique_ptr<dialogs::SettingsDialog> settingsDialog_;
    stats::AppSettings settings_;
    game::RoundRecorder recorder_;
};

} // namespace pdk::app
