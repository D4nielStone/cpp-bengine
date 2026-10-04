#pragma once

namespace editor {
    class editor_system;

    class editor_ui final {
    public:
        void open_editor_camera_settings(editor_system& system);

    private:
        void create_editor_camera_settings(editor_system& system);
    };
}