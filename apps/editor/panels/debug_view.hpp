#pragma once

#include "panel.hpp"
#include "debugging/script_log.hpp"
#include <elem/button.hpp>
#include <elem/text.hpp>
#include <lay/linear.hpp>
#include <elem/window.hpp>
#include <limits>

namespace editor::ui {

class debug_view final : public panel {
public:
    void build(bgui::dock& dock) override {
        auto& window = dock.add_window("Debug", bgui::dock_area::bottom, .3f);
        window.add_class("editor-panel");
        window.add_class("editor-surface");
        auto& content = window.add<bgui::linear>(bgui::orientation::vertical);
        content.add_class("editor-surface");
        content.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        content.add<bgui::button>("Limpar", .30f, [] { commons::script_log::instance().clear(); });
        auto& scroll = content.add<bgui::linear>(bgui::orientation::vertical);
        scroll.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        scroll.set_scrollable(true);
        m_text = &scroll.add<bgui::text>("", .30f);
        auto config = dock.get_configuration();
        config.tab_groups.push_back({"Arquivos", "Arquivos", {"Arquivos", "Debug"}});
        dock.apply_configuration(config);
        update();
    }

    void update() override {
        const auto& log = commons::script_log::instance();
        if (!m_text || m_revision == log.revision()) return;
        std::string text;
        for (const auto& line : log.lines()) { text += line; text += '\n'; }
        m_text->set_buffer(text.empty() ? "Nenhum log Lua." : text);
        m_revision = log.revision();
    }
private:
    bgui::text* m_text{nullptr};
    std::size_t m_revision{std::numeric_limits<std::size_t>::max()};
};

}
