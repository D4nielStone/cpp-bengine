
#include "components/camera.hpp"
#include "systems/system.hpp"

#include <elem/image.hpp>
#include <memory>

namespace commons {

class viewport_sync_system final : public system {
public:
    viewport_sync_system(
        std::shared_ptr<camera> camera_component,
        bgui::image& image
    )
        : m_camera(std::move(camera_component)),
          m_image(&image) {
    }

    void setup(const std::shared_ptr<ecs>&) override {
    }

    void update(const std::shared_ptr<ecs>&) override {
        if (!m_camera || !m_image)
            return;

        const int width = m_image->processed_width();
        const int height = m_image->processed_height();

        if (width <= 0 || height <= 0)
            return;

        if (m_camera->viewportFBO.x != width ||
            m_camera->viewportFBO.y != height) {
            m_camera->viewport({width, height});

            m_image->set_external_texture(
                m_camera->framebuffer_texture(),
                bgui::vec2{
                    static_cast<float>(width),
                    static_cast<float>(height)
                }
            );
        }
    }

private:
    std::shared_ptr<camera> m_camera;
    bgui::image* m_image;
};

}
