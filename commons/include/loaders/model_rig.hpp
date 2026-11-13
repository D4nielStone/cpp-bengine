#pragma once

#include <utils/vec.hpp>
#include <string>
#include <vector>

namespace commons {

struct rig_transform {
    bgui::vec3 position{}, rotation{}, scale{1.f, 1.f, 1.f};
    bool valid() const;
    bool operator==(const rig_transform&) const = default;
};

struct rig_joint {
    std::string id, name, parent;
    rig_transform rest;
};

struct rig_weight {
    std::string joint;
    float weight{};
};

struct model_rig {
    std::vector<rig_joint> joints;
    bool validate(std::string& error) const;
};

}
