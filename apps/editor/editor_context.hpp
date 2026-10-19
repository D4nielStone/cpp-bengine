#pragma once

#include "core/ecs.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace editor::ui {

struct editor_context {
    using entity_id = std::uint32_t;

    explicit editor_context(std::shared_ptr<commons::ecs> entity_registry)
        : registry(std::move(entity_registry)) {}

    std::shared_ptr<commons::ecs> registry;
    std::optional<entity_id> selected_entity;
    std::optional<entity_id> editor_camera_entity;
};

}
