#pragma once
#include <bgui.hpp>
#include <memory>
#include <functional>
#include <vector>
#include <algorithm>
#include <string>
#include <utility>

namespace editor::ui {
using context_actions = std::vector<std::pair<std::string, std::function<void()>>>;

// The popup owns no row pointers and retires itself if its row is rebuilt.
class action_popup final : public bgui::linear {
    std::weak_ptr<bool> m_owner;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_closing{false};
    bgui::vec2i m_anchor;
public:
    action_popup(context_actions actions, std::weak_ptr<bool> owner, bgui::vec2i position)
        : bgui::linear(bgui::orientation::vertical), m_owner(std::move(owner)), m_anchor(position) {
        add_class("context-menu-popup");
        add_class("editor-context-actions");
        set_flex(false);
        style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);
        style.layout.limit_min = {220, 10};
        set_position(position.x, position.y);
        for (auto& [label, action] : actions) {
            auto& option = add<bgui::button>(label, .3f, [this, action]() {
                if (m_owner.expired()) { close(); return; }
                auto owner = m_owner;
                close();
                bgui::add_function([owner, action]() { if (!owner.expired()) action(); });
            });
            option.add_class("context-menu-option");
            option.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }
    }
    void close() {
        if (m_closing) return;
        m_closing = true;
        set_enable(false);
        std::weak_ptr<bool> alive = m_alive;
        bgui::add_function([this, alive]() { if (!alive.expired()) bgui::get_layout().remove(this); });
    }
    void on_update() override {
        bgui::linear::on_update();
        const auto size = bgui::get_context().m_size;
        set_position(std::clamp(m_anchor.x, 0, std::max(0, size.x - processed_width())),
            std::clamp(m_anchor.y, 0, std::max(0, size.y - processed_height())));
        if (m_owner.expired() || bgui::get_pressed(bgui::input_key::escape)) { close(); return; }
        bool inside = false;
        for (auto* target = bgui::get_mouse_target(); target; target = target->get_parent())
            if (target == this) inside = true;
        if (!inside && bgui::get_pressed(bgui::input_key::mouse_left)) close();
    }
};

inline void show_action_popup(context_actions actions, std::weak_ptr<bool> alive) {
    const auto mouse = bgui::get_mouse_position();
    bgui::add_function([actions = std::move(actions), alive, mouse]() mutable {
        if (alive.expired()) return;
        for (auto& element : bgui::get_layout().get_elements()[bgui::layer::overlay])
            if (auto* popup = dynamic_cast<action_popup*>(element.get())) popup->close();
        bgui::get_layout().add<action_popup, bgui::layer::overlay>(std::move(actions), alive, mouse);
    });
}

class context_action_surface : public bgui::linear {
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
public:
    using bgui::linear::linear;
    std::function<context_actions()> context_options;
    void open_context() {
        if (context_options) show_action_popup(context_options(), m_alive);
    }
    void on_released() override {
        bgui::linear::on_released();
        if (bgui::get_mouse_target() == this) open_context();
    }
};

class context_action_button : public bgui::button {
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_right_down{false};
public:
    using bgui::button::button;
    std::function<context_actions()> context_options;
    std::weak_ptr<bool> lifetime() const { return m_alive; }
    void open_context() {
        if (context_options) show_action_popup(context_options(), m_alive);
    }
    void on_update() override {
        bgui::button::on_update();
        const bool right = bgui::get_pressed(bgui::input_key::mouse_right);
        bool targeted = false;
        for (auto* target = bgui::get_mouse_target(); target; target = target->get_parent())
            if (target == this) targeted = true;
        if (right && !m_right_down && targeted && context_options) {
            open_context();
        }
        m_right_down = right;
    }
};
}
