#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "os/window.hpp"
#include "systems/render_system.hpp"

int main() {
    commons::window window("Bubble Engine - Runtime");
    auto registry = window.get_ecs();

    auto camera_entity = registry->create();
    auto cube_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    registry->add<commons::renderer>(cube_entity, "assets/models/cube.obj");
    registry->get<commons::transform>(cube_entity.id)->set_position({0.f, 0.f, 5.f});
    registry->get<commons::transform>(camera_entity.id)->set_rotation(commons::fvec3{0.f, 90.f, 0.f});

    window.add<commons::render_system>();
    window.loop();

    return 0;
}
