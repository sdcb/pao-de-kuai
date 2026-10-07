#pragma once

#include "core/Scene.h"
#include "scenes/SceneCommon.h"

#include <string>

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class StartScene final : public core::Scene {
public:
    explicit StartScene(app::App& app);
    void OnEnter() override;
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    void DrawTitle(graphics::RenderContext& context);
    void DrawCardFan(graphics::RenderContext& context);

    app::App& app_;
    std::vector<Button> buttons_;
    std::string welcome_;
    float elapsed_{0.0f};
    Point mouse_{640.0f, 360.0f};
    Point parallax_{0.0f, 0.0f};
};

} // namespace pdk::scenes
