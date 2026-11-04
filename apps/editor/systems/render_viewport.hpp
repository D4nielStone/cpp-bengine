#pragma once

#include "camera_viewport.hpp"
#include "systems/render_system.hpp"

namespace editor::ui {

inline viewport_frame render_viewport(
    commons::render_system& renderer, const std::shared_ptr<commons::ecs>& registry,
    commons::camera& camera, int width, int height,
    std::uint32_t entity = 0, std::shared_ptr<commons::renderer> replacement = {},
    const std::shared_ptr<commons::transform>& transform = {}
) {
    if (width <= 0 || height <= 0) return {};
    camera.request_framebuffer_size({width, height});
    renderer.render_view(registry, camera, entity, std::move(replacement), transform);
    return camera_viewport::frame_of(camera);
}

// Picking and overlays use the layout aspect ratio while the framebuffer
// keeps the size of the last rendered frame until the next camera pass.
inline void update_viewport_projection(commons::camera& camera, int width, int height) {
    const auto previous_size = camera.viewportFBO;
    camera.viewportFBO = {width, height};
    camera.getViewMatrix();
    camera.obtProjectionMatrix();
    if (camera.flag_fb) camera.viewportFBO = previous_size;
}

}
