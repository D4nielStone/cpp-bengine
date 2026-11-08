#pragma once
#include <functional>
#include <string>

namespace commons {
// Host-owned operations; load requests must be executed between frames.
struct scene_control {
    std::string active, last_error;
    bool loading{false};
    std::function<bool(const std::string&)> load;
};
}
