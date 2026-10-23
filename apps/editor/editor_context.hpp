#pragma once

#include "core/ecs.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace editor::ui {

struct editor_context {
    using entity_id = std::uint32_t;

    explicit editor_context(std::shared_ptr<commons::ecs> entity_registry)
        : registry(std::move(entity_registry)) {}

    std::shared_ptr<commons::ecs> registry;
    std::optional<entity_id> selected_entity;
    std::optional<entity_id> editor_camera_entity;
    bool is_playing{false};
    bool show_camera_frustum{true};
    std::unordered_map<entity_id, std::string> entity_names;
    std::unordered_set<entity_id> hidden_entities;
    std::unordered_set<entity_id> locked_entities;
};

}
