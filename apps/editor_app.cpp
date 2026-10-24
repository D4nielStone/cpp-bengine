#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"

#include "os/window.hpp"

#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"

#include "editor/panels/editor_ui.hpp"
#include "editor/core/editor_context.hpp"
#include "editor/systems/project_persistence_system.hpp"
#include "editor/panels/files_view.hpp"
#include "editor/panels/scene_view.hpp"
#include "editor/panels/runtime_tools_view.hpp"
#include "editor/panels/entities_view.hpp"
#include "editor/panels/components_view.hpp"
#include "editor/systems/ui_persistence_system.hpp"
#include "editor/systems/runtime_process.hpp"
#include "editor/systems/undo_redo_shortcuts_system.hpp"

#include <lay/linear.hpp>
#include <elem/button.hpp>
#include <elem/inputbox.hpp>
#include <elem/text.hpp>

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <fstream>
#include <string>
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
    bgui::linear*& manager_root
) {
    project_choice choice;
    auto& root = bgui::get_layout().add<bgui::linear>(bgui::orientation::vertical);
    manager_root = &root;
    root.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    auto& header = root.add<bgui::linear>(bgui::orientation::vertical);
    header.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    header.style.layout.padding = bgui::vec4i{24, 16, 24, 8};
    header.add<bgui::text>("Bubble Engine", 1.2f);
    header.add<bgui::text>("Project Manager", 0.65f);

    auto& content = root.add<bgui::linear>(bgui::orientation::horizontal);
    content.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    auto& recent = content.add<bgui::linear>(bgui::orientation::vertical);
    recent.style.layout.require_width(bgui::mode::pixel, 360.f);
    recent.style.layout.require_height(bgui::mode::match_parent);
    recent.style.layout.padding = bgui::vec4i{20, 12, 14, 16};
    recent.add<bgui::text>("Recent Projects", 0.8f);

    std::vector<std::string> recent_projects;
    std::ifstream recent_file("editor-recent-projects.cfg");
    for (std::string line; std::getline(recent_file, line);) {
        if (!line.empty() && std::filesystem::is_regular_file(line))
            recent_projects.push_back(line);
    }
    if (recent_projects.empty())
        recent.add<bgui::text>("No recent projects", 0.55f);
    for (const auto& path : recent_projects) {
        auto& button = recent.add<bgui::button>(
            std::filesystem::path(path).stem().string() + "  —  " + path,
            0.55f,
            [&, path]() { choice = {path, false}; window.close(); }
        );
        button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        button.style.layout.set_padding(8, 5);
    }

    auto& actions = content.add<bgui::linear>(bgui::orientation::vertical);
    actions.style.layout.require_width(bgui::mode::stretch);
    actions.style.layout.require_height(bgui::mode::match_parent);
    actions.style.layout.padding = bgui::vec4i{20, 14, 20, 16};
    actions.style.visual.background.normal = bgui::color{0.075f, 0.082f, 0.095f, 1.f};
    actions.style.visual.border.normal = bgui::color{0.19f, 0.21f, 0.24f, 1.f};
    actions.style.visual.border_size = 1.f;
    actions.style.visual.border_radius = 5.f;
    actions.add<bgui::text>("Open or Create", 0.8f);
    actions.add<bgui::text>("Project file path", 0.55f);
    auto& path_row = actions.add<bgui::linear>(bgui::orientation::horizontal);
    path_row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& path_input = path_row.add<bgui::inputbox>("project.bengine", "Path to project file", 0.58f);
    path_input.style.layout.require_width(bgui::mode::stretch);
    path_input.style.layout.require_height(bgui::mode::wrap_content);

    auto& browser = actions.add<bgui::linear>(bgui::orientation::vertical);
    browser.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
    browser.style.layout.require_height(bgui::mode::pixel, 230.f);
    browser.style.layout.set_padding(6, 5);
    browser.style.visual.background.normal = bgui::color{0.045f, 0.05f, 0.06f, 1.f};
    browser.style.visual.border.normal = bgui::color{0.16f, 0.18f, 0.21f, 1.f};
    browser.style.visual.border_size = 1.f;
    browser.style.visual.border_radius = 4.f;
    browser.set_scrollable(true);
    browser.set_enable(false);
    std::filesystem::path browser_directory;
    std::function<void(const std::filesystem::path&)> refresh_browser;
    refresh_browser = [&](const std::filesystem::path& requested) {
        std::error_code error;
        auto directory = std::filesystem::absolute(requested, error).lexically_normal();
        if (error || !std::filesystem::is_directory(directory, error) || error)
            return;
        browser_directory = directory;
        for (auto& [layer, elements] : browser.get_elements()) {
            (void)layer;
            while (!elements.empty())
                browser.remove(elements.back().get());
        }
        auto& location = browser.add<bgui::text>(directory.string(), 0.48f);
        location.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        if (directory.has_parent_path() && directory.parent_path() != directory) {
            auto& parent = browser.add<bgui::button>("..  Parent folder", 0.48f, [&]() {
                refresh_browser(browser_directory.parent_path());
            });
            parent.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }
        std::vector<std::filesystem::path> directories;
        std::vector<std::filesystem::path> project_files;
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            std::error_code entry_error;
            if (it->is_directory(entry_error) && !entry_error) {
                directories.push_back(it->path());
            } else if (!entry_error && it->is_regular_file(entry_error) && !entry_error) {
                const auto extension = it->path().extension().string();
                if (extension == ".bengine" || extension == ".world" || extension == ".scene")
                    project_files.push_back(it->path());
            }
        }
        const auto by_name = [](const auto& left, const auto& right) {
            return left.filename().string() < right.filename().string();
        };
        std::sort(directories.begin(), directories.end(), by_name);
        std::sort(project_files.begin(), project_files.end(), by_name);
        for (const auto& entry : directories) {
            auto& item = browser.add<bgui::button>("[Folder] " + entry.filename().string(), 0.48f, [&, entry]() {
                refresh_browser(entry);
            });
            item.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }
        for (const auto& entry : project_files) {
            auto& item = browser.add<bgui::button>("[Project] " + entry.filename().string(), 0.48f, [&, entry]() {
                path_input.set_buffer(entry.string());
            });
            item.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }
    };
    auto& browse_button = path_row.add<bgui::button>("Browse...", 0.58f, [&]() {
        const auto current = path_input.get_buffer().empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(path_input.get_buffer()).parent_path();
        refresh_browser(current.empty() ? std::filesystem::current_path() : current);
        browser.set_enable(true);
    });
    browse_button.style.layout.require_width(bgui::mode::pixel, 104.f);
    browse_button.style.layout.require_height(bgui::mode::wrap_content);
    auto& open_button = actions.add<bgui::button>("Open Project", 0.58f, [&]() {
        choice = {path_input.get_buffer(), false};
        window.close();
    });
    open_button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    open_button.style.layout.set_padding(8, 5);
    auto& create_button = actions.add<bgui::button>("Create Project", 0.58f, [&]() {
        choice = {path_input.get_buffer(), true};
        window.close();
    });
    create_button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    create_button.style.layout.set_padding(8, 5);

    window.loop();
    window.reopen();
    bgui::get_layout().remove(manager_root);
    manager_root = nullptr;
    window.resize(1200, 600);
    return choice;
}

}

