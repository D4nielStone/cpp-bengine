#pragma once

#include "editor_context.hpp"

namespace commons { struct code; struct script_property; }

namespace editor::ui {

enum class editor_asset_kind { none, image, material };
enum class editor_entity_kind { generic, camera, light, mesh };

class editor_api {
public:
    using entity_id = editor_context::entity_id;
    using name_provider = std::function<std::string(entity_id)>;

    explicit editor_api(std::shared_ptr<editor_context> context)
        : m_context(std::move(context)) {}

    struct scene_entity {
        entity_id id{};
        std::string label;
        entity_id parent{};
        editor_entity_kind kind{};

        bool operator==(const scene_entity&) const = default;
    };

    std::vector<scene_entity> scene_entities(const name_provider& fallback = {}) const;
    std::string entity_name(entity_id id, const name_provider& fallback = {}) const;
    entity_id parent_of(entity_id id) const;
    editor_entity_kind entity_kind(entity_id id) const;
    std::optional<entity_id> create_entity();
    std::optional<entity_id> create_primitive(std::size_t index);
    void reparent_entity(entity_id child_id, entity_id parent_id);
    void group_selected_entities();
    void delete_selected_entity();
    void synchronize_scene();

    std::optional<entity_id> selected_entity() const;
    bool is_selected(entity_id id) const;
    bool is_hidden(entity_id id) const;
    bool is_locked(entity_id id) const;
    bool can_delete_selection() const;
    bool is_playing() const;
    void select_entity(std::optional<entity_id> id, bool additive = false);
    void toggle_visibility(entity_id id);
    void toggle_lock(entity_id id);
    void select_asset(editor_asset_kind kind, const std::filesystem::path& file);
    std::filesystem::path selected_asset(editor_asset_kind kind) const;

    bool set_script_property(entity_id owner, const std::shared_ptr<commons::code>& script,
        const commons::script_property& property);
    void record_property_edit(std::function<void()> undo, std::function<void()> redo);
    editor_events events() const noexcept { return m_context ? m_context->events : editor_events{}; }

private:
    std::shared_ptr<editor_context> m_context;
};

}
