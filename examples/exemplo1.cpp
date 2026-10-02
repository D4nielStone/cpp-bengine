#include "os/window.hpp"
#include "components/camera.hpp"
#include "systems/render_system.hpp"
#include "systems/ui_system.hpp"

int main() {
    commons::window window("Bubble Engine - exemplo 1");
    auto registry = window.get_ecs();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);

    window.add<commons::render_system>();
    window.add<commons::ui_system>();
    window.loop();

    return 0;
}