int main(int argc, char** argv) {
    std::string project_path = "project.bengine";
    bool create_project = false;
    if (argc == 3 && std::string(argv[1]) == "--project") {
        project_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: editor_app [--project <scene-file>]\n";
        return 2;
    }

    commons::window window("Bubble Engine - Editor", argc == 1 ? 980 : 1200, 600);
    window.add<commons::ui_system>();

    if (argc == 1) {
        bgui::linear* manager_root = nullptr;
        const auto choice = show_project_manager(window, manager_root);
        if (choice.path.empty())
            return 0;
        project_path = choice.path;
        create_project = choice.create;
        if (create_project) {
            std::error_code error;
            const auto parent = std::filesystem::path(project_path).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, error);
            if (error) {
                std::cerr << "[editor] Could not create project folder: " << error.message() << '\n';
                return 1;
            }
            if (std::filesystem::exists(project_path)) {
                std::cerr << "[editor] Project file already exists: " << project_path << '\n';
                return 1;
            }
        }
    }

    auto registry = window.get_ecs();

    auto camera_entity = registry->create();
    auto cube_entity = registry->create();

    registry->add<commons::camera>(camera_entity);
    registry->add<commons::renderer>(cube_entity, "assets/models/cube.obj");

    auto camera = registry->get<commons::camera>(camera_entity.id);

    registry->get<commons::transform>(cube_entity.id)->set_position({0.f, 0.f, 0.f});
    registry->get<commons::transform>(camera_entity.id)->set_position({0.f, 0.f, 5.f});
    registry->get<commons::transform>(camera_entity.id)->set_rotation({0.f, -90.f, 0.f});

    auto editor_context = std::make_shared<editor::ui::editor_context>(registry);
    editor_context->editor_camera_entity = camera_entity.id;

    auto& editor_root = bgui::get_layout().add<bgui::linear>(
        bgui::orientation::vertical
    );
    editor_root.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );
    auto& menu_bar = editor_root.add<bgui::menu_bar>(editor_root);
    auto& dock = editor_root.add<bgui::dock>();
    dock.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    editor::ui::ui_persistence_system ui_persistence;
    editor::ui::project_persistence_system project_persistence(
        registry,
        camera_entity.id,
        &editor_context->entity_names,
        &editor_context->hidden_entities,
        &editor_context->locked_entities
    );
    if (argc == 3 || (argc == 1 && !create_project)) {
        std::string error;
        if (!project_persistence.load(project_path, error)) {
            std::cerr << "[editor] Could not load project: " << error << '\n';
            return 1;
        }
    }
    if (create_project) {
        std::string error;
        if (!project_persistence.save(project_path, error)) {
            std::cerr << "[editor] Could not create project: " << error << '\n';
            return 1;
        }
    }
    {
        std::vector<std::string> recent_projects{project_path};
        std::ifstream previous_recent_file("editor-recent-projects.cfg");
        for (std::string path; std::getline(previous_recent_file, path);) {
            if (!path.empty() && path != project_path && recent_projects.size() < 12)
                recent_projects.push_back(path);
        }
        std::ofstream recent_file("editor-recent-projects.cfg", std::ios::trunc);
        for (const auto& path : recent_projects)
            recent_file << path << '\n';
    }
    editor::ui::editor_ui editor(
        dock,
        menu_bar,
        ui_persistence,
        project_persistence,
        registry,
        project_path,
        locate_build_directory(argv[0])
    );
    editor.set_history_callbacks(
        [editor_context]() { editor_context->undo(); },
        [editor_context]() { editor_context->redo(); }
    );
    editor.set_exit_callback([&window]() { window.close(); });
    window.set_close_request_callback(
        [&editor]() { editor.confirm_exit(); }
    );

    auto& scene = editor.add_panel<editor::ui::scene_view>(
        window,
        camera,
        editor_context
    );
    auto& entities_panel = editor.add_panel<editor::ui::entities_view>(
        editor_context,
        [](std::uint32_t id) {
            return "Entity " + std::to_string(id);
        }
    );
    auto& components_panel = editor.add_panel<editor::ui::components_view>(
        editor_context
    );
    auto& files_panel = editor.add_panel<editor::ui::files_view>(
        project_path,
        [&scene, editor_context, &entities_panel, &components_panel](
            const std::filesystem::path& asset,
            const bgui::vec2i& mouse_position
        ) {
            if (!scene.is_open())
                return;

            const auto viewport = scene.image().processed_rect();
            if (mouse_position.x < viewport.x ||
                mouse_position.x > viewport.x + viewport.z ||
                mouse_position.y < viewport.y ||
                mouse_position.y > viewport.y + viewport.w)
                return;

            auto model_path = asset.string();
            std::error_code error;
            const auto current_directory = std::filesystem::current_path(error);
            const auto relative_path = error
                ? std::filesystem::path{}
                : asset.lexically_relative(current_directory);
            if (!relative_path.empty())
                model_path = relative_path.string();

            auto entity = editor_context->registry->create();
            editor_context->registry->add<commons::renderer>(
                entity,
                model_path.c_str()
            );
            editor_context->entity_names[entity.id] = asset.stem().string();
            editor_context->record_entity_creation(entity.id);
            editor_context->selected_entity = entity.id;
            entities_panel.refresh();
            components_panel.refresh();
        }
    );
    auto render_system = window.add<commons::render_system>();
    auto& runtime_tools_panel =
        editor.add_panel<editor::ui::runtime_tools_view>(
            window,
            editor_context,
            render_system
        );
    auto runtime_process = std::make_shared<editor::ui::runtime_process>();
    auto runtime_executable = std::filesystem::absolute(argv[0]).parent_path() /
