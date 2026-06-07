#include "core/Tween.h"

#include <algorithm>
#include <cmath>

namespace pdk::core {

float Ease(Easing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::OutCubic:
        return 1.0f - std::pow(1.0f - t, 3.0f);
    case Easing::InOutCubic:
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case Easing::OutBack: {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
    }
    }
    return t;
}

Tween::Tween(float from, float to, float duration, Easing easing)
    : from_(from), to_(to), duration_(duration), easing_(easing) {}

void Tween::Update(float dt) {
    if (finished_) {
        return;
    }
    elapsed_ += dt;
    if (elapsed_ < delay_) {
        return;
    }
    const float activeElapsed = elapsed_ - delay_;
    const float t = duration_ <= 0.0f ? 1.0f : std::clamp(activeElapsed / duration_, 0.0f, 1.0f);
    const float eased = Ease(easing_, t);
    if (onValue_) {
        onValue_(from_ + (to_ - from_) * eased);
    }
    if (t >= 1.0f) {
        finished_ = true;
        if (onComplete_) {
            onComplete_();
        }
    }
}

void TweenSet::Add(Tween tween) {
    tweens_.push_back(std::move(tween));
}

void TweenSet::Update(float dt) {
    for (Tween& tween : tweens_) {
        tween.Update(dt);
    }
    tweens_.erase(
        std::remove_if(tweens_.begin(), tweens_.end(), [](const Tween& tween) {
            return tween.Finished();
        }),
        tweens_.end());
}

} // namespace pdk::core
