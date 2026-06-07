#include "overlays/AboutOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::overlays {

AboutOverlay::AboutOverlay(app::App& app) : app_(app) {
    buttons_ = {
        {{565.0f, 540.0f, 150.0f, 48.0f}, "知道了"}
    };
}

void AboutOverlay::Update(float) {}

void AboutOverlay::Render(graphics::RenderContext& context) {
    context.FillRect({0.0f, 0.0f, 1280.0f, 720.0f}, scenes::Color(0.0f, 0.0f, 0.0f, 0.42f));
    scenes::DrawPanel(context, {270.0f, 120.0f, 740.0f, 500.0f}, scenes::Color(0.07f, 0.13f, 0.12f, 1.0f));
    context.DrawTextUtf8("关于极客版跑得快", {300.0f, 178.0f, 680.0f, 46.0f}, 31.0f, scenes::Color(0.98f, 0.92f, 0.55f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const std::string text =
        "由 sdcb 开发，为妈妈做的一款单机跑得快。\n"
        "这是一个开源项目，代码地址：\n"
        "https://github.com/sdcb/pao-de-kuai\n\n"
        "使用技术：C++、Win32、Direct2D、DirectWrite、WIC、\n"
        "Media Foundation、XAudio2.8、cJSON、doctest、CMake。\n\n"
        "如果你喜欢这个项目，欢迎到 GitHub 给一个 star ⭐。";
    context.DrawTextUtf8(text, {330.0f, 245.0f, 620.0f, 250.0f}, 21.0f, scenes::Color(0.90f, 0.95f, 0.86f), DWRITE_TEXT_ALIGNMENT_CENTER);

    scenes::ButtonGroup::DrawAll(context, buttons_);
}

bool AboutOverlay::OnMouseMove(float x, float y) {
    scenes::ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool AboutOverlay::OnMouseDown(float x, float y) {
    if (scenes::ButtonGroup::Hit(buttons_, x, y) >= 0) {
        app_.Audio().Play(audio::SoundId::Resume);
        app_.CloseTopOverlay();
    }
    return true;
}

} // namespace pdk::overlays
