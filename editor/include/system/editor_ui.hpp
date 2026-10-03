#pragma once

#include "guizmo/grid_gizmo.hpp"

namespace bgui {
    class window;
}

namespace editor {
    class editor_config;

    class editor_ui final {
    public:
        void open_editor_camera_settings(editor_config& config);
        void update();

    private:
        bgui::window* m_editor_settings{nullptr};
        float m_camera_min_z_far{0.01f};
        float m_ui_scale{1.f};
        grid_gizmo m_grid_gizmo;

        void create_editor_camera_settings(editor_config& config);
    };
}