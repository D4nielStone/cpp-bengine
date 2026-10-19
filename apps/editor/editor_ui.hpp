#pragma once

#include "panel.hpp"
#include "project_persistence_system.hpp"
#include "ui_persistence_system.hpp"

#include <bgui.hpp>
#include <lay/dock.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace editor::ui {

class editor_ui final {
public:
    editor_ui(
        bgui::dock& dock,
        bgui::menu_bar& menu_bar,
        ui_persistence_system& ui_persistence,
        project_persistence_system& project_persistence
    );

    void set_project_loaded_callback(std::function<void()> callback);

    template <typename T, typename... Args>
    T& add_panel(Args&&... args) {
        auto panel = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *panel;
        result.build(m_dock);
        m_panels.push_back(std::move(panel));
        return result;
    }

    void update();

private:
    bgui::dock& m_dock;
    project_persistence_system& m_project_persistence;
    std::function<void()> m_project_loaded_callback;
    std::vector<std::unique_ptr<panel>> m_panels;
};

}