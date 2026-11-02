#include "editor/systems/release_indicator.hpp"
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"

#include "os/window.hpp"

#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"

#include "editor/panels/editor_ui.hpp"
#include "editor/panels/project_context_menu.hpp"
#include "editor/systems/native_file_dialog.hpp"
#include "editor/core/editor_context.hpp"
#include "editor/systems/project_persistence_system.hpp"
#include "editor/systems/project_creation_system.hpp"
#include "editor/systems/project_preview.hpp"
#include "editor/panels/files_view.hpp"
#include "editor/panels/asset_drop_overlay.hpp"
#include "editor/systems/scene_asset_drop.hpp"
#include "editor/systems/scene_asset_paths.hpp"
#include "editor/panels/debug_view.hpp"
#include "editor/panels/scene_view.hpp"
#include "editor/panels/runtime_tools_view.hpp"
#include "editor/panels/game_view.hpp"
#include "editor/panels/entities_view.hpp"
#include "editor/panels/components_view.hpp"
#include "editor/systems/ui_persistence_system.hpp"
#include "editor/systems/editor_workspace.hpp"
#include "editor/systems/runtime_process.hpp"
#include "editor/systems/undo_redo_shortcuts_system.hpp"
#include "editor/systems/ui_scale_system.hpp"
#include "editor/core/editor_commands.hpp"

#include <lay/linear.hpp>
#include <lay/dock.hpp>
#include <elem/button.hpp>
#include <elem/inputbox.hpp>
#include <elem/image.hpp>
#include <elem/text.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <vector>

