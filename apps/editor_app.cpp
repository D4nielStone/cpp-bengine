#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"

#include "os/window.hpp"

#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"

#include "editor/editor_ui.hpp"
#include "editor/scene_view.hpp"
#include "editor/entities_view.hpp"

int main() {
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

    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::editor_ui editor(dock);

    auto& scene = editor.add_panel<editor::ui::scene_view>(window, camera);
    auto& entities_panel = editor.add_panel<editor::ui::entities_view>(
        registry,
        [&registry](std::uint32_t id) {
            auto transform = registry->get<commons::transform>(id);
            if (transform) {
                return "Entity " + std::to_string(id) +
                    " (Position: " +
                    std::to_string(transform->get_position().x) + ", " +
                    std::to_string(transform->get_position().y) + ", " +
                    std::to_string(transform->get_position().z) + ")";
            }

            return "Entity " + std::to_string(id);
        }
    );

    (void)scene;
    (void)entities_panel;

    window.add<commons::render_system>();
    window.add<commons::ui_system>();

    window.loop();

    return 0;
}