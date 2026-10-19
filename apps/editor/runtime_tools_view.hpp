#pragma once

#include "editor_context.hpp"
#include "panel.hpp"
#include "systems/render_system.hpp"
#include "os/window.hpp"

#include <elem/text.hpp>

#include <memory>

namespace editor::ui {

class runtime_tools_view final : public panel {
public:
    runtime_tools_view(
        commons::window& window,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<commons::render_system> render_system
    );

    void build(bgui::dock& dock) override;
    void update() override;

private:
    commons::window& m_window;
    std::shared_ptr<editor_context> m_context;
    std::shared_ptr<commons::render_system> m_render_system;
    bgui::text* m_runtime_status{nullptr};
    bgui::text* m_frame_profile{nullptr};
    bgui::text* m_render_profile{nullptr};
    bgui::text* m_scene_debug{nullptr};
};

}
