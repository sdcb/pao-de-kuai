#pragma once

#include <chrono>

namespace pdk::core {

class FrameTimer {
public:
    float Tick() {
        const auto now = clock::now();
        const float dt = std::chrono::duration<float>(now - last_).count();
        last_ = now;
        return dt > 0.1f ? 0.1f : dt;
    }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point last_{clock::now()};
};

} // namespace pdk::core