namespace {

std::filesystem::path locate_build_directory(
    const std::filesystem::path& executable
) {
    std::error_code error;
    auto directory = std::filesystem::absolute(executable, error).parent_path();
    if (error)
        return {};

    while (!directory.empty()) {
        if (std::filesystem::is_regular_file(
                directory / "CMakeCache.txt",
                error
            ))
            return directory;
        if (error)
            return {};

        const auto parent = directory.parent_path();
        if (parent == directory)
            break;
        directory = parent;
    }
    return {};
}

struct project_choice {
    std::string path;
    bool create{false};
};

project_choice show_project_manager(
    commons::window& window,
    bgui::linear*& manager_root,
    const std::shared_future<std::string>& release_check
) {
    project_choice choice;
    const auto resolve_project_path = [](std::filesystem::path selected) {
        std::error_code error;
        if (std::filesystem::is_directory(selected, error) && !error) {
            for (const auto* filename : {"project.bengine", "game.world", "main.scene"}) {
                auto candidate = selected / filename;
                error.clear();
                if (std::filesystem::is_regular_file(candidate, error) && !error)
                    return candidate;
            }
            return std::filesystem::path{};
        }
        return selected;
    };
    auto& root = bgui::get_layout().add<bgui::linear>(bgui::orientation::vertical);
    manager_root = &root;
    root.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    root.add_class("project-manager");
    auto& header = root.add<bgui::linear>(bgui::orientation::vertical);
    header.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    header.style.layout.padding = bgui::vec4i{24, 16, 24, 12};
    header.add_class("project-manager-header");
    auto& title = header.add<bgui::text>("Bubble Engine " BUBBLE_VERSION_COMPLETA_STR, 0.68f);
    title.style.layout.set_margin(0, 3);
    header.add<editor::ui::release_indicator>(release_check);

    auto& project_dock = root.add<bgui::dock>();
    project_dock.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    bgui::dock::configuration dock_layout;
    dock_layout.left_ratio = 0.27f;
    dock_layout.right_ratio = 0.25f;
    project_dock.apply_configuration(dock_layout);
    auto& recent = project_dock.add_window("Recent Projects", bgui::dock_area::left);
    auto& actions = project_dock.add_window("Project Setup", bgui::dock_area::center);
    recent.style.layout.padding = bgui::vec4i{16, 12, 16, 16};
    auto& recent_title = recent.add<bgui::text>("Your workspaces", 0.42f);
    recent_title.style.layout.set_margin(0, 8);

    bgui::text* status = nullptr;
    bgui::text* template_selection_label = nullptr;
    auto selected_template = editor::ui::project_template::empty;
    std::vector<std::string> recent_projects;
    std::ifstream recent_file("editor-recent-projects.cfg");
    for (std::string line; std::getline(recent_file, line);) {
        if (!line.empty() && std::filesystem::is_regular_file(line))
            recent_projects.push_back(line);
    }
    if (recent_projects.empty())
        recent.add<bgui::text>("No recent projects", 0.32f);
    for (const auto& path : recent_projects) {
        const std::filesystem::path project_path(path);
        auto project_label = project_path.filename().string();
        const auto folder_name = project_path.parent_path().filename().string();
        if (!folder_name.empty())
            project_label += "  ·  " + folder_name;
        auto& card = recent.add<editor::ui::context_action_button>("", .34f,
            [&, path]() { choice = {path, false}; window.close(); });
        card.get_label().set_enable(false);
        card.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        card.style.layout.require_height(bgui::mode::pixel, 88.f);
        card.style.layout.set_padding(6, 5);
        card.add_class("project-card");

        const auto preview_path = editor::ui::project_preview_path(path);
        std::error_code preview_error;
        if (std::filesystem::is_regular_file(preview_path, preview_error) && !preview_error) {
            auto& preview = card.add<bgui::image>(preview_path.string());
            preview.set_size(120.f, 68.f);
            preview.add_class("project-preview");
        } else {
            auto& placeholder = card.add<bgui::linear>(bgui::orientation::vertical);
            placeholder.style.layout.require_size(120.f, 68.f);
            placeholder.style.layout.require_mode(bgui::mode::pixel, bgui::mode::pixel);
            placeholder.add_class("project-preview");
            placeholder.add<bgui::text>("No preview", 0.24f);
        }

        auto& details = card.add<bgui::linear>(bgui::orientation::vertical);
        details.style.layout.require_width(bgui::mode::stretch);
        details.style.layout.require_height(bgui::mode::match_parent);
        auto& button = details.add<bgui::button>(
            project_label,
            0.34f,
            [&, path]() { choice = {path, false}; window.close(); }
        );
        editor::ui::configure_project_context_menu(card, path,
            []() { return std::filesystem::path{}; },
            [&, path]() { choice = {path, false}; window.close(); },
            [surface = &card]() { surface->set_enable(false); },
            [&status](const auto& error) { if (status) status->set_buffer(error); });
        button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        button.style.layout.set_padding(5, 3);
        details.add<bgui::text>(folder_name.empty() ? path : folder_name, 0.26f)
            .add_class("project-manager-muted");
    }

    actions.style.layout.padding = bgui::vec4i{22, 16, 22, 18};
    auto& actions_title = actions.add<bgui::text>("Start building", 0.48f);
    actions_title.style.layout.set_margin(0, 8);
    auto& open_label = actions.add<bgui::text>("Open a project", 0.32f);
    open_label.style.layout.set_margin(0, 5);
    auto& path_row = actions.add<bgui::linear>(bgui::orientation::horizontal);
    path_row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& path_input = path_row.add<bgui::inputbox>("", "Project file or project folder", 0.36f);
    path_input.style.layout.require_width(bgui::mode::stretch);
    path_input.style.layout.require_height(bgui::mode::wrap_content);

    auto& open_button = actions.add<bgui::button>("Open Project", 0.36f, [&]() {
        if (path_input.get_buffer().empty()) {
            status->set_buffer("Select a recent project or enter a project path.");
            return;
        }
        const auto selected = resolve_project_path(path_input.get_buffer());
        if (selected.empty()) {
            status->set_buffer("No project file found in the selected folder.");
            return;
        }
        choice = {selected.string(), false};
        window.close();
    });
    open_button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    open_button.style.layout.set_padding(6, 4);

    auto& create_label = actions.add<bgui::text>("Create a new project", 0.32f);
    create_label.style.layout.set_margin(0, 8);
    template_selection_label = &actions.add<bgui::text>(
        "Template: Empty Project",
        0.28f
    );
    template_selection_label->add_class("project-manager-muted");
    auto& name_input = actions.add<bgui::inputbox>("New Project", "Project name", 0.36f);
    name_input.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    std::error_code current_path_error;
    auto default_location = std::filesystem::current_path(current_path_error);
    if (current_path_error)
        default_location = std::filesystem::path(".");
    auto& location_label = actions.add<bgui::text>("Location", 0.28f);
    location_label.style.layout.set_margin(0, 4);
    auto& location_row = actions.add<bgui::linear>(bgui::orientation::horizontal);
    location_row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& location_input = location_row.add<bgui::inputbox>(
        default_location.string(),
        "Parent folder for new projects",
        0.34f
    );
    location_input.style.layout.require_width(bgui::mode::stretch);
    location_input.style.layout.require_height(bgui::mode::wrap_content);

    auto& browse_button = path_row.add<bgui::button>("Browse...", 0.36f, [&]() {
        std::string error;
        const auto selected = editor::browse_native(path_input.get_buffer(), false, error);
        if (!selected.empty())
            path_input.set_buffer(selected.string());
        if (!error.empty())
            status->set_buffer(error);
    });
    browse_button.style.layout.require_width(bgui::mode::pixel, 84.f);
    browse_button.style.layout.require_height(bgui::mode::wrap_content);
    auto& location_browse_button = location_row.add<bgui::button>("Browse...", 0.34f, [&]() {
        std::string error;
        const auto selected = editor::browse_native(location_input.get_buffer(), true, error);
        if (!selected.empty())
            location_input.set_buffer(selected.string());
        if (!error.empty())
            status->set_buffer(error);
    });
    location_browse_button.style.layout.require_width(bgui::mode::pixel, 84.f);
    location_browse_button.style.layout.require_height(bgui::mode::wrap_content);
    auto& create_button = actions.add<bgui::button>("Create Project", 0.36f, [&]() {
        const auto project_name = name_input.get_buffer();
        const auto first_non_space = project_name.find_first_not_of(" \t\r\n");
        if (first_non_space == std::string::npos ||
            std::filesystem::path(project_name).has_parent_path() ||
            project_name == "." || project_name == "..") {
            status->set_buffer("Enter a project name without folder separators.");
            return;
        }
        if (location_input.get_buffer().empty()) {
            status->set_buffer("Choose a parent folder for the new project.");
            return;
        }
        const auto project_folder =
            std::filesystem::path(location_input.get_buffer()) / project_name;
        std::string error;
        if (!editor::ui::create_project_files(
                project_folder,
                error,
                selected_template
            )) {
            status->set_buffer(error);
            return;
        }
        choice = {project_folder.string(), true};
        window.close();
    });
    create_button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    create_button.style.layout.set_padding(6, 4);
    status = &actions.add<bgui::text>("", 0.28f);

    auto& templates = project_dock.add_window("Templates", bgui::dock_area::right);
    templates.style.layout.padding = bgui::vec4i{16, 12, 16, 16};
    auto& templates_title = templates.add<bgui::text>("Project templates", 0.42f);
    templates_title.style.layout.set_margin(0, 8);
    templates.add<bgui::text>(
        "Empty Project",
        0.34f
    );
    templates.add<bgui::text>(
        "A clean scene with the standard project folders.",
        0.28f
    ).add_class("project-manager-muted");
    auto& empty_template_button = templates.add<bgui::button>(
        "Use Empty Project",
        0.32f,
        [&]() {
            selected_template = editor::ui::project_template::empty;
            template_selection_label->set_buffer("Template: Empty Project");
            status->set_buffer("");
        }
    );
    empty_template_button.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::wrap_content
    );
    empty_template_button.style.layout.set_margin(0, 4);

