#pragma once

#include "panel.hpp"

#include <lay/dock.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace editor::ui {

class panel_manager final {
public:
    explicit panel_manager(bgui::dock& dock) : m_dock(dock) {}

    template <typename T, typename... Args>
    T& add(Args&&... args) {
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *value;
        result.build(m_dock);
        m_panels.push_back(std::move(value));
        return result;
    }

    void update() {
        for (auto& value : m_panels)
            if (value)
                value->update();
    }

private:
    bgui::dock& m_dock;
    std::vector<std::unique_ptr<editor_panel>> m_panels;
};

}
