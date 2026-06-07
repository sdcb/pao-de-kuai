#pragma once

#include "core/Scene.h"
#include "scenes/SceneCommon.h"
#include "stats/AppSettings.h"

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class SettingsScene final : public core::Scene {
public:
    explicit SettingsScene(app::App& app);
    void OnEnter() override;
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    stats::AppSettings draft_;
    std::vector<Button> buttons_;
};

} // namespace pdk::scenes