    templates.add<bgui::text>("FPP - First Person", 0.34f)
        .style.layout.set_margin(0, 12);
    templates.add<bgui::text>(
        "A capsule player with mouse look, WASD movement, and a physics floor.",
        0.28f
    ).add_class("project-manager-muted");
    auto& fpp_template_button = templates.add<bgui::button>(
        "Use FPP Template",
        0.32f,
        [&]() {
            selected_template = editor::ui::project_template::fpp;
            template_selection_label->set_buffer("Template: FPP - First Person");
            status->set_buffer("");
        }
    );
    fpp_template_button.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::wrap_content
    );
    fpp_template_button.style.layout.set_margin(0, 4);

    window.loop();
    window.reopen();
    bgui::get_layout().remove(manager_root);
    manager_root = nullptr;
    window.resize(1200, 600);
    return choice;
}

}

namespace bubble {

class editor_application final {
public:
    editor_application(
        commons::window& window,
        std::string project_path,
        std::filesystem::path build_directory,
        std::filesystem::path executable,
        std::shared_future<std::string> release_check
    )
        : m_window(window),
          m_project_path(std::move(project_path)),
          m_executable(std::move(executable)),
          m_release_check(std::move(release_check)),
          m_registry(window.get_ecs()) {
        auto camera_entity = m_registry->create();
        auto cube_entity = m_registry->create();
        m_camera_entity = camera_entity.id;
        m_registry->add<commons::camera>(camera_entity);
        m_registry->add<commons::renderer>(cube_entity, "assets/models/cube.obj");
        m_camera = m_registry->get<commons::camera>(camera_entity.id);
        m_registry->get<commons::transform>(cube_entity.id)->set_position({0.f, 0.f, 0.f});
        m_registry->get<commons::transform>(camera_entity.id)->set_position({0.f, 0.f, 5.f});
        m_registry->get<commons::transform>(camera_entity.id)->set_rotation({0.f, -90.f, 0.f});

        m_context = std::make_shared<editor::ui::editor_context>(m_registry);
        m_context->editor_camera_entity = m_camera_entity;
        m_context->project_root = std::filesystem::absolute(m_project_path).parent_path();
        m_commands = std::make_shared<editor::ui::editor_commands>(m_context);
        m_project_persistence = std::make_unique<editor::ui::project_persistence_system>(
            m_registry,
            m_camera_entity,
            &m_context->entity_names,
            &m_context->hidden_entities,
            &m_context->locked_entities
        );

        auto& root = bgui::get_layout().add<bgui::linear>(bgui::orientation::vertical);
        root.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        root.add<editor::ui::release_indicator>(m_release_check);
        m_menu_bar = &root.add<bgui::menu_bar>(root);
        m_dock = &root.add<bgui::dock>();
        m_dock->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);

