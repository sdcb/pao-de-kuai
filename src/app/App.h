#pragma once

#include "app/CppCompat.h"
#include "audio/CppCompat.h"
#include "core/CppCompat.h"
#include "game/RoundRecorder.h"
#include "graphics/CppCompat.h"
#include "graphics/CppCompat.h"
#include "stats/CppCompat.h"

#include <memory>
#include <string>
#include <vector>

#include <windows.h>

namespace pdk::app {

class App {
public:
    ~App();

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
    void ChangeScene(core::Scene scene);

    void PushOverlay(core::Overlay overlay);
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
    ::RoundRecorder& Recorder() { return recorder_; }
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
    // Overlays are stacked and swept in order every frame, so a fixed array is enough; the
    // deepest stack is a dialog plus a toast.  Each live entry owns its implementation.
    enum { OverlayCapacity = 8 };
    core::Overlay overlays_[OverlayCapacity]{};
    int overlayCount_{0};
    ImeInput ime_;
    stats::AppSettings settings_;
    ::RoundRecorder recorder_;
};

} // namespace pdk::app
