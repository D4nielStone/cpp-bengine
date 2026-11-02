#pragma once

#include "panel.hpp"
#include "debugging/script_log.hpp"
#include <elem/button.hpp>
#include <elem/text.hpp>
#include <lay/linear.hpp>
#include <elem/window.hpp>
#include <functional>
#include <limits>

namespace editor::ui {

class debug_view_layout final : public bgui::linear {
public:
    debug_view_layout()
        : bgui::linear(bgui::orientation::vertical) {}

    std::function<void()> before_update;

    void on_update() override {
        if (before_update)
            before_update();
        bgui::linear::on_update();
    }
};

class debug_view final : public panel {
public:
    void build(bgui::dock& dock) override {
        auto& window = dock.add_window("Debug", bgui::dock_area::bottom, .3f);
        window.add_class("editor-panel");
        window.add_class("editor-surface");
        auto& content = window.add<debug_view_layout>();
        content.before_update = [this]() { update(); };
        content.add_class("editor-surface");
        content.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);

        auto& toolbar = content.add<bgui::linear>(bgui::orientation::horizontal);
        toolbar.add_class("debug-toolbar");
        toolbar.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        m_status = &toolbar.add<bgui::text>("0 registros", .27f);
        m_status->add_class("debug-log-status");
        m_status->style.layout.require_mode(bgui::mode::stretch, bgui::mode::wrap_content);
        auto& clear = toolbar.add<bgui::button>(
            "Limpar", .28f, [] { commons::script_log::instance().clear(); }
        );
        clear.add_class("debug-clear-button");
        clear.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);

        auto& scroll = content.add<bgui::linear>(bgui::orientation::vertical);
        scroll.add_class("debug-log-scroll");
        scroll.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        scroll.set_scrollable(true);
        m_text = &scroll.add<bgui::text>("", .30f);
        m_text->add_class("debug-log-text");
        m_text->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
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
        const auto count = log.lines().size();
        m_status->set_buffer(
            std::to_string(count) + (count == 1 ? " registro" : " registros")
        );
        m_revision = log.revision();
    }
private:
    bgui::text* m_text{nullptr};
    bgui::text* m_status{nullptr};
    std::size_t m_revision{std::numeric_limits<std::size_t>::max()};
};

}
