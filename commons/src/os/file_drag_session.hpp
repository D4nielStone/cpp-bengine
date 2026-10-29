#pragma once
#include <cstdint>

namespace commons::detail {
// Tracks an incoming file offer separately from a possibly retained native selection.
class file_drag_session {
public:
    void enter(std::uintptr_t source, bool contains_files) {
        m_source = contains_files ? source : 0;
    }
    void leave(std::uintptr_t source) {
        if (m_source == source) m_source = 0;
    }
    bool hovered(std::uintptr_t selection_owner, bool inside, bool button_down) {
        if (!button_down || selection_owner != m_source) {
            m_source = 0;
            return false;
        }
        return m_source && inside;
    }
private:
    std::uintptr_t m_source{0};
};
}
