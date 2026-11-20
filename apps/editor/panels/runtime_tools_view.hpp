#pragma once
#include "editor_context.hpp"
#include "panel.hpp"
#include "systems/render_system.hpp"
#include "os/window.hpp"
#include "profiling/profiler.hpp"
#include <elem/text.hpp>
#include <memory>
namespace editor::ui {
// Retain the panel type for existing editor integrations and workspace ownership.
class runtime_tools_view final : public panel {
public:
    runtime_tools_view(commons::window&, std::shared_ptr<editor_context>, std::shared_ptr<commons::render_system>);
    void build(bgui::dock&) override;
    void update() override;
private:
    std::shared_ptr<editor_context> m_context;
    bgui::text* m_summary{};
    bgui::text* m_details{};
    std::vector<Bubble::profile_frame> m_capture, m_baseline;
    std::string m_filter, m_status;
    std::size_t m_selected{};
    bool m_paused{};
    std::chrono::steady_clock::time_point m_refresh{};
};
}
