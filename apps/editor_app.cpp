#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "os/window.hpp"
#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"

#include <bgui.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>
#include <elem/image.hpp>
#include "systems/viewport_sync.hpp"

int main() {
    commons::window window("Bubble Engine - Editor");
    auto registry = window.get_ecs();

    auto camera_entity = registry->create();
    auto cube_entity = registry->create();

    registry->add<commons::camera>(camera_entity);
    registry->add<commons::renderer>(
        cube_entity,
        "assets/models/cube.obj"
    );

    auto camera = registry->get<commons::camera>(camera_entity.id);

    registry->get<commons::transform>(cube_entity.id)
        ->set_position({0.f, 0.f, 5.f});

    registry->get<commons::transform>(camera_entity.id)
        ->set_rotation({0.f, 90.f, 0.f});

    window.add<commons::render_system>();
    window.add<commons::ui_system>();

    auto& scene_window = bgui::get_layout()
        .add<bgui::dock>()
        .add_window("Scene", bgui::dock_area::center, 0.3f);

    auto& scene_context =
        scene_window.add<bgui::linear>(
            bgui::orientation::vertical
        );

    scene_context.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    scene_context.style.layout.padding = bgui::vec4i{0};

    auto& framebuffer_image =
        scene_context.add<bgui::image>();

    framebuffer_image.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    framebuffer_image.receives_input(true);

    if (!camera->flag_fb)
        camera->createFB();

    framebuffer_image.set_external_texture(
        camera->framebuffer_texture(),
        bgui::vec2{
            static_cast<float>(camera->viewportFBO.x),
            static_cast<float>(camera->viewportFBO.y)
        }
    );

    window.add<commons::viewport_sync_system>(
        camera,
        framebuffer_image
    );

    window.loop();
    return 0;
}