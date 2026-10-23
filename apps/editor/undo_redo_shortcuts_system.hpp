#pragma once

#include "editor_context.hpp"

#include <os/os.hpp>

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

        if (m_context && z_down && !m_z_was_down) {
            if (shift_down)
                m_context->redo();
            else
                m_context->undo();
        }
        if (m_context && y_down && !m_y_was_down)
            m_context->redo();

        m_z_was_down = z_down;
        m_y_was_down = y_down;
    }

private:
    std::shared_ptr<editor_context> m_context;
    bool m_z_was_down{false};
    bool m_y_was_down{false};
};

}
