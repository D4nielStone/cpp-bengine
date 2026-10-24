#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "os/window.hpp"
#include "systems/code_system.hpp"
#include "systems/physics_system.hpp"
#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"
#include "editor/systems/project_persistence_system.hpp"
#include "systems/system.hpp"

#include <bgui.hpp>
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

namespace {

class runtime_viewport_sync_system final : public commons::system {
public:
    runtime_viewport_sync_system(
        std::shared_ptr<commons::camera> camera,
        bgui::image& image
    )
        : m_camera(std::move(camera)),
          m_image(&image) {}

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

        const int width = m_image->processed_width();
        const int height = m_image->processed_height();
        if (width <= 0 || height <= 0)
            return;

        if (m_camera->viewportFBO.x == width &&
            m_camera->viewportFBO.y == height) {
            return;
        }

        m_camera->viewport({width, height});
        m_image->set_external_texture(
            m_camera->framebuffer_texture(),
            bgui::vec2{
                static_cast<float>(width),
                static_cast<float>(height)
            }
        );
    }

private:
    std::shared_ptr<commons::camera> m_camera;
    bgui::image* m_image;
};

class runtime_profiler_text final : public bgui::text {
public:
    runtime_profiler_text(
        commons::window& window,
        std::shared_ptr<commons::render_system> render_system,
        std::shared_ptr<commons::ecs> registry
    )
        : bgui::text("", 0.3f),
          m_window(window),
          m_render_system(std::move(render_system)),
          m_registry(std::move(registry)) {}

    void on_update() override {
        const double frame_time = m_window.delta_time();
        m_fps_elapsed += frame_time;
        ++m_frame_count;
        if (m_fps_elapsed >= 1.0) {
            m_fps = static_cast<double>(m_frame_count) / m_fps_elapsed;
            m_fps_elapsed = 0.0;
            m_frame_count = 0;

            std::ostringstream stats;
            const double render_time = m_render_system->last_render_time_ms();
            const std::size_t render_count =
                m_render_system->last_rendered_entity_count();
            const std::size_t camera_count = m_render_system->last_camera_count();
            stats << std::fixed << std::setprecision(2)
                  << "Runtime | " << frame_time * 1000.0 << " ms | "
                  << std::setprecision(0) << m_fps << " FPS | Render "
                  << std::setprecision(2)
                  << render_time << " ms | "
                  << render_count << " draws | "
                  << m_registry->entities.size() << " entities | "
                  << camera_count << " cameras";
            set_buffer(stats.str());
        }
        bgui::text::on_update();
    }

private:
    commons::window& m_window;
    std::shared_ptr<commons::render_system> m_render_system;
    std::shared_ptr<commons::ecs> m_registry;
    double m_fps_elapsed{0.0};
    double m_fps{0.0};
    std::size_t m_frame_count{0};
};

}

int main(const int argc, char** argv) {
    std::string project_path;
    if (argc == 3 && std::string(argv[1]) == "--project") {
        project_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: runtime_app [--project <project-file>]\n";
        return 2;
    }

    commons::window window("Bubble Engine - Runtime");
    auto registry = window.get_ecs();

    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera_transform = registry->get<commons::transform>(camera_entity.id);
    camera_transform->set_position({0.f, 0.f, 5.f});
    camera_transform->set_rotation({0.f, -90.f, 0.f});

    std::shared_ptr<commons::camera> game_camera =
        registry->get<commons::camera>(camera_entity.id);
    std::array<float, 3> gravity{0.f, -9.8f, 0.f};

    if (project_path.empty()) {
        auto cube_entity = registry->create();
        registry->add<commons::renderer>(cube_entity, "assets/models/cube.obj");
        registry->get<commons::transform>(cube_entity.id)->set_position(
            {0.f, 0.f, 0.f}
        );
    } else {
        editor::ui::project_persistence_system project(
            registry,
            camera_entity.id
        );
        std::string error;
        if (!project.load(project_path, error)) {
            std::cerr << "[runtime] Could not load project: " << error << '\n';
            return 1;
        }
        gravity = project.global_gravity();

        for (const auto& [id, components] : registry->entities) {
            (void)components;
            if (id != camera_entity.id &&
                registry->get<commons::camera>(id)) {
                game_camera = registry->get<commons::camera>(id);
                break;
            }
        }
        if (game_camera->my_object != camera_entity.id)
            registry->remove(camera_entity.id);
    }

    auto& root = bgui::get_layout().add<bgui::linear>(
        bgui::orientation::vertical
    );
    root.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    auto render_system = std::make_shared<commons::render_system>();
    auto& profiler = root.add<runtime_profiler_text>(
        window,
        render_system,
        registry
    );
    profiler.style.layout.require_mode(
        bgui::mode::wrap_content,
        bgui::mode::wrap_content
    );
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
    window.add<runtime_viewport_sync_system>(game_camera, game_view);
    window.add<commons::code_system>();
    auto physics_system = std::make_shared<commons::physics_system>();
    physics_system->set_gravity({gravity[0], gravity[1], gravity[2]});
    window.add(physics_system);
    window.add(render_system);
    if (!game_camera->flag_fb) {
        std::cerr << "[runtime] Could not create the game camera framebuffer.\n";
        return 1;
    }
    window.add<commons::ui_system>();
    window.loop();

    return 0;
}
