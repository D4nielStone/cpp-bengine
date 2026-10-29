#pragma once
#include "../core/editor_context.hpp"
#include "../systems/asset_drag_state.hpp"
#include "../systems/viewport_input_state.hpp"
#include <elem/text.hpp>
#include <lay/linear.hpp>
#include <functional>

namespace editor::ui {
class asset_drop_overlay final : public bgui::linear {
public:
    asset_drop_overlay(std::function<bool()> native_hover, std::shared_ptr<editor_context> context)
        : bgui::linear(bgui::orientation::vertical), m_native_hover(std::move(native_hover)),
          m_context(std::move(context)) {
        add_class("asset-drop-overlay");
        receives_input(false);
        set_flex(false);
        style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        style.visual.background.normal = bgui::color{.015f, .02f, .03f, .68f};
        style.visual.visible = false;
        m_title = &add<bgui::text>("Arraste aqui", .55f);
        m_hint = &add<bgui::text>("Arquivos serão copiados para o projeto", .3f);
        for (auto* text : {m_title, m_hint}) {
            text->receives_input(false);
            text->set_flex(false);
            text->set_wrap(false);
            text->style.layout.align = bgui::vec<2, bgui::alignment>{bgui::alignment::center, bgui::alignment::start};
            text->style.visual.text.normal = bgui::color{1.f, 1.f, 1.f, 1.f};
        }
    }
    void get_requires(bgui::draw_data* data) override {
        if (m_visible) bgui::linear::get_requires(data);
    }
    void on_update() override {
        const auto& size = bgui::get_context().m_size;
        const float scale = bgui::get_global_scale();
        set_final_rect(0, 0, size.x, size.y);
        m_title->set_final_rect(0, size.y / 2 - int(32 * scale), size.x, int(40 * scale));
        m_hint->set_final_rect(0, size.y / 2 + int(14 * scale), size.x, int(28 * scale));
        const bool visible = !m_context->is_playing && !viewport_input_blocked_by_modal() &&
            (asset_drag_active || (m_native_hover && m_native_hover()));
        if (visible)
            m_hint->set_buffer(asset_drag_active ? "Solte na cena ou sobre um objeto" :
                "Arquivos serão copiados para o projeto");
        if (m_visible != visible) {
            m_visible = visible;
            style.visual.visible = visible;
            mark_style_dirty();
        }
        bgui::linear::on_update();
    }
private:
    std::function<bool()> m_native_hover;
    std::shared_ptr<editor_context> m_context;
    bgui::text* m_title;
    bgui::text* m_hint;
    bool m_visible{false};
};
}