#ifdef _WIN32
        "runtime_app.exe";
#else
        "runtime_app";
#endif
    editor.set_play_state_callback(
        [runtime_process, runtime_executable, &project_persistence, editor_context](
            const bool playing,
            std::string& error
        ) {
            if (!playing) {
                if (!runtime_process->stop(error))
                    return false;
                editor_context->is_playing = false;
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
                 std::to_string(
                     std::chrono::steady_clock::now()
                         .time_since_epoch()
                         .count()
                 ) +
                 ".bengine");
            if (!project_persistence.save(project_file.string(), error)) {
                std::filesystem::remove(project_file, filesystem_error);
                return false;
            }

            const bool started = runtime_process->start(
                runtime_executable,
                project_file,
                error
            );
            if (started)
                editor_context->is_playing = true;
            return started;
        }
    );
    editor.set_runtime_running_callback(
        [runtime_process](std::string& error) {
            return runtime_process->running(error);
        }
    );
    editor.set_runtime_pause_callback(
        [runtime_process](const bool paused, std::string& error) {
            return runtime_process->set_paused(paused, error);
        }
    );
    editor.set_project_loaded_callback(
        [&entities_panel, &components_panel, &files_panel, editor_context](
            const std::string& loaded_project_path
        ) {
        editor_context->clear_history();
        editor_context->selected_entity.reset();
        entities_panel.refresh();
        components_panel.refresh();
        files_panel.set_project_file(loaded_project_path);
    });

    (void)scene;
    (void)entities_panel;
    (void)components_panel;
    (void)files_panel;
    (void)runtime_tools_panel;

    bgui::get_layout().add<editor::ui::undo_redo_shortcuts_system>(editor_context);

    window.loop();

    return 0;
}
