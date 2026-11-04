#include "editor_api.hpp"

#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/spot_light.hpp"
#include "components/transform.hpp"
#include "primitive_entities.hpp"

#include <algorithm>

namespace editor::ui {
namespace {

using entity_id = editor_api::entity_id;

struct local_transform_state {
    entity_id parent{};
    bgui::vec3 position;
    bgui::vec3 rotation;
    bgui::vec3 scale;
};

local_transform_state capture_transform(
    const std::shared_ptr<commons::transform>& transform
) {
    return {
        transform->parent_id,
        transform->position,
        transform->rotation,
        transform->scale
    };
}

void restore_transform(
    const std::shared_ptr<commons::transform>& transform,
    const local_transform_state& state
) {
    transform->parent_id = state.parent;
    transform->position = state.position;
    transform->rotation = state.rotation;
    transform->scale = state.scale;
}

}

editor_api::entity_id editor_api::parent_of(const entity_id id) const {
    if (!m_context || !m_context->registry)
        return 0;
    const auto transform = m_context->registry->get<commons::transform>(id);
    return transform ? transform->parent_id : 0;
}

void editor_api::reparent_entity(const entity_id child_id, const entity_id parent_id) {
    if (!m_context || !m_context->registry || child_id == parent_id)
        return;
    const auto child = m_context->registry->get<commons::transform>(child_id);
    if (!child)
        return;
    const auto before = capture_transform(child);
    if (!child->set_parent(parent_id, true))
        return;
    const auto after = capture_transform(child);
    if (before.parent == after.parent && before.position == after.position &&
        before.rotation == after.rotation && before.scale == after.scale)
        return;
    m_context->record_action(
        [child, before]() { restore_transform(child, before); },
        [child, after]() { restore_transform(child, after); }
    );
}

void editor_api::group_selected_entities() {
    if (!m_context || !m_context->registry) return;
    m_context->synchronize_selection();
    auto ids = m_context->selected_entities;
    if (m_context->selected_entity) ids.insert(*m_context->selected_entity);
    std::vector<entity_id> roots;
    for (auto id : ids) {
        if (!m_context->registry->is_alive(id) || m_context->locked_entities.contains(id) ||
            m_context->editor_camera_entity == id) continue;
        bool nested = false;
        std::unordered_set<entity_id> visited;
        for (auto parent = parent_of(id); parent && visited.insert(parent).second; parent = parent_of(parent))
            if (ids.contains(parent)) { nested = true; break; }
        if (!nested) roots.push_back(id);
    }
    if (roots.empty()) return;
    const auto history_start = m_context->undo_stack.size();
    auto group = m_context->registry->create();
    m_context->entity_names[group.id] = "Grupo";
    m_context->record_entity_creation(group.id);
    for (auto id : roots) reparent_entity(id, group.id);
    std::vector<editor_context::history_action> actions;
    while (m_context->undo_stack.size() > history_start) {
        actions.push_back(std::move(m_context->undo_stack.back()));
        m_context->undo_stack.pop_back();
    }
    m_context->record_action(
        [actions] { for (const auto& action : actions) action.undo(); },
        [actions] { for (auto it = actions.rbegin(); it != actions.rend(); ++it) it->redo(); });
    m_context->select_entity(group.id);
}

void editor_api::delete_selected_entity() {
    if (!m_context || !m_context->registry || !m_context->selected_entity)
        return;
    const auto id = *m_context->selected_entity;
    if (m_context->editor_camera_entity == id ||
        m_context->locked_entities.contains(id))
        return;
    const auto found = m_context->registry->entities.find(id);
    const auto mask = m_context->registry->mascaras.find(id);
    if (found == m_context->registry->entities.end() ||
        mask == m_context->registry->mascaras.end())
        return;

    auto registry = m_context->registry;
    auto context = m_context;
    const auto removed_components = found->second;
    const auto removed_mask = mask->second;
    const auto transform = registry->get<commons::transform>(id);
    const auto removed_name = context->entity_names.find(id);
    const std::optional<std::string> name = removed_name == context->entity_names.end()
        ? std::nullopt
        : std::optional<std::string>{removed_name->second};
    const bool was_hidden = context->hidden_entities.contains(id);
    const bool was_locked = context->locked_entities.contains(id);

    struct child_state {
        std::shared_ptr<commons::transform> transform;
        local_transform_state before;
        local_transform_state after;
    };
    std::vector<child_state> children;
    const auto new_parent = transform ? transform->parent_id : 0;
    for (const auto& [child_id, components] : registry->entities) {
        (void)components;
        const auto child = registry->get<commons::transform>(child_id);
        if (!child || child->parent_id != id)
            continue;
        const auto before = capture_transform(child);
        if (!child->set_parent(new_parent, true)) {
            for (const auto& state : children)
                restore_transform(state.transform, state.before);
            return;
        }
        children.push_back({child, before, capture_transform(child)});
    }

    std::vector<std::pair<std::shared_ptr<commons::code>, std::string>> references;
    registry->cada<commons::code>([&](entity_id owner) {
        if (owner == id) return;
        const auto script = registry->get<commons::code>(owner);
        const auto collect = [&](const std::shared_ptr<commons::code>& entry) {
            for (const auto& property : entry->properties) {
                const auto* target = std::get_if<std::uint32_t>(&property.value);
                if (target && *target == id) references.emplace_back(entry, property.name);
            }
        };
        collect(script);
        for (const auto& entry : script->scripts) collect(entry);
    });
    for (const auto& [script, property] : references)
        script->set_property(property, std::uint32_t{0});

    registry->remove(id);
    context->entity_names.erase(id);
    context->hidden_entities.erase(id);
    context->locked_entities.erase(id);
    context->select_entity(std::nullopt);
    const std::weak_ptr<editor_context> weak_context = context;
    context->record_action(
        [registry, weak_context, id, removed_components, removed_mask,
         name, was_hidden, was_locked, children, references]() {
            const auto context = weak_context.lock();
            if (!context)
                return;
            registry->entities[id] = removed_components;
            registry->mascaras[id] = removed_mask;
            registry->live_entities.insert(id);
            if (name)
                context->entity_names[id] = *name;
            if (was_hidden)
                context->hidden_entities.insert(id);
            if (was_locked)
                context->locked_entities.insert(id);
            for (const auto& child : children)
                restore_transform(child.transform, child.before);
            for (const auto& [script, property] : references) script->set_property(property, id);
            context->selected_entity = id;
        },
        [registry, weak_context, id, children, references]() {
            const auto context = weak_context.lock();
            if (!context)
                return;
            for (const auto& child : children)
                restore_transform(child.transform, child.after);
            for (const auto& [script, property] : references)
                script->set_property(property, std::uint32_t{0});
            registry->remove(id);
            context->entity_names.erase(id);
            context->hidden_entities.erase(id);
            context->locked_entities.erase(id);
            context->select_entity(std::nullopt);
        }
    );
}

void editor_api::toggle_visibility(const entity_id id) {
    if (!m_context || !m_context->registry)
        return;

    if (!m_context->registry->is_alive(id)) return;

    const bool now_hidden =
        !m_context->hidden_entities.contains(id);
    if (now_hidden)
        m_context->hidden_entities.insert(id);
    else
        m_context->hidden_entities.erase(id);

    if (const auto renderer = m_context->registry->get<commons::renderer>(id))
        renderer->visible = !now_hidden;
    m_context->events.publish(editor_event::scene);
}

void editor_api::toggle_lock(const entity_id id) {
    if (!m_context || !m_context->registry || !m_context->registry->is_alive(id))
        return;

    if (m_context->locked_entities.contains(id)) {
        m_context->locked_entities.erase(id);
    } else {
        m_context->locked_entities.insert(id);
        if (m_context->selected_entity == id)
            m_context->selected_entity.reset();
        m_context->synchronize_selection();
        m_context->events.publish(editor_event::selection);
    }
    m_context->events.publish(editor_event::scene);
}

std::vector<editor_api::scene_entity> editor_api::scene_entities(const name_provider& fallback) const {
    std::vector<scene_entity> result;
    if (!m_context || !m_context->registry) return result;
    result.reserve(m_context->registry->entities.size());
    for (const auto& [id, components] : m_context->registry->entities) {
        if (id != m_context->editor_camera_entity)
            result.push_back({id, entity_name(id, fallback), parent_of(id), entity_kind(id)});
    }
    std::ranges::sort(result, {}, &scene_entity::id);
    return result;
}

std::string editor_api::entity_name(entity_id id, const name_provider& fallback) const {
    if (m_context) {
        const auto found = m_context->entity_names.find(id);
        if (found != m_context->entity_names.end() && !found->second.empty())
            return found->second;
    }
    if (fallback) {
        auto name = fallback(id);
        if (!name.empty()) return name;
    }
    return "Entity " + std::to_string(id);
}

editor_entity_kind editor_api::entity_kind(entity_id id) const {
    if (!m_context || !m_context->registry) return editor_entity_kind::generic;
    auto& registry = *m_context->registry;
    if (registry.get<commons::camera>(id)) return editor_entity_kind::camera;
    if (registry.get<commons::directional_light>(id) || registry.get<commons::spot_light>(id))
        return editor_entity_kind::light;
    if (registry.get<commons::renderer>(id)) return editor_entity_kind::mesh;
    return editor_entity_kind::generic;
}

std::optional<editor_api::entity_id> editor_api::create_entity() {
    if (!m_context || !m_context->registry) return std::nullopt;
    const auto entity = m_context->registry->create();
    m_context->record_entity_creation(entity.id);
    return entity.id;
}

std::optional<editor_api::entity_id> editor_api::create_primitive(std::size_t index) {
    return create_primitive_entity(m_context, index);
}

void editor_api::synchronize_scene() {
    if (!m_context || !m_context->registry) return;
    auto& registry = *m_context->registry;
    const auto missing = [&](entity_id id) {
        return m_context->editor_camera_entity == id || !registry.entities.contains(id);
    };
    std::erase_if(m_context->entity_names, [&](const auto& entry) { return missing(entry.first); });
    std::erase_if(m_context->hidden_entities, missing);
    std::erase_if(m_context->locked_entities, missing);
    for (const auto id : m_context->hidden_entities) {
        if (const auto renderer = registry.get<commons::renderer>(id))
            renderer->visible = false;
    }
    if (m_context->selected_entity && missing(*m_context->selected_entity))
        m_context->selected_entity.reset();
    m_context->synchronize_selection();
}

std::optional<editor_api::entity_id> editor_api::selected_entity() const {
    return m_context ? m_context->selected_entity : std::nullopt;
}

bool editor_api::is_selected(entity_id id) const {
    return m_context && (m_context->selected_entity == id || m_context->selected_entities.contains(id));
}

bool editor_api::is_hidden(entity_id id) const {
    return m_context && m_context->hidden_entities.contains(id);
}

bool editor_api::is_locked(entity_id id) const {
    return m_context && m_context->locked_entities.contains(id);
}

bool editor_api::can_delete_selection() const {
    const auto id = selected_entity();
    return id && id != m_context->editor_camera_entity && !is_locked(*id);
}

bool editor_api::is_playing() const {
    return m_context && m_context->is_playing;
}

void editor_api::select_entity(std::optional<entity_id> id, bool additive) {
    if (m_context) m_context->select_entity(id, additive);
}

void editor_api::select_asset(editor_asset_kind kind, const std::filesystem::path& file) {
    if (!m_context) return;
    if (kind != editor_asset_kind::none) {
        m_context->select_entity(std::nullopt);
        m_context->selected_hud_widget.reset();
    }
    m_context->selected_image = kind == editor_asset_kind::image ? file : std::filesystem::path{};
    m_context->selected_material = kind == editor_asset_kind::material ? file : std::filesystem::path{};
    m_context->events.publish(editor_event::assets);
}

std::filesystem::path editor_api::selected_asset(editor_asset_kind kind) const {
    if (!m_context) return {};
    if (kind == editor_asset_kind::image) return m_context->selected_image;
    if (kind == editor_asset_kind::material) return m_context->selected_material;
    return {};
}

bool editor_api::set_script_property(entity_id owner, const std::shared_ptr<commons::code>& script,
    const commons::script_property& property) {
    if (!m_context || !m_context->registry || !script || is_playing() || is_locked(owner))
        return false;

    const auto root = m_context->registry->get<commons::code>(owner);
    if (!root || (root != script && std::ranges::find(root->scripts, script) == root->scripts.end()))
        return false;

    const auto it = std::ranges::find(script->properties, property.name, &commons::script_property::name);
    if (it == script->properties.end() || it->value == property.value)
        return false;

    const auto before = it->value;
    if (!script->set_property(property.name, property.value))
        return false;
    record_property_edit(
        [script, name = property.name, before] { script->set_property(name, before); },
        [script, name = property.name, next = property.value] { script->set_property(name, next); });
    return true;
}

void editor_api::record_property_edit(std::function<void()> undo, std::function<void()> redo) {
    if (m_context)
        m_context->record_action(std::move(undo), std::move(redo), editor_event::properties);
}

}
