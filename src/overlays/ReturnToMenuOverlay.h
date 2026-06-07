#pragma once

#include "core/Overlay.h"
#include "scenes/SceneCommon.h"

namespace pdk::app {
class App;
}

namespace pdk::overlays {

class ReturnToMenuOverlay final : public core::Overlay {
public:
    explicit ReturnToMenuOverlay(app::App& app);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return true; }
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    std::vector<scenes::Button> buttons_;
};

} // namespace pdk::overlays
