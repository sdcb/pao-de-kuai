#pragma once

#include "core/Scene.h"

#include <string>

namespace pdk::app {
class App;
}

namespace pdk::scenes {

enum class LoadingTarget {
    Game,
    Stats
};

class LoadingScene final : public core::Scene {
public:
    LoadingScene(app::App& app, LoadingTarget target);
    void OnEnter() override;
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;

private:
    app::App& app_;
    LoadingTarget target_;
    float elapsed_{0.0f};
    float progress_{0.0f};
    std::string item_{"准备加载"};
    bool loaded_{false};
};

} // namespace pdk::scenes
