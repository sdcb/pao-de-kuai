#pragma once

#include "core/Scene.h"

#include <memory>

namespace pdk::core {

class SceneManager {
public:
    void Change(std::unique_ptr<Scene> scene) {
        if (scene_) {
            scene_->OnExit();
        }
        scene_ = std::move(scene);
        if (scene_) {
            scene_->OnEnter();
        }
    }

    Scene* Current() const { return scene_.get(); }

private:
    std::unique_ptr<Scene> scene_;
};

} // namespace pdk::core
