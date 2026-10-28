#pragma once
#include "lua_completion.hpp"
#include <bgui.hpp>
#include <elem/inputbox.hpp>
#include <elem/button.hpp>
#include <os/os.hpp>
#include <array>
#include <cmath>

namespace editor::ui {
class lua_inputbox : public bgui::inputbox {
public:
    explicit lua_inputbox(const std::string& buffer)
        : inputbox(buffer, "", 0.28f, nullptr, bgui::input_mode::multiline) {
        m_popup = &add<bgui::linear, bgui::layer::overlay>(bgui::orientation::vertical);
        m_popup->add_class("context-menu-popup");
        m_popup->add_class("lua-completion-popup");
        m_popup->set_flex(false);
        m_popup->set_scrollable(true);
        m_popup->style.layout.require_mode(bgui::mode::pixel, bgui::mode::pixel);
        m_popup->style.visual.background.normal = bgui::color{0.10f, 0.11f, 0.14f, 1.f};
        m_popup->set_enable(false);
    }

    void on_update() override {
        auto& io = bgui::get_context();
        constexpr std::array keys{bgui::input_key::up, bgui::input_key::down,
            bgui::input_key::enter, bgui::input_key::tab, bgui::input_key::escape};
        std::array<bgui::input_action, keys.size()> saved;
        const bool was_open = m_popup->is_enabled() && is_focused();
        for (std::size_t i = 0; i < keys.size(); ++i) {
            saved[i] = io.m_input_map[keys[i]];
            const bool down = saved[i] == bgui::input_action::press || saved[i] == bgui::input_action::repeat;
            if (was_open) {
                if (down && !m_key_down[i] && m_popup->is_enabled()) {
                    if (i == 0) m_selected = (m_selected + m_suggestions.size() - 1) % m_suggestions.size();
                    else if (i == 1) m_selected = (m_selected + 1) % m_suggestions.size();
                    else if (i == 2 || i == 3) accept(m_selected);
                    else dismiss();
                }
                m_consumed[i] = down;
            }
            if (!down) m_consumed[i] = false;
            if (m_consumed[i]) io.m_input_map[keys[i]] = bgui::input_action::none;
            m_key_down[i] = down;
        }
        inputbox::on_update();
        for (std::size_t i = 0; i < keys.size(); ++i) io.m_input_map[keys[i]] = saved[i];
        const auto mouse = bgui::get_mouse_position();
        const bool popup_hover = m_popup->is_enabled() &&
            mouse.x >= m_popup->processed_x() && mouse.x < m_popup->processed_x() + m_popup->processed_width() &&
            mouse.y >= m_popup->processed_y() && mouse.y < m_popup->processed_y() + m_popup->processed_height();
        if (!is_focused()) {
            if (!popup_hover) m_popup->set_enable(false);
            return;
        }
        if (m_dismissed_buffer == m_input_buffer && m_dismissed_cursor == m_cursor_position) return;
        const auto completion = complete_lua(m_input_buffer, m_cursor_position);
        if (!completion) { m_popup->set_enable(false); return; }
        m_begin = completion->begin;
        m_end = completion->end;
        if (completion->members != m_suggestions) {
            auto& children = m_popup->get_elements()[bgui::layer::base];
            while (!children.empty()) m_popup->remove(children.back().get());
            m_suggestions = completion->members;
            m_selected = 0;
            for (std::size_t i = 0; i < m_suggestions.size(); ++i) {
                auto& option = m_popup->add<bgui::button>(m_suggestions[i], 0.28f, [this, i]() { accept(i); });
                option.add_class("context-menu-option");
                option.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
                option.style.layout.require_height(bgui::mode::pixel, 25.f);
            }
        }
        std::size_t i = 0;
        for (auto& child : m_popup->get_elements()[bgui::layer::base]) {
            child->style.visual.background.normal = i++ == m_selected
                ? bgui::color{0.20f, 0.30f, 0.46f, 1.f} : bgui::color{0.10f, 0.11f, 0.14f, 1.f};
            child->mark_style_dirty();
        }
        m_popup->set_enable(true);
        mark_layout_dirty();
    }

    void arrange() override {
        inputbox::arrange();
        if (!m_popup->is_enabled()) return;
        const float scale = bgui::get_global_scale();
        const int width = std::min(processed_width(), static_cast<int>(240 * scale));
        const int height = std::min(processed_height(), static_cast<int>(m_suggestions.size() * 25 * scale));
        const auto line_start = m_cursor_position == 0 ? std::string::npos : m_input_buffer.rfind('\n', m_cursor_position - 1);
        const auto start = line_start == std::string::npos ? 0 : line_start + 1;
        const int row = static_cast<int>(std::count(m_input_buffer.begin(), m_input_buffer.begin() + m_cursor_position, '\n'));
        const int lines = 1 + static_cast<int>(std::count(m_input_buffer.begin(), m_input_buffer.end(), '\n'));
        const int line_height = std::max(1, get_label().get_content_size().y / lines);
        int x = get_label().processed_x() + static_cast<int>(get_label().get_text_width(m_input_buffer.substr(start, m_cursor_position - start)));
        int y = get_label().processed_y() + (row + 1) * line_height;
        if (y + height > processed_y() + processed_height()) y -= height + line_height;
        x = std::clamp(x, processed_x(), processed_x() + processed_width() - width);
        y = std::clamp(y, processed_y(), processed_y() + processed_height() - height);
        m_popup->set_final_rect(x, y, width, height);
        m_popup->arrange();
    }

private:
    bgui::linear* m_popup{};
    std::vector<std::string> m_suggestions;
    std::size_t m_selected{}, m_begin{}, m_end{};
    std::array<bool, 5> m_key_down{}, m_consumed{};
    std::string m_dismissed_buffer;
    std::size_t m_dismissed_cursor{std::string::npos};

    void dismiss() {
        m_popup->set_enable(false);
        m_dismissed_buffer = m_input_buffer;
        m_dismissed_cursor = m_cursor_position;
    }
    void accept(std::size_t index) {
        if (index >= m_suggestions.size()) return;
        m_undo_stack.push_back({m_input_buffer, m_cursor_position, m_selection_anchor});
        m_redo_stack.clear();
        m_input_buffer.replace(m_begin, m_end - m_begin, m_suggestions[index]);
        m_cursor_position = m_begin + m_suggestions[index].size();
        clear_selection();
        bgui::focus_keyboard(*this);
        dismiss();
        update_display();
    }
};
}
