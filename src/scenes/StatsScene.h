#pragma once

#include "core/Scene.h"
#include "scenes/SceneCommon.h"
#include "stats/StatStore.h"

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class StatsScene final : public core::Scene {
public:
    explicit StatsScene(app::App& app);
    void OnEnter() override;
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    std::vector<Button> buttons_;
    stats::StatSummary today_;
    stats::StatSummary month_;
    stats::StatSummary history_;
};

} // namespace pdk::scenes
