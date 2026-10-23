#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"

#include "os/window.hpp"

#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"

#include "editor/editor_ui.hpp"
#include "editor/editor_context.hpp"
#include "editor/project_persistence_system.hpp"
#include "editor/scene_view.hpp"
#include "editor/runtime_tools_view.hpp"
#include "editor/entities_view.hpp"
#include "editor/components_view.hpp"
#include "editor/ui_persistence_system.hpp"
#include "editor/runtime_process.hpp"

#include <lay/linear.hpp>

#include <chrono>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
    commons::window window("Bubble Engine - Editor");
    auto registry = window.get_ecs();

    auto camera_entity = registry->create();
    auto cube_entity = registry->create();

    registry->add<commons::camera>(camera_entity);
    registry->add<commons::renderer>(cube_entity, "assets/models/cube.obj");

    auto camera = registry->get<commons::camera>(camera_entity.id);

    registry->get<commons::transform>(cube_entity.id)->set_position({0.f, 0.f, 0.f});
    registry->get<commons::transform>(camera_entity.id)->set_position({0.f, 0.f, 5.f});
    registry->get<commons::transform>(camera_entity.id)->set_rotation({0.f, -90.f, 0.f});

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
        camera_entity.id
    );
    editor::ui::editor_ui editor(
        dock,
        menu_bar,
        ui_persistence,
        project_persistence
    );
    auto editor_context = std::make_shared<editor::ui::editor_context>(registry);
    editor_context->editor_camera_entity = camera_entity.id;

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
        [&entities_panel, &components_panel, editor_context]() {
        for (const auto id : editor_context->hidden_entities) {
            if (auto renderer = editor_context->registry->get<commons::renderer>(id))
                renderer->visible = true;
        }
        editor_context->hidden_entities.clear();
        editor_context->locked_entities.clear();
        editor_context->selected_entity.reset();
        entities_panel.refresh();
        components_panel.refresh();
    });

    (void)scene;
    (void)entities_panel;
    (void)components_panel;
    (void)runtime_tools_panel;

    window.add<commons::ui_system>();

    window.loop();

    return 0;
}