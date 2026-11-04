#pragma once

#include "core/ecs.hpp"
#include "editor_preferences.hpp"
#include "editor_events.hpp"

#include <cstdint>
#include <functional>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace commons { struct hud_element; }

namespace editor::ui {

struct editor_context {
    using entity_id = std::uint32_t;

    struct history_action {
        std::function<void()> undo;
        std::function<void()> redo;
        editor_event event{editor_event::scene};
    };

    explicit editor_context(std::shared_ptr<commons::ecs> entity_registry)
        : registry(std::move(entity_registry)) {}

    editor_events events;
    std::shared_ptr<commons::ecs> registry;
    std::filesystem::path project_root;
    std::vector<history_action> undo_stack;
    std::vector<history_action> redo_stack;
    std::optional<entity_id> selected_entity;
    std::filesystem::path selected_image;
    std::filesystem::path selected_material;
    std::weak_ptr<commons::hud_element> selected_hud_widget;
    std::unordered_set<entity_id> selected_entities;

    void synchronize_selection() {
        if (!selected_entity || !registry || !registry->is_alive(*selected_entity) ||
            locked_entities.contains(*selected_entity)) {
            selected_entity.reset();
            selected_entities.clear();
            return;
        }
        if (!selected_entities.contains(*selected_entity)) selected_entities.clear();
        std::erase_if(selected_entities, [this](auto id) {
            return !registry->is_alive(id) || locked_entities.contains(id);
        });
        selected_entities.insert(*selected_entity);
    }

    void select_entity(std::optional<entity_id> id, bool additive = false) {
        events.publish(editor_event::selection);
        if (!selected_image.empty() || !selected_material.empty())
            events.publish(editor_event::assets);
        selected_image.clear();
        selected_material.clear();
        synchronize_selection();
        if (!additive) selected_entities.clear();
        else if (selected_entity) selected_entities.insert(*selected_entity);
        if (id && registry && registry->is_alive(*id) && !locked_entities.contains(*id)) {
            if (additive && selected_entities.erase(*id)) {
                selected_entity = selected_entities.empty() ? std::nullopt
                    : std::optional<entity_id>{*selected_entities.begin()};
                return;
            }
            selected_entities.insert(*id);
            selected_entity = id;
        } else if (!additive) selected_entity.reset();
    }

    std::optional<entity_id> editor_camera_entity;
    bool is_playing{false};
    bool show_colliders{false};
    bool orbital_view{false};
    bool model_orbital_view{false};
    int workspace_mode{0};
    int model_selection{0};
    int model_view_mode{0};
    std::string extrude_distance{"0.25"};
    std::unordered_map<std::string, bool> inspector_sections;
    editor_preferences preferences;
    std::size_t history_revision{0};
    std::unordered_map<entity_id, std::string> entity_names;
    std::unordered_set<entity_id> hidden_entities;
    std::unordered_set<entity_id> locked_entities;

    void record_action(
        std::function<void()> undo,
        std::function<void()> redo,
        editor_event event = editor_event::scene
    ) {
        undo_stack.push_back({std::move(undo), std::move(redo), event});
        events.publish(event);
        events.publish(editor_event::history);
        redo_stack.clear();
    }

    bool undo() {
        if (undo_stack.empty())
            return false;
        auto action = std::move(undo_stack.back());
        undo_stack.pop_back();
        action.undo();
        events.publish(action.event);
        events.publish(editor_event::history);
        ++history_revision;
        redo_stack.push_back(std::move(action));
        return true;
    }

    bool redo() {
        if (redo_stack.empty())
            return false;
        auto action = std::move(redo_stack.back());
        redo_stack.pop_back();
        action.redo();
        events.publish(action.event);
        events.publish(editor_event::history);
        ++history_revision;
        undo_stack.push_back(std::move(action));
        return true;
    }

    void clear_history() {
        undo_stack.clear();
        redo_stack.clear();
        ++history_revision;
        events.publish(editor_event::history);
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
