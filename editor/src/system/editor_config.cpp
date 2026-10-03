#include "system/editor_config.hpp"

#include <bgui.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

void editor::editor_config::initialize_interface() const {
    bgui::style_manager::get_instance().apply_theme(bgui::dark_theme());
    bgui::set_global_scale(0.9f);
}

void editor::editor_config::load_interface() const {
    bgui::load_configuration("editor.cfg");
}

void editor::editor_config::save_interface() const {
    bgui::save_configuration("editor.cfg");
}

float editor::editor_config::camera_move_speed() const {
    return m_camera_move_speed;
}

float editor::editor_config::camera_look_sensitivity() const {
    return m_camera_look_sensitivity;
}

float editor::editor_config::camera_zoom_sensitivity() const {
    return m_camera_zoom_sensitivity;
}

float editor::editor_config::ui_scale() const {
    return m_ui_scale;
}

void editor::editor_config::set_camera_move_speed(const float value) {
    m_camera_move_speed = value;
}

void editor::editor_config::set_camera_look_sensitivity(const float value) {
    m_camera_look_sensitivity = value;
}

void editor::editor_config::set_camera_zoom_sensitivity(const float value) {
    m_camera_zoom_sensitivity = value;
}

void editor::editor_config::set_ui_scale(const float value) {
    m_ui_scale = value;
    bgui::set_global_scale(value);
}