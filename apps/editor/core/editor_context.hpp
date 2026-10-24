#pragma once

#include "core/ecs.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::ui {

struct editor_context {
    using entity_id = std::uint32_t;

    struct history_action {
        std::function<void()> undo;
        std::function<void()> redo;
    };

    explicit editor_context(std::shared_ptr<commons::ecs> entity_registry)
        : registry(std::move(entity_registry)) {}

    std::shared_ptr<commons::ecs> registry;
    std::vector<history_action> undo_stack;
    std::vector<history_action> redo_stack;
    std::optional<entity_id> selected_entity;
    std::optional<entity_id> editor_camera_entity;
    bool is_playing{false};
    std::unordered_map<entity_id, std::string> entity_names;
    std::unordered_set<entity_id> hidden_entities;
    std::unordered_set<entity_id> locked_entities;

    void record_action(
        std::function<void()> undo,
        std::function<void()> redo
    ) {
        undo_stack.push_back({std::move(undo), std::move(redo)});
        redo_stack.clear();
    }

    bool undo() {
        if (undo_stack.empty())
            return false;
        auto action = std::move(undo_stack.back());
        undo_stack.pop_back();
        action.undo();
        redo_stack.push_back(std::move(action));
        return true;
    }

    bool redo() {
        if (redo_stack.empty())
            return false;
        auto action = std::move(redo_stack.back());
        redo_stack.pop_back();
        action.redo();
        undo_stack.push_back(std::move(action));
        return true;
    }

    void clear_history() {
        undo_stack.clear();
        redo_stack.clear();
    }

    void record_entity_creation(const entity_id id) {
        if (!registry)
            return;
        const auto components = registry->entities.find(id);
        const auto mask = registry->mascaras.find(id);
        if (components == registry->entities.end() ||
            mask == registry->mascaras.end())
            return;

        auto saved_components = components->second;
        const auto saved_mask = mask->second;
        auto* entity_name_map = &entity_names;
        const auto name = entity_names.find(id);
        const std::optional<std::string> saved_name = name == entity_names.end()
            ? std::nullopt
            : std::optional<std::string>{name->second};
        auto entity_registry = registry;
        record_action(
            [entity_registry, entity_name_map, id]() {
                entity_registry->remove(id);
                entity_name_map->erase(id);
            },
            [entity_registry, entity_name_map, id, saved_components,
             saved_mask, saved_name]() {
                entity_registry->entities[id] = saved_components;
                entity_registry->mascaras[id] = saved_mask;
                entity_registry->live_entities.insert(id);
                if (saved_name)
                    (*entity_name_map)[id] = *saved_name;
            }
        );
    }
};

}
