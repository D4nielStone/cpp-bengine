#pragma once

#include <bgui.hpp>
#include <os/os.hpp>

#include <algorithm>
#include <cmath>

namespace editor::ui {

inline void change_ui_scale(int steps) {
    const auto percent = std::lround(bgui::get_global_scale() * 100.f);
    bgui::set_global_scale(std::clamp(percent + steps * 10L, 50L, 200L) / 100.f);
}

inline void reset_ui_scale() {
    bgui::set_global_scale(1.f);
}

class ui_scale_system final : public bgui::element {
public:
    ui_scale_system() {
        receives_input(false);
        set_flex(false);
    }

    void on_update() override {
        using key = bgui::input_key;
        const bool control = bgui::get_pressed(key::left_control) ||
            bgui::get_pressed(key::right_control);
        const bool increase = bgui::get_pressed(key::equal) ||
            bgui::get_pressed(key::keypad_add);
        const bool decrease = bgui::get_pressed(key::minus) ||
            bgui::get_pressed(key::keypad_subtract);
        const bool reset = bgui::get_pressed(key::zero) ||
            bgui::get_pressed(key::keypad_zero);
        if (control) {
            if (reset && !m_reset_down)
                reset_ui_scale();
            else if (increase && !m_increase_down)
                change_ui_scale(1);
            else if (decrease && !m_decrease_down)
                change_ui_scale(-1);
        }
        m_increase_down = increase;
        m_decrease_down = decrease;
        m_reset_down = reset;
    }

private:
    bool m_increase_down{false};
    bool m_decrease_down{false};
    bool m_reset_down{false};
};

}