        m_editor = std::make_unique<editor::ui::editor_ui>(
            *m_dock,
            *m_menu_bar,
            m_ui_persistence,
            *m_project_persistence,
            m_registry,
            m_project_path,
            std::move(build_directory)
        );
        m_editor->set_editor_context(m_context);
        m_editor->set_editor_commands(m_commands);
        m_editor->set_exit_callback([this]() { m_window.close(); });
        m_window.set_close_request_callback([this]() { m_editor->confirm_exit(); });
    }

    bool load_project(std::string& error) {
        if (!m_project_persistence->load(m_project_path, error))
            return false;
        remember_recent_project();
        return true;
    }

    void register_default_panels() {
        m_scene = &m_editor->add_panel<editor::ui::scene_view>(
            m_window, m_camera, m_context
        );
        m_game_view = &m_editor->add_panel<editor::ui::game_view>(m_context);
        m_entities = &m_editor->add_panel<editor::ui::entities_view>(
            m_context,
            [](std::uint32_t id) { return "Entity " + std::to_string(id); }
        );
        m_components = &m_editor->add_panel<editor::ui::components_view>(m_context);

        auto drop_asset = [this](
            const std::filesystem::path& asset,
            const bgui::vec2i& mouse_position
        ) {
            if (m_context->is_playing || editor::ui::viewport_input_blocked_by_modal())
                return;
            try {
                if (editor::ui::is_lua_script(asset)) {
                    auto target = m_entities->entity_at(mouse_position);
                    if (!target && m_scene->is_open() &&
                        editor::ui::viewport_contains_drop(&m_scene->image(), mouse_position))
                        target = m_scene->entity_at(mouse_position);
                    if (target) {
                        std::string error;
                        if (editor::ui::attach_lua_script(m_context, *target, asset, error)) {
                            m_context->selected_entity = target;
                            m_components->refresh();
                            if (m_files)
                                m_files->set_asset_status("Script associado à entidade.");
                        } else if (m_files) {
                            m_files->set_asset_status(error);
                        }
                    } else if (!m_components->drop_asset(asset, mouse_position) && m_files) {
                        m_files->set_asset_status(
                            "Solte o script sobre um objeto na cena ou na lista de entidades."
                        );
                    }
                    return;
                }
                if (editor::ui::files_view::supports_image(asset) && m_scene->is_open() &&
                    editor::ui::viewport_contains_drop(&m_scene->image(), mouse_position)) {
                    const auto target = m_scene->entity_at(mouse_position);
                    std::string error;
                    if (target && editor::ui::apply_dropped_albedo(m_context, *target, asset, error)) {
                        m_context->selected_entity = target;
                        m_components->refresh();
                        if (m_files) m_files->set_asset_status("Textura aplicada ao objeto.");
                    } else if (m_files) {
                        m_files->set_asset_status(
                            error.empty() ? "Solte a imagem sobre um objeto." : error
                        );
                    }
                    return;
                }
                if (editor::ui::files_view::supports_model(asset))
                    (void)editor::ui::asset_dependencies(std::filesystem::canonical(asset), true);
                if (m_components->drop_asset(asset, mouse_position) ||
                    !editor::ui::files_view::supports_model(asset) || !m_scene->is_open() ||
                    !editor::ui::viewport_contains_drop(&m_scene->image(), mouse_position))
                    return;

                const auto position = editor::ui::scene_drop_position(
                    *m_camera, m_scene->image(), mouse_position
                );
                if (!position)
                    return;
                auto model = std::make_unique<commons::model>(asset.string());
                if (model->meshes.empty()) {
                    if (m_files)
                        m_files->show_model_importer(
                            asset, {}, "O carregador não conseguiu importar este modelo."
                        );
                    return;
                }
                auto entity = m_context->registry->create();
                m_context->registry->add<commons::renderer>(entity, model.release());
                m_context->registry->get<commons::transform>(entity.id)->set_position(*position);
                m_context->entity_names[entity.id] = asset.stem().string();
                m_context->record_entity_creation(entity.id);
                m_context->selected_entity = entity.id;
                m_entities->refresh();
                m_components->refresh();
                if (m_files) m_files->set_asset_status("Modelo adicionado à cena.");
            } catch (const std::exception& error) {
                if (m_files) {
                    m_files->set_asset_status(error.what());
                    if (editor::ui::files_view::supports_model(asset))
                        m_files->show_model_importer(asset, {}, error.what());
                }
            }
        };

        m_files = &m_editor->add_panel<editor::ui::files_view>(m_project_path, drop_asset);
        m_components->set_model_import_error_callback(
            [this](const auto& path, const auto& error) {
                bgui::add_function([this, path, error]() {
                    m_files->show_model_importer(path, {}, error);
                });
            }
        );
        m_files->set_model_imported_callback([this](const auto& path) {
            m_context->registry->cada<commons::renderer>([this, &path](auto id) {
                auto renderer = m_context->registry->get<commons::renderer>(id);
                if (renderer->m_modelo &&
                    std::filesystem::path(renderer->m_modelo->get_source_path()) == path)
                    renderer->m_modelo->load_model(path.string());
            });
            m_components->refresh();
        });
        m_files->set_file_operation_guard([this]() { return !m_context->is_playing; });
        m_files->set_file_moved_callback([this](const auto& from, const auto& to) {
            editor::ui::relocate_scene_asset(m_context, from, to);
            m_components->refresh();
        });
        m_window.set_file_drop_callback([this, drop_asset](const auto& paths, int x, int y) {
            if (m_context->is_playing || editor::ui::viewport_input_blocked_by_modal())
                return;
            const auto directory = m_files->folder_at({x, y});
            for (const auto& path : paths) {
                try {
                    const auto imported = m_files->import_file(
                        path, directory.value_or(std::filesystem::path{})
                    );
                    m_files->set_asset_status(
                        "Arquivo importado: " + imported.filename().string()
                    );
                    drop_asset(imported, {x, y});
                } catch (const std::exception& error) {
                    m_files->set_asset_status(error.what());
                    if (editor::ui::files_view::supports_model(path))
                        m_files->show_model_importer(
                            path, directory.value_or(std::filesystem::path{}), error.what(),
                            [drop_asset, x, y](const auto& imported) {
                                drop_asset(imported, {x, y});
                            }
                        );
                }
            }
        });
        bgui::get_layout().add<editor::ui::asset_drop_overlay, bgui::layer::overlay>(
            [this]() { return m_window.file_drag_hovered(); }, m_context
        );
        m_editor->add_panel<editor::ui::debug_view>();

        m_render_system = m_window.add<commons::render_system>();
        m_render_system->set_camera_filter([this](std::uint32_t entity) {
            return m_scene->needs_camera_render(entity) ||
                m_game_view->needs_camera_render(entity);
        });
        m_runtime_tools = &m_editor->add_panel<editor::ui::runtime_tools_view>(
            m_window, m_context, m_render_system
        );
        register_runtime_commands();
        bgui::get_layout().add<editor::ui::undo_redo_shortcuts_system>(m_commands);
    }

