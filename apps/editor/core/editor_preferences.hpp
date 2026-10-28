#pragma once

#include <cmath>

namespace editor::ui {

struct editor_preferences {
    bool show_grid{true};
    float movement_speed{5.f};
    float sprint_multiplier{3.f};
    float mouse_sensitivity{.15f};
    float zoom_sensitivity{.8f};
    float smoothing_speed{12.f};

    bool valid() const {
        return in_range(movement_speed, .1f, 100.f) &&
            in_range(sprint_multiplier, 1.f, 10.f) &&
            in_range(mouse_sensitivity, .01f, 2.f) &&
            in_range(zoom_sensitivity, .01f, 10.f) &&
            in_range(smoothing_speed, .1f, 100.f);
    }

private:
    static bool in_range(float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    }
};

}
