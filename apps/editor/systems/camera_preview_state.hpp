#pragma once

#include "components/camera.hpp"

#include <memory>

namespace editor::ui {

struct camera_preview_state {
    std::shared_ptr<commons::camera> main_runtime_camera;
    bgui::vec4i viewport{0, 0, 0, 0};
    bool hovered{false};
};

}