    bool load_workspace(std::string& error) {
        m_workspace = std::make_unique<editor::ui::editor_workspace>(m_ui_persistence);
        m_workspace->set_project_file(m_project_path);
        m_editor->set_project_loaded_callback([this](const std::string& path) {
            m_context->clear_history();
            m_context->selected_entity.reset();
            m_context->project_root = std::filesystem::absolute(path).parent_path();
            m_entities->refresh();
            m_components->refresh();
            std::string error;
            if (!m_workspace->save_workspace(*m_files, error))
                std::cerr << "[editor] " << error << '\n';
            m_files->set_project_file(path);
            m_editor->reset_ui();
            m_project_path = path;
            m_workspace->set_project_file(path);
            if (!m_workspace->load_workspace(*m_files, error))
                std::cerr << "[editor] " << error << '\n';
        });
        return m_workspace->load_workspace(*m_files, error);
    }

    int run() {
        m_window.loop();
        m_window.set_file_drop_callback({});

        std::string error;
        if (!m_workspace->save_workspace(*m_files, error))
            std::cerr << "[editor] " << error << '\n';
        std::string preview_error;
        if (!editor::ui::save_project_preview(m_project_path, m_camera, preview_error))
            std::cerr << "[editor] Could not save project preview: "
                      << preview_error << '\n';
        return 0;
    }

private:
    void remember_recent_project() const {
        std::vector<std::string> projects{m_project_path};
        std::ifstream previous("editor-recent-projects.cfg");
        for (std::string path; std::getline(previous, path);) {
            if (!path.empty() && path != m_project_path && projects.size() < 12)
                projects.push_back(path);
        }
        std::ofstream recent("editor-recent-projects.cfg", std::ios::trunc);
        for (const auto& path : projects)
            recent << path << '\n';
    }

