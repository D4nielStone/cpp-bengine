#pragma once

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

        void create_editor_camera_settings(editor_config& config);
    };
}