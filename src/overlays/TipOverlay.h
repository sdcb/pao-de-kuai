#pragma once

#include "core/Overlay.h"
#include "scenes/SceneCommon.h"

#include <string>

namespace pdk::app {
class App;
}

namespace pdk::overlays {

class TipOverlay final : public core::Overlay {
public:
    TipOverlay(app::App& app, std::string text);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return false; }
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    std::string text_;
};

} // namespace pdk::overlays
