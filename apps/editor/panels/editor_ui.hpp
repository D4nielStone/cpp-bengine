#pragma once

#include "panel.hpp"
#include "build_process.hpp"
#include "project_persistence_system.hpp"
#include "ui_persistence_system.hpp"
#include "editor_context.hpp"

#include <bgui.hpp>
#include <lay/dock.hpp>

#include <filesystem>
#include <functional>
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace bgui {
class linear;
class modal;
class inputbox;
class text;
class progress_bar;
}

namespace editor::ui {

class editor_ui final {
public:
    editor_ui(
        bgui::dock& dock,
        bgui::menu_bar& menu_bar,
        ui_persistence_system& ui_persistence,
        project_persistence_system& project_persistence,
        std::shared_ptr<commons::ecs> registry,
        std::string project_path,
        std::filesystem::path build_directory
    );

    void set_project_loaded_callback(
        std::function<void(const std::string&)> callback
    );
    void set_play_state_callback(
        std::function<bool(bool, std::string&)> callback
    );
    void set_runtime_running_callback(
        std::function<bool(std::string&)> callback
    );
    void set_runtime_pause_callback(
        std::function<bool(bool, std::string&)> callback
    );
    void set_history_callbacks(
        std::function<void()> undo_callback,
        std::function<void()> redo_callback
    );
    void set_exit_callback(std::function<void()> callback);
    void confirm_exit();
    void set_editor_context(std::shared_ptr<editor_context> context);

    template <typename T, typename... Args>
    T& add_panel(Args&&... args) {
        auto panel = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *panel;
        result.build(m_dock);
        m_panels.push_back(std::move(panel));
        return result;
    }

    void update();

private:
    bgui::dock& m_dock;
    std::shared_ptr<editor_context> m_context;
    project_persistence_system& m_project_persistence;
    std::shared_ptr<commons::ecs> m_registry;
    std::string m_project_path;
    std::function<void(const std::string&)> m_project_loaded_callback;
    std::function<bool(bool, std::string&)> m_play_state_callback;
    std::function<bool(std::string&)> m_runtime_running_callback;
    std::function<bool(bool, std::string&)> m_runtime_pause_callback;
    std::function<void()> m_undo_callback;
    std::function<void()> m_redo_callback;
    bgui::button* m_play_button{nullptr};
    bgui::icon* m_play_icon{nullptr};
    bgui::button* m_pause_button{nullptr};
    bgui::icon* m_pause_icon{nullptr};
    bgui::text* m_build_status{nullptr};
    bgui::progress_bar* m_build_progress{nullptr};
    bgui::modal* m_build_modal{nullptr};
    bgui::inputbox* m_build_directory_input{nullptr};
    bgui::button* m_build_directory_browse_button{nullptr};
    bgui::modal* m_project_modal{nullptr};
    bgui::modal* m_new_project_modal{nullptr};
    bgui::modal* m_exit_modal{nullptr};
    bgui::modal* m_global_lighting_modal{nullptr};
    bgui::modal* m_global_physics_modal{nullptr};
    bgui::inputbox* m_project_directory_input{nullptr};
    bgui::inputbox* m_new_project_name_input{nullptr};
    bgui::inputbox* m_new_project_location_input{nullptr};
    std::array<bgui::inputbox*, 3> m_light_direction_inputs{};
    std::array<bgui::inputbox*, 3> m_light_ambient_inputs{};
    std::array<bgui::inputbox*, 3> m_light_color_inputs{};
    bgui::inputbox* m_light_intensity_input{nullptr};
    std::array<bgui::inputbox*, 3> m_gravity_inputs{};
    bgui::text* m_project_picker_status{nullptr};
    bool m_is_playing{false};
    bool m_is_paused{false};
    bool m_build_running{false};
    std::filesystem::path m_build_directory;
    std::filesystem::path m_repository_root;
    std::unique_ptr<build_process> m_build_process;
    std::function<void()> m_exit_callback;
    std::vector<std::unique_ptr<panel>> m_panels;

    void create_project_modal(
        const std::string& selected_path,
        const std::string& status
    );
    void create_new_project_modal();
    void create_exit_modal();
    void open_preferences_modal();
    void apply_layout(bool wide);
    void open_global_lighting_modal();
    void open_global_physics_modal();
    void create_global_lighting_modal();
    void create_global_physics_modal();
    void refresh_project_picker(const std::string& directory);
    bool load_project_selection();
    void open_build_modal();
    void start_platform_build(build_target target);
    void update_build_process();
    void create_build_modal();
    void create_build_directory_modal();
};

}
