#include "overlays/AboutOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"

namespace pdk::overlays {
namespace {

using namespace ui;

constexpr Rect Panel{300.0f, 96.0f, 680.0f, 528.0f};

} // namespace

AboutOverlay::AboutOverlay(app::App& app) : app_(app) {
    buttons_ = {
        ui::MakeButton({565.0f, 548.0f, 150.0f, 46.0f}, "知道了", ui::ButtonStyle::Primary)
    };
}

void AboutOverlay::Update(float dt) {
    elapsed_ += dt;
    ButtonGroup::UpdateAll(buttons_, dt);
}

void AboutOverlay::Render(graphics::RenderContext& context) {
    BeginModal(context, Panel, elapsed_);
    DrawPanel(context, Panel);
    DrawRadialGlow(context, {640.0f, 150.0f}, 170.0f, WithAlpha(theme::Gold, 0.10f));

    DrawSeal(context, {640.0f, 150.0f}, 64.0f, "快", theme::Cinnabar, -5.0f, 40.0f);
    context.DrawTextUtf8("极客版跑得快", {Panel.x, 194.0f, Panel.width, 46.0f}, Centered(Kai(34.0f)), theme::GoldLight);
    context.DrawTextUtf8("由 sdcb 开发，为妈妈做的一款单机跑得快", {Panel.x, 242.0f, Panel.width, 24.0f}, Centered(Text(15.5f)), theme::Muted);
    DrawHairline(context, Panel.x + 80.0f, Panel.x + Panel.width - 80.0f, 282.0f, 0.45f);

    float y = 302.0f;
    auto section = [&](const char* label, const std::string& body, D2D1_COLOR_F color) {
        context.DrawTextUtf8(label, {Panel.x + 70.0f, y + 1.0f, 110.0f, 24.0f}, Text(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD), theme::Gold);
        graphics::TextStyle style = Text(15.0f);
        style.lineHeight = 23.0f;
        const float width = Panel.width - 250.0f;
        const float height = context.MeasureText(body, style, width).height;
        context.DrawTextUtf8(body, {Panel.x + 180.0f, y, width, height + 4.0f}, style, color);
        y += height + 16.0f;
    };
    section("开源地址", "https://github.com/sdcb/pao-de-kuai", theme::GoldLight);
    section("使用技术", "C、Win32、Direct2D、DirectWrite、WIC、Media Foundation、WASAPI、cJSON、doctest、CMake", theme::Ivory);
    section("第三方许可", "cJSON / doctest 使用 MIT License", theme::Ivory);

    const std::string thanks = "如果你喜欢这个项目，欢迎到 GitHub 给一个 star";
    const graphics::TextStyle thanksStyle = Text(15.0f);
    const float width = context.MeasureText(thanks, thanksStyle).width;
    const float left = 640.0f - (width + 26.0f) * 0.5f;
    DrawIcon(context, Icon::Star, {left, 497.0f, 18.0f, 18.0f}, theme::Gold);
    context.DrawTextUtf8(thanks, {left + 26.0f, 494.0f, width + 4.0f, 24.0f}, thanksStyle, theme::Muted);

    ButtonGroup::DrawAll(context, buttons_);
    EndModal(context);
}

bool AboutOverlay::OnMouseMove(float x, float y) {
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool AboutOverlay::OnMouseDown(float x, float y) {
    if (ButtonGroup::Hit(buttons_, x, y) >= 0) {
        app_.Audio().Play(SOUND_RESUME);
        app_.CloseTopOverlay();
    }
    return true;
}

} // namespace pdk::overlays
