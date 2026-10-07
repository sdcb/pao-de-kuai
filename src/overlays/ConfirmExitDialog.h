#pragma once

#include "core/CppCompat.h"
#include "scenes/SceneCommon.h"

namespace pdk::app {
class App;
}

namespace pdk::overlays {

class ConfirmExitDialog final : public core::OverlayClass {
public:
    explicit ConfirmExitDialog(app::App& app);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return true; }
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    std::vector<ui::Button> buttons_;
    float elapsed_{0.0f};
};

} // namespace pdk::overlays
