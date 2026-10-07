#include "overlays/SettingsOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/CppCompat.h"

#include <windows.h>

namespace pdk::overlays {
namespace {

using namespace ui;

constexpr Rect Panel{330.0f, 104.0f, 620.0f, 512.0f};
constexpr float LabelX = Panel.x + 48.0f;
constexpr float ControlX = Panel.x + 188.0f;
constexpr float ControlRight = Panel.x + Panel.width - 48.0f;
constexpr float RowName = Panel.y + 140.0f;
constexpr float RowVolume = Panel.y + 212.0f;
constexpr float RowAi1 = Panel.y + 280.0f;
constexpr float RowAi2 = Panel.y + 342.0f;
constexpr float RowTrace = Panel.y + 404.0f;

std::string TrimSpaces(std::string text) {
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

void DrawRowLabel(graphics::RenderContext& context, const char* text, float centerY) {
    graphics::TextStyle style = Text(17.0f);
    style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    style.wrap = false;
    context.DrawTextUtf8(text, {LabelX, centerY - 20.0f, 130.0f, 40.0f}, style, theme::Muted);
}

void DrawRowNote(graphics::RenderContext& context, const std::string& text, float x, float centerY) {
    graphics::TextStyle style = Text(14.5f);
    style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    style.wrap = false;
    context.DrawTextUtf8(text, {x, centerY - 16.0f, ControlRight - x, 32.0f}, style, theme::Faint);
}

const char* StrategyNote(int selected) {
    return selected == 1 ? "会记牌、算牌" : "稳健直接";
}

} // namespace

SettingsOverlay::SettingsOverlay(app::App& app) : app_(app), draft_(app.Settings()), originalVolume_(app.Settings().masterVolume) {
    nameField_.rect = {ControlX, RowName - 23.0f, ControlRight - ControlX, 46.0f};
    nameField_.placeholder = "输入你的名字";
    nameField_.clipboardOwner = app.Hwnd();
    nameField_.SetUtf8(draft_.playerName);
    nameField_.SetFocused(true);

    volume_.rect = {ControlX + 10.0f, RowVolume - 14.0f, 300.0f, 28.0f};
    volume_.value = draft_.masterVolume;

    const std::vector<std::string> strategies{"基础", "强力"};
    ai1_.rect = {ControlX, RowAi1 - 20.0f, 240.0f, 40.0f};
    ai1_.options = strategies;
    ai1_.selected = draft_.ai1 == "strong" ? 1 : 0;
    ai1_.slide = static_cast<float>(ai1_.selected);
    ai2_.rect = {ControlX, RowAi2 - 20.0f, 240.0f, 40.0f};
    ai2_.options = strategies;
    ai2_.selected = draft_.ai2 == "strong" ? 1 : 0;
    ai2_.slide = static_cast<float>(ai2_.selected);

    trace_.rect = {ControlX, RowTrace - 15.0f, 54.0f, 30.0f};
    trace_.on = draft_.roundTraceEnabled;
    trace_.t = trace_.on ? 1.0f : 0.0f;

    const float buttonY = Panel.y + Panel.height - 72.0f;
    buttons_ = {
        {{ControlRight - 296.0f, buttonY, 140.0f, 46.0f}, "取消", ButtonStyle::Secondary},
        {{ControlRight - 140.0f, buttonY, 140.0f, 46.0f}, "保存", ButtonStyle::Primary}
    };
}

void SettingsOverlay::Update(float dt) {
    elapsed_ += dt;
    nameField_.Update(dt);
    volume_.Update(dt);
    ai1_.Update(dt);
    ai2_.Update(dt);
    trace_.Update(dt);
    ButtonGroup::UpdateAll(buttons_, dt);
}

void SettingsOverlay::Render(graphics::RenderContext& context) {
    BeginModal(context, Panel, elapsed_);
    DrawPanel(context, Panel);

    context.DrawTextUtf8("设置", {Panel.x + 44.0f, Panel.y + 26.0f, 200.0f, 44.0f}, Kai(32.0f), theme::GoldLight);
    graphics::TextStyle hint = Text(14.0f);
    hint.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    hint.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    hint.wrap = false;
    context.DrawTextUtf8("Enter 保存 · Esc 取消", {ControlRight - 260.0f, Panel.y + 30.0f, 260.0f, 36.0f}, hint, theme::Faint);
    DrawHairline(context, Panel.x + 32.0f, Panel.x + Panel.width - 32.0f, Panel.y + 84.0f, 0.45f);

    DrawRowLabel(context, "玩家名", RowName);
    nameField_.Draw(context);

    DrawRowLabel(context, "主音量", RowVolume);
    volume_.Draw(context);
    std::string percent;
    core::AppendNumber(percent, RoundToInt(volume_.value * 100.0f));
    percent += '%';
    graphics::TextStyle valueStyle = Text(17.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    valueStyle.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    valueStyle.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    valueStyle.wrap = false;
    context.DrawTextUtf8(percent, {ControlRight - 70.0f, RowVolume - 16.0f, 70.0f, 32.0f}, valueStyle, theme::GoldLight);

    DrawRowLabel(context, "AI1 策略", RowAi1);
    ai1_.Draw(context);
    DrawRowNote(context, StrategyNote(ai1_.selected), ai1_.rect.x + ai1_.rect.width + 18.0f, RowAi1);

    DrawRowLabel(context, "AI2 策略", RowAi2);
    ai2_.Draw(context);
    DrawRowNote(context, StrategyNote(ai2_.selected), ai2_.rect.x + ai2_.rect.width + 18.0f, RowAi2);

    DrawRowLabel(context, "复盘记录", RowTrace);
    trace_.Draw(context);
    DrawRowNote(context, "每局结束写入复盘 JSON", trace_.rect.x + trace_.rect.width + 18.0f, RowTrace);

    ButtonGroup::DrawAll(context, buttons_);
    EndModal(context);
}

bool SettingsOverlay::OnMouseMove(float x, float y) {
    nameField_.UpdateHover(x, y);
    nameField_.OnMouseMove(x, y);
    if (volume_.OnMouseMove(x, y)) {
        draft_.masterVolume = volume_.value;
        app_.Audio().SetMasterVolume(draft_.masterVolume);
    }
    ai1_.UpdateHover(x, y);
    ai2_.UpdateHover(x, y);
    trace_.UpdateHover(x, y);
    ButtonGroup::UpdateHover(buttons_, x, y);
    return true;
}

bool SettingsOverlay::OnMouseDown(float x, float y) {
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (nameField_.OnMouseDown(x, y, shift)) {
        return true;
    }
    if (volume_.OnMouseDown(x, y)) {
        draft_.masterVolume = volume_.value;
        app_.Audio().SetMasterVolume(draft_.masterVolume);
        return true;
    }
    if (ai1_.OnMouseDown(x, y) || ai2_.OnMouseDown(x, y)) {
        app_.Audio().Play(audio::SoundId::SelectCard);
        return true;
    }
    if (trace_.OnMouseDown(x, y)) {
        app_.Audio().Play(audio::SoundId::ButtonClick);
        return true;
    }
    const int hit = ButtonGroup::Hit(buttons_, x, y);
    if (hit == 0) {
        Cancel();
    } else if (hit == 1) {
        Save();
    }
    return true;
}

bool SettingsOverlay::OnMouseUp(float x, float y) {
    (void)x;
    (void)y;
    nameField_.OnMouseUp();
    if (volume_.OnMouseUp()) {
        app_.Audio().Play(audio::SoundId::SelectCard);
    }
    return true;
}

bool SettingsOverlay::OnKeyDown(const core::KeyEvent& key) {
    if (key.key == VK_ESCAPE) {
        Cancel();
        return true;
    }
    if (key.key == VK_RETURN) {
        Save();
        return true;
    }
    if (key.key == VK_TAB) {
        nameField_.SetFocused(!nameField_.Focused());
        return true;
    }
    nameField_.OnKeyDown(key);
    return true;
}

bool SettingsOverlay::OnText(const std::wstring& text) {
    nameField_.Insert(text);
    return true;
}

void SettingsOverlay::OnImeComposition(const std::wstring& text, int cursor) {
    nameField_.SetComposition(text, cursor);
}

bool SettingsOverlay::TextCaretRect(Rect& caret) const {
    if (!nameField_.Focused()) {
        return false;
    }
    caret = nameField_.CaretRect();
    return true;
}

void SettingsOverlay::Save() {
    const std::string name = TrimSpaces(nameField_.Utf8());
    if (!name.empty()) {
        draft_.playerName = name;
    }
    draft_.masterVolume = volume_.value;
    draft_.ai1 = ai1_.selected == 1 ? "strong" : "basic";
    draft_.ai2 = ai2_.selected == 1 ? "strong" : "basic";
    draft_.roundTraceEnabled = trace_.on;
    // Window size is tracked live by the app; keep whatever it is now.
    draft_.windowWidth = app_.Settings().windowWidth;
    draft_.windowHeight = app_.Settings().windowHeight;
    app_.Settings() = draft_;
    app_.Audio().SetMasterVolume(draft_.masterVolume);
    app_.SaveSettings();
    app_.Audio().Play(audio::SoundId::Confirm);
    app_.CloseTopOverlay();
}

void SettingsOverlay::Cancel() {
    app_.Audio().SetMasterVolume(originalVolume_);
    app_.Audio().Play(audio::SoundId::Cancel);
    app_.CloseTopOverlay();
}

} // namespace pdk::overlays
