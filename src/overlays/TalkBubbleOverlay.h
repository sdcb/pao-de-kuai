#pragma once

#include "core/Overlay.h"
#include "rules/Scoring.h"

#include <string>

namespace pdk::overlays {

class TalkBubbleOverlay final : public core::Overlay {
public:
    TalkBubbleOverlay(rules::PlayerId player, std::string text);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return false; }
    bool Expired() const { return elapsed_ > 3.0f; }

private:
    rules::PlayerId player_;
    std::string text_;
    float elapsed_{0.0f};
};

} // namespace pdk::overlays
