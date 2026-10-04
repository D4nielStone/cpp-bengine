#include "system/editor_config.hpp"
#include "system/editor_ui.hpp"
#include "system/editor_ui_elements.hpp"

#include <algorithm>
#include <functional>
#include <string>

#include <bgui.hpp>
#include <elem/input_area.hpp>
#include <os/os.hpp>

namespace {
    float setting_value(const std::string& text, const float current,
                        const float minimum, const float maximum) {
        try {
            return std::clamp(std::stof(text), minimum, maximum);
        } catch (...) {
            return current;
        }
    }
}

void editor::editor_ui::open_editor_camera_settings(editor_config& config) {
    if (!ui_elements::contains(ui_elements::camera_settings_window))
        create_editor_camera_settings(config);

    const auto size = bgui::get_context_size();
    auto& settings_window = ui_elements::require<bgui::window>(ui_elements::camera_settings_window);
    settings_window.set_position(
        std::max(0, (size.x - settings_window.processed_width()) / 2),
        std::max(0, (size.y - settings_window.processed_height()) / 2)
    );
    settings_window.set_enable(true);
    settings_window.set_flex(false);
}

void editor::editor_ui::create_editor_camera_settings(editor_config& config) {
    auto& root = bgui::get_layout();
    auto& settings_window = root.add_persistent<bgui::window, bgui::layer::overlay>("Editor Camera");
    settings_window.add_class(ui_elements::camera_settings_window);

    auto add_setting = [this, &settings_window](
        const std::string& title,
        const std::string& initial,
        const std::function<void(const std::string)>& apply) {
        auto& row = settings_window.add_persistent<bgui::linear>(bgui::orientation::horizontal);
        row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);

        auto& label = row.add_persistent<bgui::text>(title, 0.35f);
        label.style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);

        auto& input = row.add_persistent<bgui::input_area>(initial, 0.35f, apply);
        input.style.layout.require_mode(bgui::mode::pixel, bgui::mode::match_parent);
        input.style.layout.require_size(100.f, 30.f);
    };

    add_setting("Move speed", "4.0", [&config](const std::string value) {
        config.set_camera_move_speed(setting_value(value, config.camera_move_speed(), 0.1f, 100.f));
    });
    add_setting("Look sensitivity", "0.12", [&config](const std::string value) {
        config.set_camera_look_sensitivity(setting_value(value, config.camera_look_sensitivity(), 0.01f, 2.f));
    });
    add_setting("Zoom sensitivity", "3.0", [&config](const std::string value) {
        config.set_camera_zoom_sensitivity(setting_value(value, config.camera_zoom_sensitivity(), 0.1f, 20.f));
    });
    add_setting("UI scale", "0.9", [this, &config](const std::string value) {
        m_ui_scale = setting_value(value, m_ui_scale, 0.5f, 2.f);
        config.set_ui_scale(m_ui_scale);
        bgui::set_global_scale(m_ui_scale);
    });
    add_setting("Min Z far", "0.01", [this](const std::string value) {
        m_camera_min_z_far = setting_value(value, m_camera_min_z_far, 0.01f, 100.f);
    });
    auto& grid_visibility = settings_window.add_persistent<bgui::checkbox>(
        "Show grid", 0.35f, m_grid_gizmo.enabled);
    grid_visibility.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    grid_visibility.set_on_change([this](const bool checked) {
        m_grid_gizmo.enabled = checked;
    });
    add_setting("Grid spacing", "1.0", [this](const std::string value) {
        m_grid_gizmo.spacing = setting_value(value, m_grid_gizmo.spacing, 0.5f, 20.f);
    });
    add_setting("Grid extent", "50.0", [this](const std::string value) {
        m_grid_gizmo.extent = setting_value(value, m_grid_gizmo.extent, 1.f, 50.f);
    });
}
