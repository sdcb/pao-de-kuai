#pragma once

#include "app/ImeInput.h"
#include "audio/AudioEngine.h"
#include "core/Overlay.h"
#include "core/SceneManager.h"
#include "game/RoundRecorder.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "stats/CppCompat.h"

#include <memory>
#include <string>
#include <vector>

#include <windows.h>

namespace pdk::app {

class App {
public:
    bool Initialize(HWND hwnd, bool viewerMode = false, bool offscreen = false);
    void Update(float dt);
    void Render();
    void Resize(int width, int height);
    bool OnMouseMove(float x, float y);
    bool OnMouseDown(float x, float y);
    bool OnMouseUp(float x, float y);
    bool OnKeyDown(const core::KeyEvent& key);
    bool OnText(const std::wstring& text);
    // Handles WM_IME_* while a text field has focus; returns false to fall back to DefWindowProc.
    bool HandleImeMessage(UINT message, WPARAM wParam, LPARAM& lParam);
    bool WantsTextInput() const;

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

    bool LoadGameResources();
    bool LoadCardAtlas();
    bool GameResourcesReady() const { return audioLoaded_ && cardAtlas_.Loaded(); }
    void ReleaseGameResources();

    graphics::RenderContext& RenderContext() { return renderContext_; }
    audio::AudioEngine& Audio() { return audio_; }
    graphics::SpriteAtlas& CardAtlas() { return cardAtlas_; }
    stats::AppSettings& Settings() { return settings_; }
    const stats::AppSettings& Settings() const { return settings_; }
    game::RoundRecorder& Recorder() { return recorder_; }
    bool ViewerMode() const { return viewerMode_; }
    HWND Hwnd() const { return hwnd_; }

    void SaveSettings();
    void UpdateWindowSize(int width, int height);

private:
    void PlaySceneEventAudio(const std::string& eventName);
    core::Overlay* TopOverlay() const;
    void SyncIme();

    HWND hwnd_{};
    bool viewerMode_{false};
    bool shouldQuit_{false};
    bool audioLoaded_{false};
    float sceneFade_{0.0f};
    graphics::RenderContext renderContext_;
    audio::AudioEngine audio_;
    graphics::SpriteAtlas cardAtlas_;
    core::SceneManager sceneManager_;
    std::vector<std::unique_ptr<core::Overlay>> overlays_;
    ImeInput ime_;
    stats::AppSettings settings_;
    game::RoundRecorder recorder_;
};

} // namespace pdk::app
