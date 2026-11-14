#pragma once

#include "panel.hpp"
#include "build_process.hpp"
#include "project_persistence_system.hpp"
#include "ui_persistence_system.hpp"
#include "editor_context.hpp"
#include "editor_commands.hpp"
#include "panel_manager.hpp"

#include <bgui.hpp>
#include <lay/dock.hpp>

#include <filesystem>
#include <functional>
#include <array>
#include <memory>
#include <string>

namespace commons { class renderer; class model; }

namespace bgui {
class linear;
class modal;
class inputbox;
class text;
class progress_bar;
class checkbox;
}

namespace editor::ui {

class files_view;
class scenes_view;
class entities_view;
class components_view;

enum class editor_workspace_mode {
    editor,
    model_editing,
    scene_management,
    visual_scripting
};

class editor_ui final {
public:
    editor_ui(
        bgui::dock& editor_dock,
        bgui::dock& model_dock,
        bgui::dock& management_dock,
        bgui::menu_bar& menu_bar,
        bgui::linear& side_bar,
        ui_persistence_system& ui_persistence,
        project_persistence_system& project_persistence,
        std::shared_ptr<commons::ecs> registry,
        std::string project_path,
        std::filesystem::path build_directory
    );

    void set_load_dispatcher(std::function<void(std::function<void()>)> dispatch) { m_load_dispatcher = std::move(dispatch); }
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
    void set_editor_commands(std::shared_ptr<editor_commands> commands);
    void set_exit_callback(std::function<void()> callback);
    void confirm_exit();
    void set_project_manager_callback(std::function<void()> callback) { m_project_manager_callback = std::move(callback); }
    void set_animation_callback(std::function<void()> callback) { m_animation_callback = std::move(callback); }
    void open_project(const std::string& path);
    void set_loaded_project_path(const std::string& path);
    void set_editor_context(std::shared_ptr<editor_context> context);
    void set_workspace_mode(editor_workspace_mode mode);
    void set_project_unloading_callback(std::function<bool(std::string&)> callback) {
        m_project_unloading_callback = std::move(callback);
    }

    template <typename T, typename... Args>
    T& add_panel(Args&&... args) {
        return m_panel_manager.add<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    T& add_model_panel(Args&&... args) {
        return m_model_panel_manager.add<T>(std::forward<Args>(args)...);
    }

    void setup_scene_management(files_view& files, std::function<void()> scene_loaded);
    std::unordered_map<uint32_t, std::pair<std::weak_ptr<commons::renderer>, const commons::model*>> m_rig_models;
    void update();
    void reset_ui();
    void refresh_feature_settings();
    std::optional<std::uint32_t> managed_entity_at(bgui::vec2i position) const;
    bool drop_managed_asset(const std::filesystem::path& path, bgui::vec2i position);

private:
    std::function<void(std::function<void()>)> m_load_dispatcher;
    bool prepare_project_transition(std::string& error);
    bool dispatch_load(const std::string& path, std::function<void(bool, const std::string&)> complete,
        const std::filesystem::path& scene = {});
    void open_managed_scene(const std::filesystem::path& path);
    std::filesystem::path project_root() const;
    bgui::dock& m_dock;
    bgui::dock& m_model_dock;
    bgui::dock& m_management_dock;
    bgui::dock& m_scripting_dock;
    panel_manager m_panel_manager;
    panel_manager m_model_panel_manager;
    panel_manager m_management_panel_manager;
    panel_manager m_scripting_panel_manager;
    scenes_view* m_scenes{};
    entities_view* m_managed_entities{};
    components_view* m_managed_components{};
    std::function<void()> m_show_scene_workflow, m_show_scripting;
    files_view* m_files{};
    std::function<void()> m_scene_loaded;
    std::shared_ptr<editor_context> m_context;
    std::function<bool(std::string&)> m_project_unloading_callback;
    std::shared_ptr<editor_commands> m_commands;
    project_persistence_system& m_project_persistence;
    std::shared_ptr<commons::ecs> m_registry;
    std::string m_project_path;
    bool m_project_load_pending{false};
    bool m_visual_scripting_enabled{false};
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
    std::array<bgui::icon*, 4> m_workspace_icons{};
    std::array<bgui::button*, 4> m_workspace_buttons{};
    bgui::text* m_build_status{nullptr};
    bgui::progress_bar* m_build_progress{nullptr};
    bgui::modal* m_build_modal{nullptr};
    bgui::inputbox* m_build_directory_input{nullptr};
    bgui::button* m_build_directory_browse_button{nullptr};
    bgui::modal* m_project_modal{nullptr};
    bgui::modal* m_new_project_modal{nullptr};
    bgui::modal* m_exit_modal{nullptr};
    bgui::modal* m_editor_camera_modal{nullptr};
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
    bgui::inputbox* m_runtime_template_input{nullptr};
    bgui::checkbox* m_single_executable_checkbox{nullptr};
    bool m_single_executable{false};
    std::filesystem::path m_runtime_template;
    bool m_build_running{false};
    std::filesystem::path m_build_directory;
    std::filesystem::path m_repository_root;
    std::unique_ptr<build_process> m_build_process;
    std::function<void()> m_exit_callback;
    std::function<void()> m_project_manager_callback, m_animation_callback;

    void create_project_modal(
        const std::string& selected_path,
        const std::string& status
    );
    void create_new_project_modal();
    void create_exit_modal();
    bool stop_runtime_for_edit(std::string& error);
    void open_preferences_modal();
    void open_editor_camera_modal();
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
