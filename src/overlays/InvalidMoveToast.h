#pragma once

#include "core/Overlay.h"

#include <string>

namespace pdk::overlays {

class InvalidMoveToast final : public core::Overlay {
public:
    explicit InvalidMoveToast(std::string text);
    void Update(float dt) override;
    void Render(graphics::RenderContext& context) override;
    bool BlocksInputBelow() const override { return false; }
    bool Expired() const { return elapsed_ > 2.0f; }

private:
    std::string text_;
    float elapsed_{0.0f};
};

} // namespace pdk::overlays
