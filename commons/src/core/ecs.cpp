#include "core/ecs.hpp"
#include "entities/entity.hpp"
#include "core/ecs.hpp"
#include "components/renderer.hpp"
#include "loaders/shader.hpp"
#include "debugging/debug.hpp"

using namespace COMMONS_NS;

bool ecs::is_alive(const uint32_t id) const noexcept {
    return live_entities.find(id) != live_entities.end();
}

bool ecs::validate_invariants() const noexcept {
    for (const auto& [id, mask] : mascaras) {
        if (live_entities.find(id) == live_entities.end())
            return false;
        if (entities.find(id) == entities.end())
            return false;
        if ((mask & component::COMPONENTE_NONE) != component::COMPONENTE_NONE &&
            entities.at(id).empty())
            return false;
    }
    for (const auto& [id, components] : entities) {
        if (live_entities.find(id) == live_entities.end())
            return false;
        const auto mask_it = mascaras.find(id);
        if (mask_it == mascaras.end() && !components.empty())
            return false;
        if (mask_it != mascaras.end() && mask_it->second != component::COMPONENTE_NONE &&
            components.empty())
            return false;
    }
    return true;
}

entity ecs::create(const uint32_t id)
{
    uint32_t current_id = id;
    if (current_id == 0 || live_entities.find(current_id) != live_entities.end()) {
        uint32_t next_entity = 1;
        while (live_entities.find(next_entity) != live_entities.end())
            ++next_entity;
        current_id = next_entity;
    }

    live_entities.insert(current_id);
    entity ent = { current_id, component::COMPONENTE_NONE };
    add<transform>(ent);
    return ent;
}

component::mask ecs::get_components(const uint32_t& id) const {
    auto it = mascaras.find(id);
    if (it != mascaras.end()) {
        return it->second;
    }
    return component::COMPONENTE_NONE;
}

void ecs::remove(const uint32_t& ent)
{
    if (live_entities.find(ent) == live_entities.end())
        return;

    auto entity_it = entities.find(ent);
    if (entity_it != entities.end()) {
        entity_it->second.clear();
        entities.erase(entity_it);
    }
    mascaras.erase(ent);
    live_entities.erase(ent);
    entity_names.erase(ent);
}

std::optional<uint32_t> ecs::find_by_name(const std::string& name) const {
    if (name.empty()) return std::nullopt;
    std::optional<uint32_t> found;
    for (const auto& [id, candidate] : entity_names) {
        if (candidate != name || !is_alive(id)) continue;
        if (found) return std::nullopt;
        found = id;
    }
    return found;
}
