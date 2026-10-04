#pragma once

namespace editor {
    class editor_config final {
    public:
        void initialize_interface() const;
        void load_interface() const;
        void save_interface() const;

        float camera_move_speed() const;
        float camera_look_sensitivity() const;
        float camera_zoom_sensitivity() const;
        float ui_scale() const;

        void set_camera_move_speed(float value);
        void set_camera_look_sensitivity(float value);
        void set_camera_zoom_sensitivity(float value);
        void set_ui_scale(float value);

    private:
        float m_camera_move_speed{4.f};
        float m_camera_look_sensitivity{0.12f};
        float m_camera_zoom_sensitivity{3.f};
        float m_ui_scale{0.78f};
    };
}