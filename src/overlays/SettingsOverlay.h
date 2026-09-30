#pragma once

#include "core/Overlay.h"
#include "scenes/SceneCommon.h"
#include "stats/AppSettings.h"
#include "ui/Inputs.h"

namespace pdk::app {
class App;
}

namespace pdk::overlays {

class SettingsOverlay final : public core::Overlay {
public:
    explicit SettingsOverlay(app::App& app);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return true; }
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;
    bool OnMouseUp(float x, float y) override;
    bool OnKeyDown(const core::KeyEvent& key) override;
    bool OnText(const std::wstring& text) override;
    bool WantsTextInput() const override { return nameField_.Focused(); }
    void OnImeComposition(const std::wstring& text, int cursor) override;
    bool TextCaretRect(core::Rect& caret) const override;

private:
    void Save();
    void Cancel();

    app::App& app_;
    stats::AppSettings draft_;
    float originalVolume_{0.8f};
    ui::TextField nameField_;
    ui::Slider volume_;
    ui::Segmented ai1_;
    ui::Segmented ai2_;
    ui::Toggle trace_;
    std::vector<ui::Button> buttons_;
    float elapsed_{0.0f};
};

} // namespace pdk::overlays
