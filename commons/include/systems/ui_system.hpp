#pragma once

#include <memory>

#include "systems/system.hpp"

namespace bgui {
    class image;
    class linear;
}

namespace COMMONS_NS {
    class camera;

    class ui_system final : public system {
    public:
        ~ui_system() override;

        void setup(const std::shared_ptr<ecs>&) override;
        void update(const std::shared_ptr<ecs>&) override;

    private:
        bool m_initialized{false};
        std::weak_ptr<camera> m_camera;
        bgui::linear* m_window_context{nullptr};
        bgui::image* m_framebuffer_image{nullptr};
        int m_framebuffer_display_width{0};
        int m_framebuffer_display_height{0};
    };
}
