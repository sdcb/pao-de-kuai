#pragma once

#include "core/Geometry.h"

#include <functional>
#include <vector>

namespace pdk::core {

enum class Easing {
    Linear,
    OutCubic,
    InOutCubic,
    OutBack
};

float Ease(Easing easing, float t);

class Tween {
public:
    Tween(float from, float to, float duration, Easing easing = Easing::OutCubic);

    void SetDelay(float delay) { delay_ = delay; }
    void SetOnValue(std::function<void(float)> onValue) { onValue_ = std::move(onValue); }
    void SetOnComplete(std::function<void()> onComplete) { onComplete_ = std::move(onComplete); }
    void Update(float dt);
    bool Finished() const { return finished_; }

private:
    float from_{};
    float to_{};
    float duration_{};
    float elapsed_{};
    float delay_{};
    Easing easing_{Easing::OutCubic};
    bool finished_{false};
    std::function<void(float)> onValue_;
    std::function<void()> onComplete_;
};

class TweenSet {
public:
    void Add(Tween tween);
    void Update(float dt);
    bool Empty() const { return tweens_.empty(); }
    void Clear() { tweens_.clear(); }

private:
    std::vector<Tween> tweens_;
};

} // namespace pdk::core