    void register_runtime_commands() {
        m_runtime_process = std::make_shared<editor::ui::runtime_process>();
        const auto runtime_executable = m_executable.parent_path() /
#ifdef _WIN32
            "runtime_app.exe";
#else
            "runtime_app";
#endif
        m_editor->set_play_state_callback(
            [this, runtime_executable](const bool playing, std::string& error) {
                if (!playing) {
                    if (!m_runtime_process->stop(error))
                        return false;
                    m_context->is_playing = false;
                    return true;
                }
                std::error_code filesystem_error;
                const auto temp_directory =
                    std::filesystem::temp_directory_path(filesystem_error);
                if (filesystem_error) {
                    error = "Could not locate the temporary directory: " +
                        filesystem_error.message();
                    return false;
                }
                const auto project_file = temp_directory /
                    ("cpp-bengine-runtime-" +
                     std::to_string(std::chrono::steady_clock::now()
                         .time_since_epoch().count()) + ".bengine");
                if (!m_project_persistence->save(project_file.string(), error)) {
                    std::filesystem::remove(project_file, filesystem_error);
                    return false;
                }
                const bool started = m_runtime_process->start(
                    runtime_executable, project_file, error
                );
                if (started)
                    m_context->is_playing = true;
                return started;
            }
        );
        m_editor->set_runtime_running_callback([this](std::string& error) {
            return m_runtime_process->running(error);
        });
        m_editor->set_runtime_pause_callback(
            [this](const bool paused, std::string& error) {
                return m_runtime_process->set_paused(paused, error);
            }
        );
    }

