#pragma once

#include "editor_context.hpp"

#include <memory>
#include <utility>

namespace editor::ui {

enum class editor_command {
    undo,
    redo,
    toggle_orbital,
    toggle_colliders
};

class editor_commands final {
public:
    explicit editor_commands(std::shared_ptr<editor_context> context)
        : m_context(std::move(context)) {}

    std::shared_ptr<editor_context> context() const noexcept {
        return m_context;
    }

    bool execute(const editor_command command) const {
        if (!m_context)
            return false;

        switch (command) {
        case editor_command::undo:
            return m_context->undo();
        case editor_command::redo:
            return m_context->redo();
        case editor_command::toggle_orbital:
            m_context->orbital_view = !m_context->orbital_view;
            m_context->events.publish(editor_event::viewport);
            return true;
        case editor_command::toggle_colliders:
            m_context->show_colliders = !m_context->show_colliders;
            m_context->events.publish(editor_event::viewport);
            return true;
        }
        return false;
    }

private:
    std::shared_ptr<editor_context> m_context;
};

}
