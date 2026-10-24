#pragma once

#include <bgui.hpp>
#include <elem/window.hpp>

namespace editor::ui {

struct viewport_input_state {
    bool gizmo_active{false};
};

inline bool viewport_input_blocked_by_modal() {
    const auto& root_elements = bgui::get_layout().get_elements();
    const auto overlays = root_elements.find(bgui::layer::overlay);
    if (overlays == root_elements.end())
        return false;
    for (const auto& overlay : overlays->second) {
        if (overlay && overlay->is_enabled() && overlay->type == "modal")
            return true;
    }
    return false;
}

inline bgui::window* viewport_owner_window(const bgui::element* viewport) {
    auto* current = const_cast<bgui::element*>(viewport);
    while (current && current->type != "window")
        current = current->get_parent();
    return dynamic_cast<bgui::window*>(current);
}

inline bool viewport_owns_mouse(const bgui::element* viewport) {
    auto* owner = viewport_owner_window(viewport);
    for (auto* current = bgui::get_mouse_target(); current;
         current = current->get_parent()) {
        if (current == owner)
            return owner != nullptr;
    }
    return false;
}

inline bool mouse_over_viewport(const bgui::element* viewport) {
    if (!viewport || !viewport_owns_mouse(viewport))
        return false;
    const auto mouse = bgui::get_mouse_position();
    return mouse.x >= viewport->processed_x() &&
        mouse.y >= viewport->processed_y() &&
        mouse.x < viewport->processed_x() + viewport->processed_width() &&
        mouse.y < viewport->processed_y() + viewport->processed_height();
}

}
