#pragma once
#include "editor_context.hpp"
#include "systems/rig_entities.hpp"

namespace editor::ui {
inline bool expose_rig_entities(const std::shared_ptr<editor_context>& context, uint32_t owner) {
    if (!context || context->is_playing || context->locked_entities.contains(owner)) return false;
    const auto registry = context->registry;
    const auto created = commons::create_rig_entities(*registry, owner);
    if (created.empty()) return false;
    struct entry {
        uint32_t id;
        decltype(registry->entities)::mapped_type components;
        commons::component::mask mask;
        std::string name;
    };
    std::vector<entry> entries;
    for (auto id : created) {
        context->entity_names[id] = registry->entity_names[id];
        entries.push_back({id, registry->entities.at(id), registry->mascaras.at(id), registry->entity_names[id]});
    }
    auto* names = &context->entity_names;
    context->record_action([registry, names, created] {
        for (auto id : created) { registry->remove(id); names->erase(id); }
    }, [registry, names, entries] {
        for (const auto& entry : entries) {
            registry->live_entities.insert(entry.id);
            registry->entities[entry.id] = entry.components;
            registry->mascaras[entry.id] = entry.mask;
            registry->entity_names[entry.id] = (*names)[entry.id] = entry.name;
        }
    });
    context->events.publish(editor_event::scene);
    return true;
}
}
