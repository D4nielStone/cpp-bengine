#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace editor::ui {

enum class editor_event { scene, selection, assets, properties, viewport, history, count };

// Consumers keep their own cursor and can refresh once for several edits.
class editor_events {
public:
    std::uint64_t revision(editor_event event) const noexcept {
        return m_revisions[static_cast<std::size_t>(event)];
    }

    void publish(editor_event event) noexcept {
        ++m_revisions[static_cast<std::size_t>(event)];
    }

private:
    std::array<std::uint64_t, static_cast<std::size_t>(editor_event::count)> m_revisions{};
};

}
