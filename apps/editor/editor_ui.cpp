#include "editor_ui.hpp"

#include <elem/menu_bar.hpp>

#include <iostream>
#include <string>

namespace editor::ui {
namespace {

constexpr const char* ui_configuration_path = "editor-ui.cfg";
constexpr const char* project_path = "project.bengine";

void report_result(
    const char* action,
    const bool success,
    const std::string& error
) {
    if (success)
        std::clog << "[editor] " << action << " completed.\n";
    else
        std::cerr << "[editor] " << action << " failed: " << error << '\n';
}

}

editor_ui::editor_ui(
    bgui::dock& dock,
    bgui::menu_bar& menu_bar,
    ui_persistence_system& ui_persistence,
    project_persistence_system& project_persistence
)
    : m_dock(dock),
      m_project_persistence(project_persistence) {
    menu_bar.add_menu(
        "Files",
        0.3f,
        [this, ui_persistence_ptr = &ui_persistence](bgui::context_menu& menu) {
            menu.add_button("Save Project", [this]() {
                std::string error;
                const bool success = m_project_persistence.save(project_path, error);
                report_result("Saving project", success, error);
            });
            menu.add_button("Load Project", [this]() {
                std::string error;
                const bool success = m_project_persistence.load(project_path, error);
                report_result("Loading project", success, error);
                if (success && m_project_loaded_callback)
                    m_project_loaded_callback();
            });
            menu.add_separator();
            menu.add_button("Save UI", [ui_persistence_ptr]() {
                std::string error;
                const bool success = ui_persistence_ptr->save(ui_configuration_path, error);
                report_result("Saving UI", success, error);
            });
            menu.add_button("Load UI", [ui_persistence_ptr]() {
                std::string error;
                const bool success = ui_persistence_ptr->load(ui_configuration_path, error);
                report_result("Loading UI", success, error);
            });
        }
    );
}

void editor_ui::set_project_loaded_callback(std::function<void()> callback) {
    m_project_loaded_callback = std::move(callback);
}

void editor_ui::update() {
    for (auto& panel : m_panels)
        if (panel)
            panel->update();
}

}