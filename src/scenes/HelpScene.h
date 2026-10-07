#pragma once

#include "core/CppCompat.h"
#include "scenes/SceneCommon.h"

#include <string_view>

namespace pdk::app {
class App;
}

namespace pdk::scenes {

class HelpScene final : public core::SceneClass {
public:
    explicit HelpScene(app::App& app);
    void OnEnter() override;
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool OnMouseMove(float x, float y) override;
    bool OnMouseDown(float x, float y) override;

private:
    float DrawBullets(graphics::RenderContext& context, std::string_view text, float x, float y, float width);
    void DrawPatternGallery(graphics::RenderContext& context, float x, float y, float width);

    app::App& app_;
    std::vector<Button> buttons_;
    float elapsed_{0.0f};
};

} // namespace pdk::scenes
