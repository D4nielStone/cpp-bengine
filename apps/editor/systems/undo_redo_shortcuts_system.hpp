#pragma once

#include "editor_context.hpp"
#include "entity_clipboard.hpp"
#include "viewport_input_state.hpp"

#include <os/os.hpp>
#include <bgui.hpp>
#include <elem/inputbox.hpp>

#include <memory>
#include <utility>

namespace editor::ui {

class undo_redo_shortcuts_system final : public bgui::element {
public:
    explicit undo_redo_shortcuts_system(
        std::shared_ptr<editor_context> context
    )
        : m_context(std::move(context)) {
        receives_input(false);
        set_flex(false);
    }

    void on_update() override {
        const bool f3_down = bgui::get_pressed(bgui::input_key::f3);
        if (m_context && f3_down && !m_f3_was_down)
            m_context->show_colliders = !m_context->show_colliders;
        m_f3_was_down = f3_down;

        const bool control_down =
            bgui::get_pressed(bgui::input_key::left_control) ||
            bgui::get_pressed(bgui::input_key::right_control);
        const bool z_down = control_down &&
            bgui::get_pressed(bgui::input_key::z);
        const bool y_down = control_down &&
            bgui::get_pressed(bgui::input_key::y);
        const bool shift_down =
            bgui::get_pressed(bgui::input_key::left_shift) ||
            bgui::get_pressed(bgui::input_key::right_shift);

        const bool editing_text = dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus()) != nullptr;
        if (!editing_text && m_context && z_down && !m_z_was_down) {
            if (shift_down)
                m_context->redo();
            else
                m_context->undo();
        }
        if (!editing_text && m_context && y_down && !m_y_was_down)
            m_context->redo();

        const bool copy_down = control_down && bgui::get_pressed(bgui::input_key::c);
        const bool paste_down = control_down && bgui::get_pressed(bgui::input_key::v);
        if (!editing_text && m_context && !m_context->is_playing && !viewport_input_blocked_by_modal()) {
            std::string error;
            if (copy_down && !m_c_was_down) m_clipboard.copy(m_context, error);
            if (paste_down && !m_v_was_down) m_clipboard.paste(m_context, error);
            if (!error.empty()) {
                bgui::add_function([error]() {
                    auto& modal = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
                    modal.add<bgui::text>(error, .3f);
                    modal.set_confirmation_text("Fechar");
                });
            }
        }
        m_c_was_down = copy_down;
        m_v_was_down = paste_down;
        m_z_was_down = z_down;
        m_y_was_down = y_down;
    }

private:
    std::shared_ptr<editor_context> m_context;
    entity_clipboard m_clipboard;
    bool m_c_was_down{false}, m_v_was_down{false};
    bool m_z_was_down{false};
    bool m_y_was_down{false};
    bool m_f3_was_down{false};
};

}
