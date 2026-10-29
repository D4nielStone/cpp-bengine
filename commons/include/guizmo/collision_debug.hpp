#pragma once
#include "core/ecs.hpp"
#include <utils/vec.hpp>
#include <vector>
namespace editor {
struct collision_line { bgui::vec3 from, to, color; };
// GPU-free geometry from the same Bullet shape and Transform used by simulation.
std::vector<collision_line> collision_lines(const std::shared_ptr<commons::ecs>& registry);
}
