#pragma once

#include <utils/vec.hpp>

#include "commons_namespace.hpp"

namespace COMMONS_NS {
    class camera;
    class transform;
}

namespace editor {
    class transform_gizmo {
    public:
        bool update(
            const COMMONS_NS::camera& camera,
            COMMONS_NS::transform* target,
            const bgui::vec4i& viewport,
            const bgui::vec2i& mouse_position,
            const bgui::vec2i& mouse_delta,
            bool mouse_over_view,
            bool left_just_pressed,
            bool left_down);

    private:
        enum class mode {
            translate,
            rotate,
            scale
        };

        mode m_mode{mode::translate};
        int m_active_axis{-1};
        bool m_dragging{false};
        float m_last_mouse_angle{0.f};
        float m_start_scale{1.f};
        bgui::vec2 m_center{};
        bgui::vec2 m_drag_axis_screen{};
        bgui::vec2 m_drag_start_mouse{};
        float m_drag_pixels_per_unit{0.f};
    };
}