    commons::window& m_window;
    std::string m_project_path;
    std::filesystem::path m_executable;
    std::shared_future<std::string> m_release_check;
    std::shared_ptr<commons::ecs> m_registry;
    std::uint32_t m_camera_entity{0};
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<editor::ui::editor_context> m_context;
    std::shared_ptr<editor::ui::editor_commands> m_commands;
    editor::ui::ui_persistence_system m_ui_persistence;
    std::unique_ptr<editor::ui::project_persistence_system> m_project_persistence;
    std::unique_ptr<editor::ui::editor_workspace> m_workspace;
    bgui::menu_bar* m_menu_bar{nullptr};
    bgui::dock* m_dock{nullptr};
    std::unique_ptr<editor::ui::editor_ui> m_editor;
    editor::ui::scene_view* m_scene{nullptr};
    editor::ui::game_view* m_game_view{nullptr};
    editor::ui::entities_view* m_entities{nullptr};
    editor::ui::components_view* m_components{nullptr};
    editor::ui::files_view* m_files{nullptr};
    editor::ui::runtime_tools_view* m_runtime_tools{nullptr};
    std::shared_ptr<commons::render_system> m_render_system;
    std::shared_ptr<editor::ui::runtime_process> m_runtime_process;
};

}

int main(int argc, char** argv) {
    std::string project_path = "project.bengine";
    if (argc == 3 && std::string(argv[1]) == "--project") {
        project_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: editor_app [--project <scene-file>]\n";
        return 2;
    }

    auto release_check = std::async(
        std::launch::async, editor::ui::check_latest_release
    ).share();
    commons::window window(
        "Bubble Engine " BUBBLE_VERSION_COMPLETA_STR " - Editor",
        argc == 1 ? 980 : 1200,
        600
    );
    window.add<commons::ui_system>();
    bgui::get_layout().add<editor::ui::ui_scale_system>();
    if (argc == 1) {
        bgui::linear* manager_root = nullptr;
        const auto choice = show_project_manager(window, manager_root, release_check);
        if (choice.path.empty())
            return 0;
        project_path = choice.path;
        if (choice.create)
            project_path = editor::ui::project_file_path(project_path).string();
    }

    bubble::editor_application application(
        window,
        project_path,
        locate_build_directory(argv[0]),
        std::filesystem::absolute(argv[0]),
        std::move(release_check)
    );
    std::string error;
    if (!application.load_project(error)) {
        std::cerr << "[editor] Could not load project: " << error << '\n';
        return 1;
    }
    application.register_default_panels();
    if (!application.load_workspace(error))
        std::cerr << "[editor] " << error << '\n';
    return application.run();
}
