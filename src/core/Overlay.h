#pragma once

namespace pdk::graphics {
class RenderContext;
}

namespace pdk::core {

class Overlay {
public:
    virtual ~Overlay() = default;

    virtual void Update(float dt) = 0;
    virtual void Render(graphics::RenderContext& context) = 0;

    virtual bool BlocksInputBelow() const = 0;
    virtual bool OnMouseMove(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseDown(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseUp(float x, float y) { (void)x; (void)y; return false; }
};

} // namespace pdk::core
