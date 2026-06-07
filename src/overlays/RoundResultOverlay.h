#pragma once

#include "core/Overlay.h"
#include "scenes/SceneCommon.h"
#include "stats/DailyStat.h"

namespace pdk::app {
class App;
}

namespace pdk::overlays {

class RoundResultOverlay final : public core::Overlay {
public:
    RoundResultOverlay(app::App& app, stats::RoundRecord record);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return true; }
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    stats::RoundRecord record_;
    std::vector<scenes::Button> buttons_;
};

} // namespace pdk::overlays
