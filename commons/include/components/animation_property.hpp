#pragma once
#include <limits>
#include <string>

namespace commons {
struct animation_property {
    std::string path;
    double minimum{-std::numeric_limits<float>::max()};
    double maximum{std::numeric_limits<float>::max()};
    bool stepped{false};
};
}
