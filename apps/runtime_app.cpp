#include "profiling/profiler.hpp"
#include "systems/animation_system.hpp"
#include "ui/hud_view.hpp"
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "os/window.hpp"
#include "systems/code_system.hpp"
#include "debugging/script_log.hpp"
#include "systems/physics_system.hpp"
#include "systems/sound_system.hpp"
#include "guizmo/grid_gizmo.hpp"
#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"
#include "project/project_persistence_system.hpp"
#include "systems/system.hpp"

#include <bgui.hpp>
#include <os/resource_files.hpp>
#include <elem/image.hpp>
#include <elem/text.hpp>
#include <lay/linear.hpp>

#include <cstddef>
#include <array>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "runtime/game_bootstrap.hpp"
#include "runtime/test_capture.hpp"
#include "editor/systems/scene_loading_modal.hpp"
#include "editor/systems/runtime_scene_preview.hpp"
#include <chrono>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cstdlib>

namespace {

class runtime_preview_publisher final : public commons::system {
    std::filesystem::path m_path;
    std::chrono::steady_clock::time_point m_last{};
public:
    explicit runtime_preview_publisher(std::filesystem::path path) : m_path(std::move(path)) {}
    void setup(const std::shared_ptr<commons::ecs>&) override {}
    void update(const std::shared_ptr<commons::ecs>& registry) override {
        const auto now = std::chrono::steady_clock::now();
        if (now - m_last < std::chrono::milliseconds(33)) return;
        m_last = now;
        try {
            editor::ui::project_files::stage({m_path, editor::ui::capture_runtime_preview(registry)});
            editor::ui::project_files::replace(m_path);
        } catch (const std::exception& error) {
            std::cerr << "[runtime preview] " << error.what() << '\n';
        }
    }
};

class runtime_viewport_sync_system final : public commons::system {
public:
    runtime_viewport_sync_system(
        std::shared_ptr<commons::camera>& camera,
        bgui::image& image,
        int width = 0, int height = 0
    )
        : m_camera(camera), m_image(&image), m_width(width), m_height(height) {}

    void setup(const std::shared_ptr<commons::ecs>&) override {
        if (m_camera && !m_camera->flag_fb)
            m_camera->createFB();
        if (m_camera && m_image && m_camera->flag_fb) {
            m_image->set_external_texture(
                m_camera->framebuffer_texture(),
                bgui::vec2{
                    static_cast<float>(m_camera->viewportFBO.x),
                    static_cast<float>(m_camera->viewportFBO.y)
                }
            );
        }
    }

    void update(const std::shared_ptr<commons::ecs>&) override {
        if (!m_camera || !m_image)
            return;

        const int width = m_width ? m_width : m_image->processed_width();
        const int height = m_height ? m_height : m_image->processed_height();
        if (width <= 0 || height <= 0)
            return;

        if (m_camera->flag_fb && m_camera->viewportFBO.x == width &&
            m_camera->viewportFBO.y == height && m_texture == m_camera->framebuffer_texture()) {
            return;
        }

        m_camera->viewport({width, height});
        if (!m_camera->flag_fb) m_camera->createFB();
        m_texture = m_camera->framebuffer_texture();
        m_image->set_external_texture(
            m_camera->framebuffer_texture(),
            bgui::vec2{
                static_cast<float>(width),
                static_cast<float>(height)
            }
        );
    }

private:
    std::shared_ptr<commons::camera>& m_camera;
    bgui::image* m_image;
    unsigned int m_texture{};
    int m_width{}, m_height{};
};

}

namespace {
struct runtime_collision_overlay final : commons::system {
    editor::grid_gizmo gizmo;
    bool visible{false}, was_down{false};
    void setup(const std::shared_ptr<commons::ecs>&) override {}
    void update(const std::shared_ptr<commons::ecs>& registry) override {
        const bool down = bgui::get_pressed(bgui::input_key::f3);
        if (down && !was_down) visible = !visible;
        was_down = down;
        if (!visible) return;
        registry->cada<commons::camera>([&](uint32_t id) {
            auto camera = registry->get<commons::camera>(id);
            gizmo.draw_colliders(*camera, {0,0,camera->viewportFBO.x,camera->viewportFBO.y}, registry);
        });
    }
};
}

