#pragma once

#include "components/camera.hpp"
#include "editor_viewport.hpp"

#include <memory>
#include <utility>

namespace editor::ui {

class camera_viewport final : public editor_viewport {
public:
    explicit camera_viewport(std::shared_ptr<commons::camera> camera)
        : m_camera(std::move(camera)) {}

    void initialize() {
        if (m_camera && !m_camera->flag_fb)
            m_camera->createFB();
    }

    viewport_frame frame() const override {
        return m_camera ? frame_of(*m_camera) : viewport_frame{};
    }

    static viewport_frame frame_of(const commons::camera& camera) {
        if (!camera.flag_fb) return {};
        return {camera.framebuffer_texture(), camera.viewportFBO.x, camera.viewportFBO.y};
    }

    void request_size(int width, int height) override {
        if (m_camera && width > 0 && height > 0)
            m_camera->request_framebuffer_size({width, height});
    }

private:
    std::shared_ptr<commons::camera> m_camera;
};

}
