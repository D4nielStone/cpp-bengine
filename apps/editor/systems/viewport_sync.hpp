
#pragma once

#include "editor_viewport.hpp"
#include "systems/system.hpp"

#include <elem/image.hpp>

namespace editor::ui {
    class viewport_sync_system final : public commons::system {
    public:
        viewport_sync_system(
            editor_viewport& viewport,
            bgui::image& image
        )
            : m_viewport(viewport),
            m_image(&image) {
        }

        void setup(const std::shared_ptr<commons::ecs>&) override {
        }

        void update(const std::shared_ptr<commons::ecs>&) override {
            const int width = m_image->processed_width();
            const int height = m_image->processed_height();

            if (width <= 0 || height <= 0)
                return;

            m_viewport.request_size(width, height);
            const auto frame = m_viewport.frame();
            const bgui::vec2 size{static_cast<float>(frame.width), static_cast<float>(frame.height)};
            const auto& current = m_image->get_texture();
            if (current.m_id != frame.texture || (frame.texture && current.m_size != size))
                m_image->set_external_texture(frame.texture, size);
        }

    private:
        editor_viewport& m_viewport;
        bgui::image* m_image;
    };

}