int main(const int argc, char** argv) {
    std::string project_path, script_log_path, test_spec_path, profile_json, profile_trace;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return 2;
        const std::string option = argv[i];
        if (option == "--project" && project_path.empty()) project_path = argv[i + 1];
        else if (option == "--script-log" && script_log_path.empty()) script_log_path = argv[i + 1];
        else if (option == "--profile-json" && profile_json.empty()) profile_json = argv[i + 1];
        else if (option == "--profile-trace" && profile_trace.empty()) profile_trace = argv[i + 1];
        else if (option == "--test-spec" && test_spec_path.empty()) test_spec_path = argv[i + 1];
        else return 2;
    }
    if (argc != 1 && project_path.empty()) return 2;
    Bubble::runtime_test_spec test_spec;
    bool test_failed = false;
    if (!test_spec_path.empty()) {
        try { test_spec.load(std::filesystem::absolute(test_spec_path)); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
        const auto log = test_spec.path.string() + ".log";
        if (!std::freopen(log.c_str(), "a", stdout) || !std::freopen(log.c_str(), "a", stderr)) return 2;
#ifdef _WIN32
        if (!std::freopen("NUL", "r", stdin)) return 2;
#else
        if (!std::freopen("/dev/null", "r", stdin)) return 2;
#endif
    }
    if (!script_log_path.empty()) commons::script_log::instance().set_output_file(script_log_path);

    std::jthread watchdog;
    if (!test_spec_path.empty()) watchdog = std::jthread([timeout = test_spec.timeout](std::stop_token stop) {
        std::mutex mutex;
        std::unique_lock lock(mutex);
        std::condition_variable_any wake;
        wake.wait_for(lock, stop, std::chrono::seconds(timeout), [] { return false; });
        if (!stop.stop_requested()) std::_Exit(124);
    });
    bool diagnostics = true;
    try {
        const auto executable = game_bootstrap::executable_path(argv[0]);
        game_bootstrap::mount_executable(executable);
        const auto directory = executable.parent_path();
        diagnostics = game_bootstrap::diagnostics_enabled(directory);
        if (!diagnostics) std::filesystem::current_path(directory);
        if (argc == 1 && !diagnostics) {
            const auto launch = game_bootstrap::resolve(directory);
            std::filesystem::current_path(launch.directory);
            project_path = launch.project.string();
        }
    } catch (const std::exception& error) {
        std::cerr << "[game] " << error.what() << '\n';
        return 1;
    }

    try {
    commons::window window("Bubble Engine - Runtime");
    auto registry = window.get_ecs();
    auto ui = window.add<commons::ui_system>();

    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera_transform = registry->get<commons::transform>(camera_entity.id);
    camera_transform->set_position({0.f, 0.f, 5.f});
    camera_transform->set_rotation({0.f, -90.f, 0.f});

    std::shared_ptr<commons::camera> game_camera =
        registry->get<commons::camera>(camera_entity.id);
    std::array<float, 3> gravity{0.f, -9.8f, 0.f};

    auto project = std::make_shared<editor::ui::project_persistence_system>(registry, camera_entity.id);
    project->set_migration_writes_enabled(false);
    if (project_path.empty()) {
        auto cube_entity = registry->create();
        registry->add<commons::renderer>(cube_entity, "assets/models/primitive-cube.bmodel");
        registry->get<commons::transform>(cube_entity.id)->set_position(
            {0.f, 0.f, 0.f}
        );
    } else {
        std::string error;
        if (!editor::ui::load_project_with_modal(*project, project_path, error)) {
            std::cerr << "[runtime] Could not load project: " << error << '\n';
            return 1;
        }
        gravity = project->global_gravity();

        for (const auto& [id, components] : registry->entities) {
            (void)components;
            if (id != camera_entity.id &&
                registry->get<commons::camera>(id)) {
                game_camera = registry->get<commons::camera>(id);
                break;
            }
        }
    }

    auto& root = bgui::get_layout().add<bgui::linear>(
        bgui::orientation::vertical
    );
    root.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    auto render_system = std::make_shared<commons::render_system>();
    render_system->set_camera_filter([&game_camera](std::uint32_t entity) {
        return entity == game_camera->my_object;
    });
    auto& game_view = root.add<bgui::image>();
    game_view.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::stretch
    );
    game_view.set_external_texture(
        game_camera->framebuffer_texture(),
        bgui::vec2{
            static_cast<float>(game_camera->viewportFBO.x),
            static_cast<float>(game_camera->viewportFBO.y)
        }
    );
    root.add<commons::hud_view, bgui::layer::overlay>(registry, game_camera, game_view,
        std::function<bool()>{}, [&game_camera] { return game_camera; });
    window.add<runtime_viewport_sync_system>(game_camera, game_view,
        static_cast<int>(test_spec.width), static_cast<int>(test_spec.height));
    if (!test_spec_path.empty()) window.resize(test_spec.width, test_spec.height);
    auto physics_system = std::make_shared<commons::physics_system>();
    physics_system->set_gravity({gravity[0], gravity[1], gravity[2]});
    auto scripts = std::make_shared<commons::code_system>(physics_system);
    auto sound = std::make_shared<commons::sound_system>(
        project_path.empty() ? std::filesystem::current_path() :
            std::filesystem::absolute(project_path).parent_path(),
        game_camera->my_object
    );
    std::string scene_project = project_path;
    auto initial_scene = project->active_scene();
    if (!script_log_path.empty() && !bgui::resource_files::embedded()) {
        rapidjson::Document snapshot;
        const auto contents = bgui::resource_files::text(project_path);
        snapshot.Parse(contents.data(), contents.size());
        if (snapshot.IsObject() && snapshot.HasMember("runtime_source_project") &&
            snapshot["runtime_source_project"].IsString() && snapshot.HasMember("runtime_source_scene") &&
            snapshot["runtime_source_scene"].IsString()) {
            scene_project = snapshot["runtime_source_project"].GetString();
            initial_scene = snapshot["runtime_source_scene"].GetString();
        }
    }
    if (!project_path.empty()) {
        registry->scenes = std::make_shared<commons::scene_control>();
        auto control = registry->scenes;
        const auto root_path = std::filesystem::absolute(scene_project).parent_path();
        control->active = initial_scene.lexically_relative(root_path).generic_string();
        control->load = [&, weak_control = std::weak_ptr<commons::scene_control>(control), root_path, scripts, sound](const std::string& requested) {
            const auto control = weak_control.lock();
            if (!control || control->loading) return false;
            const auto relative = std::filesystem::path(requested).lexically_normal();
            if (requested.empty() || relative.is_absolute() || relative.empty() || *relative.begin() == ".." ||
                (relative.extension() != ".scene" && relative.extension() != ".world") ||
                !bgui::resource_files::exists(root_path / relative)) {
                control->last_error = "Choose an existing scene inside the project.";
                return false;
            }
            control->loading = true;
            control->last_error.clear();
            window.defer([&, control, relative, root_path, scripts, sound] {
                try {
                    if (project->load_runtime_scene(scene_project, root_path / relative, control->last_error)) {
                        game_camera = registry->get<commons::camera>(camera_entity.id);
                        for (const auto& [id, components] : registry->entities)
                            if (id != camera_entity.id && registry->get<commons::camera>(id)) {
                                game_camera = registry->get<commons::camera>(id); break;
                            }
                        control->active = relative.generic_string();
                        auto gravity = project->global_gravity();
                        physics_system->set_gravity({gravity[0],gravity[1],gravity[2]});
                        physics_system->setup(registry);
                        sound->set_asset_root(root_path);
                        sound->set_camera_entity(game_camera->my_object);
                        scripts->setup(registry);
                    } else commons::script_log::instance().write(0, "Scene load failed: " + control->last_error);
                } catch (const std::exception& failure) {
                    control->last_error = failure.what();
                    commons::script_log::instance().write(0, "Scene load failed: " + control->last_error);
                }
                control->loading = false;
            });
            return true;
        };
    }
    window.add(scripts);
    window.add(physics_system);
    window.add(sound);
    if (!script_log_path.empty() && test_spec_path.empty()) window.add<runtime_preview_publisher>(project_path + ".preview");
    window.add<commons::animation_system>([&window] { return window.delta_time(); });
    window.add(render_system);
    if (diagnostics) window.add<runtime_collision_overlay>();
    if (!game_camera->flag_fb) {
        std::cerr << "[runtime] Could not create the game camera framebuffer.\n";
        return 1;
    }
    if (!test_spec_path.empty()) window.add<Bubble::runtime_test_capture>(window, game_camera, test_spec, test_failed);
    window.loop();

    if (registry->scenes) registry->scenes->load = {};
    for (const auto& [path, chrome] : {std::pair{profile_json, false}, std::pair{profile_trace, true}}) {
        if (path.empty()) continue;
        std::string error;
        if (!Bubble::profiler::instance().export_capture(path, chrome, error)) {
            std::cerr << "[profiler] " << error << '\n'; test_failed = true;
        }
    }
    return test_failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "[runtime] " << error.what() << '\n';
        return 1;
    }
}
