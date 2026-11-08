#include "ui_persistence_system.hpp"

#include <bgui.hpp>
#include "localized_layout.hpp"

namespace editor::ui {

bool ui_persistence_system::save(
    const std::string& path,
    std::string& error
) const {
    error.clear();
    canonical_layout_titles titles;
    if (bgui::save_configuration(path))
        return true;

    error = "BGUI could not save the interface configuration to '" + path + "'.";
    return false;
}

bool ui_persistence_system::load(
    const std::string& path,
    std::string& error
) const {
    error.clear();
    canonical_layout_titles titles;
    if (bgui::load_configuration(path))
        return true;

    error = "BGUI could not load the interface configuration from '" + path + "'.";
    return false;
}

}
