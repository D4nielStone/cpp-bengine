#include "os/window.hpp"
#include "systems/ui_system.hpp"

int main() {
    commons::window window("Bubble Engine - exemplo 1");
    window.add<commons::ui_system>();
    window.loop();

    return 0;
}