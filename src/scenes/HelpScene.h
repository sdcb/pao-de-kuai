#pragma once

#include "core/Scene.h"
#include "scenes/SceneCommon.h"

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class HelpScene final : public core::Scene {
public:
    explicit HelpScene(app::App& app);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    app::App& app_;
    std::vector<Button> buttons_;
};

} // namespace pdk::scenes
