#pragma once

#include <bgui.hpp>

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "editor_context.hpp"
#include "panel.hpp"
#include "viewport_input_state.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::ui {
namespace detail {

class entities_refreshing_layout final : public bgui::linear {
public:
    entities_refreshing_layout()
        : bgui::linear(bgui::orientation::vertical) {}

    std::function<void()> before_update;
    std::shared_ptr<bool> alive;
    ~entities_refreshing_layout() override { if (alive) *alive = false; }

    void on_update() override {
        if (before_update)
            before_update();
        bgui::linear::on_update();
    }
};

}

class entities_view final : public panel {
public:
    using entity_id = std::uint32_t;
    using name_provider = std::function<std::string(entity_id)>;

private:
    struct entry {
        entity_id id{};
        std::string label;
        std::string normalized_label;

        bool operator==(const entry& other) const noexcept {
            return id == other.id && label == other.label;
        }
    };

    struct row {
        entity_id id{};
        bgui::element* target{nullptr};
        bgui::button* selection{nullptr};
        bgui::button* expand{nullptr};
        bgui::linear* children{nullptr};
        bgui::icon* visibility{nullptr};
        bgui::icon* lock{nullptr};
        bgui::tree* node{nullptr};
    };

    static constexpr const char* selected_class = "editor-entity-row-selected";
    static constexpr const char* selected_class_legacy = "entity-row-selected";

    std::shared_ptr<editor_context> m_context;
    name_provider m_get_name;

    detail::entities_refreshing_layout* m_root{nullptr};
    std::shared_ptr<bool> m_surface_alive = std::make_shared<bool>(false);
    bgui::linear* m_toolbar{nullptr};
    bgui::inputbox* m_filter{nullptr};
    bgui::linear* m_list{nullptr};
    bgui::tree* m_scene_tree{nullptr};
    bgui::button* m_delete_button{nullptr};
    bool m_delete_was_down{false};
    std::unordered_map<entity_id, bool> m_expanded_entities;

    std::vector<row> m_rows;
    std::vector<entry> m_last_entries;
    std::string m_last_filter;
    std::optional<entity_id> m_last_selection;
    std::vector<std::pair<entity_id, entity_id>> m_last_parentage;

    static std::string normalize(std::string value) {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return value;
    }

    static std::string normalized_filter(const std::string& raw_filter) {
        const auto is_space = [](unsigned char character) {
            return std::isspace(character) != 0;
        };
        const auto begin = std::find_if_not(
            raw_filter.begin(), raw_filter.end(), is_space
        );
        const auto end = std::find_if_not(
            raw_filter.rbegin(), raw_filter.rend(), is_space
        ).base();
        return begin < end
            ? normalize(std::string(begin, end))
            : std::string{};
    }

    static bool matches_filter(const entry& item, const std::string& filter) {
        return filter.empty() ||
            item.normalized_label.find(filter) != std::string::npos ||
            std::to_string(item.id).find(filter) != std::string::npos;
    }

    std::string entity_label(const entity_id id) const {
        if (m_context) {
            const auto name = m_context->entity_names.find(id);
            if (name != m_context->entity_names.end() && !name->second.empty())
                return name->second;
        }
        if (m_get_name) {
            auto name = m_get_name(id);
            if (!name.empty())
                return name;
        }
        return "Entity " + std::to_string(id);
    }

    std::string_view entity_icon(const entity_id id) const {
        if (!m_context || !m_context->registry)
            return "circle";

        if (m_context->registry->get<commons::camera>(id))
            return "camera";
        if (m_context->registry->get<commons::directional_light>(id))
            return "lightbulb";
        if (m_context->registry->get<commons::renderer>(id))
            return "cube";

        return "circle";
    }

    // Snapshot ordenado por id (ordem estável independente do container).
    std::vector<entry> collect_entries() const {
        std::vector<entry> result;

        if (!m_context || !m_context->registry)
            return result;

        result.reserve(m_context->registry->entities.size());

        for (const auto& [id, components] : m_context->registry->entities) {
            (void)components;
            if (m_context->editor_camera_entity == id)
                continue;

            entry e;
            e.id = id;
            e.label = entity_label(id);
            e.normalized_label = normalize(e.label);
            result.push_back(std::move(e));
        }

        std::sort(result.begin(), result.end(), [](const entry& a, const entry& b) {
            return a.id < b.id;
        });

        return result;
    }

    static bool contains(const std::vector<entry>& entries, const entity_id id) {
        return std::binary_search(
            entries.begin(),
            entries.end(),
            entry{id, {}, {}},
            [](const entry& a, const entry& b) { return a.id < b.id; }
        );
    }

    struct local_transform_state {
        entity_id parent{};
        bgui::vec3 position;
        bgui::vec3 rotation;
        bgui::vec3 scale;
    };

    static local_transform_state capture_transform(
        const std::shared_ptr<commons::transform>& transform
    ) {
        return {
            transform->parent_id,
            transform->position,
            transform->rotation,
            transform->scale
        };
    }

    static void restore_transform(
        const std::shared_ptr<commons::transform>& transform,
        const local_transform_state& state
    ) {
        transform->parent_id = state.parent;
        transform->position = state.position;
        transform->rotation = state.rotation;
        transform->scale = state.scale;
    }

    entity_id parent_of(const entity_id id) const {
        if (!m_context || !m_context->registry)
            return 0;
        const auto transform = m_context->registry->get<commons::transform>(id);
        return transform ? transform->parent_id : 0;
    }

    std::vector<std::pair<entity_id, entity_id>> parentage_signature(
        const std::vector<entry>& entries
    ) const {
        std::vector<std::pair<entity_id, entity_id>> result;
        result.reserve(entries.size());
        for (const auto& item : entries)
            result.emplace_back(item.id, parent_of(item.id));
        return result;
    }

    void reparent_entity(const entity_id child_id, const entity_id parent_id) {
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

    void drop_entity(const entity_id dragged_id, const bgui::vec2i& position) {
        for (const auto& item : m_rows) {
            if (!item.target)
                continue;
            const auto rect = item.target->processed_rect();
            if (position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w) {
                if (item.id != dragged_id)
                    reparent_entity(dragged_id, item.id);
                return;
            }
        }
        if (m_scene_tree) {
            const auto rect = m_scene_tree->processed_rect();
            if (position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w)
                reparent_entity(dragged_id, 0);
        }
    }

    void create_entity() {
        if (!m_context || !m_context->registry)
            return;
        auto entity = m_context->registry->create();
        m_context->registry->add<commons::transform>(entity);
        m_context->record_entity_creation(entity.id);
        select(entity.id);
    }

    void delete_selected_entity() {
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

        registry->remove(id);
        context->entity_names.erase(id);
        context->hidden_entities.erase(id);
        context->locked_entities.erase(id);
        context->selected_entity.reset();
        context->record_action(
            [registry, context, id, removed_components, removed_mask,
             name, was_hidden, was_locked, children]() {
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
                context->selected_entity = id;
            },
            [registry, context, id, children]() {
                for (const auto& child : children)
                    restore_transform(child.transform, child.after);
                registry->remove(id);
                context->entity_names.erase(id);
                context->hidden_entities.erase(id);
                context->locked_entities.erase(id);
                context->selected_entity.reset();
            }
        );
    }

    static void configure_icon_button(bgui::button& button, const int size) {
        const bgui::color transparent{0.f, 0.f, 0.f, 0.f};
        button.style.layout.require_mode(bgui::mode::pixel, bgui::mode::pixel);
        button.style.layout.require_width(bgui::mode::pixel, size);
        button.style.layout.require_height(bgui::mode::pixel, size);
        button.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };
        button.style.layout.padding = bgui::vec4i{0};
        button.style.visual.background.normal = transparent;
        button.style.visual.background.hover =
            bgui::color{0.18f, 0.18f, 0.18f, 1.f};
        button.style.visual.background.pressed =
            bgui::color{0.24f, 0.24f, 0.24f, 1.f};
        button.style.visual.background.focused = transparent;
        button.style.visual.border.normal = transparent;
        button.style.visual.border.hover = transparent;
        button.style.visual.border.pressed = transparent;
        button.style.visual.border.focused = transparent;
        button.style.visual.border_size = 0.f;
        button.style.visual.border_radius = 5.f;
        button.get_label().set_enable(false);
        button.mark_style_dirty();
    }

    static bgui::icon& add_button_icon(
        bgui::button& button,
        const std::string_view name,
        const float scale
    ) {
        auto& icon = button.add<bgui::icon>(name, scale);
        icon.receives_input(false);
        icon.style.layout.require_mode(
            bgui::mode::wrap_content,
            bgui::mode::wrap_content
        );
        icon.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };
        return icon;
    }

    void update_row_styles() {
        const bgui::color transparent{0.f, 0.f, 0.f, 0.f};
        const bgui::color selected_background{0.216f, 0.216f, 0.216f, 1.f};

        for (const auto& item : m_rows) {
            if (!item.selection)
                continue;

            const bool is_selected =
                m_context && m_context->selected_entity &&
                *m_context->selected_entity == item.id;
            if (is_selected) {
                item.selection->add_class(selected_class);
                item.selection->add_class(selected_class_legacy);
                item.selection->style.visual.background.normal = selected_background;
                item.selection->style.visual.background.hover = selected_background;
                item.selection->style.visual.background.pressed = selected_background;
                item.selection->style.visual.background.focused = selected_background;
            } else {
                item.selection->remove_class(selected_class);
                item.selection->remove_class(selected_class_legacy);
                item.selection->style.visual.background.normal = transparent;
                item.selection->style.visual.background.hover = transparent;
                item.selection->style.visual.background.pressed = transparent;
                item.selection->style.visual.background.focused = transparent;
            }
            item.selection->style.visual.border.normal = transparent;
            item.selection->style.visual.border.hover = transparent;
            item.selection->style.visual.border.pressed = transparent;
            item.selection->style.visual.border.focused = transparent;
            item.selection->style.visual.border_radius = 0.f;
            item.selection->style.visual.border_size = 0.f;
            item.selection->mark_style_dirty();

            const bool hidden =
                m_context && m_context->hidden_entities.contains(item.id);
            if (item.visibility) {
                item.visibility->set_buffer(
                    hidden ? "\xef\x81\xb0" : "\xef\x81\xae"
                );
                item.visibility->style.visual.text.normal = hidden
                    ? bgui::color{0.42f, 0.42f, 0.42f, 1.f}
                    : bgui::color{0.72f, 0.72f, 0.72f, 1.f};
                item.visibility->mark_style_dirty();
            }

            const bool locked =
                m_context && m_context->locked_entities.contains(item.id);
            if (item.lock) {
                item.lock->style.visual.text.normal = locked
                    ? bgui::color{0.92f, 0.72f, 0.32f, 1.f}
                    : bgui::color{0.42f, 0.42f, 0.42f, 1.f};
                item.lock->mark_style_dirty();
            }
        }
    }

    void select(const entity_id id) {
        if (!m_context || m_context->locked_entities.contains(id) ||
            m_context->selected_entity == id)
            return;

        m_context->selected_entity = id;
        m_last_selection = id;
        update_row_styles();
    }

    void toggle_visibility(const entity_id id) {
        if (!m_context || !m_context->registry)
            return;

        const bool now_hidden =
            !m_context->hidden_entities.contains(id);
        if (now_hidden)
            m_context->hidden_entities.insert(id);
        else
            m_context->hidden_entities.erase(id);

        if (const auto renderer = m_context->registry->get<commons::renderer>(id))
            renderer->visible = !now_hidden;

        update_row_styles();
    }

    void toggle_lock(const entity_id id) {
        if (!m_context)
            return;

        if (m_context->locked_entities.contains(id)) {
            m_context->locked_entities.erase(id);
        } else {
            m_context->locked_entities.insert(id);
            if (m_context->selected_entity == id)
                m_context->selected_entity.reset();
        }

        update_row_styles();
    }

    void build_entity_node(
        const entry& item,
        bgui::linear& parent,
        const std::unordered_map<entity_id, std::vector<entity_id>>& children_by_parent,
        const std::unordered_map<entity_id, entry>& entries_by_id,
        std::unordered_set<entity_id>& recursion_path
    ) {
        if (!recursion_path.insert(item.id).second)
            return;
        const auto child_it = children_by_parent.find(item.id);
        const bool has_children = child_it != children_by_parent.end() &&
            !child_it->second.empty();

        auto& node = parent.add<bgui::tree>(item.label, 0.28f, has_children);
        node.set_leaf(!has_children);
        node.set_icon(entity_icon(item.id), 0.24f);
        if (normalized_filter(m_filter->get_buffer()).empty() && m_expanded_entities.contains(item.id))
            node.set_expanded(m_expanded_entities.at(item.id));
        auto& row_layout = node.header();
        const auto id = item.id;
        row_layout.set_function([this, id]() { select(id); });
        node.set_on_drop(
            [this, id](const bgui::vec2i& position) {
                drop_entity(id, position);
            }
        );
        node.label().set_wrap(false);

        auto& visibility_button = row_layout.add<bgui::button>(
            "",
            0.26f,
            [this, id]() { toggle_visibility(id); }
        );
        configure_icon_button(visibility_button, 20);
        auto& visibility_icon = add_button_icon(visibility_button, "eye", 0.24f);

        auto& lock_button = row_layout.add<bgui::button>(
            "",
            0.26f,
            [this, id]() { toggle_lock(id); }
        );
        configure_icon_button(lock_button, 20);
        auto& lock_icon = add_button_icon(lock_button, "key", 0.24f);

        auto& nested = node.children();

        m_rows.push_back({id, &row_layout, &row_layout, &node.disclosure(), &nested,
            &visibility_icon, &lock_icon, &node});
        if (has_children) {
            for (const auto child_id : child_it->second) {
                const auto child = entries_by_id.find(child_id);
                if (child != entries_by_id.end())
                    build_entity_node(child->second, nested, children_by_parent,
                        entries_by_id, recursion_path);
            }
        }
        recursion_path.erase(item.id);
    }

    static void clear_tree(bgui::tree& tree) {
        for (auto& [layer, elements] : tree.children().get_elements()) {
            (void)layer;
            while (!elements.empty())
                tree.children().remove(elements.back().get());
        }
    }

public:
    explicit entities_view(
        std::shared_ptr<editor_context> context,
        name_provider get_name = {}
    )
        : m_context(std::move(context)),
          m_get_name(std::move(get_name)) {}

    entities_view(const entities_view&) = delete;
    entities_view& operator=(const entities_view&) = delete;

    void build(bgui::dock& parent) override {
        if (m_root)
            return;

        auto& window = parent.add_window(
            "Hierarchy",
            bgui::dock_area::left,
            0.3f
        );
        auto& window_context = window.get_context();
        window_context.set_scrollable(false);
        m_root = &window_context.add<detail::entities_refreshing_layout>();
        *m_surface_alive = true;
        m_root->alive = m_surface_alive;
        m_root->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::match_parent
        );
        m_root->set_scrollable(false);
        m_root->before_update = [this]() { update(); };

        const auto start_align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::start
        };

        m_root->style.layout.align = start_align;

        m_toolbar = &m_root->add<bgui::linear>(
            bgui::orientation::horizontal
        );
        m_toolbar->add_class("entities-toolbar");
        m_toolbar->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        m_toolbar->style.layout.padding = bgui::vec4i{4, 3, 4, 3};
        m_toolbar->style.visual.visible = false;

        auto& add_entity = m_toolbar->add<bgui::button>(
            "",
            0.28f,
            [this]() { create_entity(); }
        );
        add_entity.add_class("entities-add-button");
        configure_icon_button(add_entity, 18);
        add_button_icon(add_entity, "plus", 0.26f);

        auto& remove_entity = m_toolbar->add<bgui::button>(
            "",
            0.28f,
            [this]() { delete_selected_entity(); }
        );
        remove_entity.add_class("entities-remove-button");
        configure_icon_button(remove_entity, 18);
        add_button_icon(remove_entity, "trash_alt", 0.24f);
        m_delete_button = &remove_entity;

        auto& search_icon = m_toolbar->add<bgui::icon>("search", 0.24f);
        search_icon.receives_input(false);
        search_icon.style.layout.require_mode(
            bgui::mode::pixel,
            bgui::mode::wrap_content
        );
        search_icon.style.layout.require_width(bgui::mode::pixel, 18.f);
        search_icon.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };

        m_filter = &m_toolbar->add<bgui::inputbox>(
            "",
            "Search entities...",
            0.3f
        );
        m_filter->add_class("editor-input");
        m_filter->style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::wrap_content
        );

        m_list = &m_root->add<bgui::linear>(bgui::orientation::vertical);
        m_list->add_class("entities-list");
        m_list->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::match_parent
        );
        m_list->style.layout.align = start_align;
        m_list->style.layout.padding = bgui::vec4i{0, 2, 0, 0};
        m_list->set_scrollable(true);

        refresh();
    }

    void update() override {
        if (!*m_surface_alive || !m_root || !m_filter || !m_list)
            return;

        const bool delete_down = bgui::get_pressed(bgui::input_key::delete_key);
        if (delete_down && !m_delete_was_down &&
            !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus()) &&
            !viewport_input_blocked_by_modal() && m_context && !m_context->is_playing)
            delete_selected_entity();
        m_delete_was_down = delete_down;
        if (m_delete_button) {
            const bool can_delete = m_context && m_context->selected_entity &&
                m_context->selected_entity != m_context->editor_camera_entity &&
                !m_context->locked_entities.contains(*m_context->selected_entity);
            m_delete_button->receives_input(can_delete);
            m_delete_button->set_style_state(can_delete ? bgui::state::normal : bgui::state::disabled);
        }

        const auto entries = collect_entries();
        const auto current_filter = m_filter->get_buffer();

        if (m_context) {
            for (auto it = m_context->entity_names.begin();
                 it != m_context->entity_names.end();) {
                if (!contains(entries, it->first))
                    it = m_context->entity_names.erase(it);
                else
                    ++it;
            }
            for (auto it = m_context->hidden_entities.begin();
                 it != m_context->hidden_entities.end();) {
                if (!contains(entries, *it))
                    it = m_context->hidden_entities.erase(it);
                else
                    ++it;
            }
            for (auto it = m_context->locked_entities.begin();
                 it != m_context->locked_entities.end();) {
                if (!contains(entries, *it))
                    it = m_context->locked_entities.erase(it);
                else
                    ++it;
            }

            if (m_context->registry) {
                for (const auto& item : entries) {
                    if (!m_context->hidden_entities.contains(item.id))
                        continue;
                    if (const auto renderer =
                            m_context->registry->get<commons::renderer>(item.id)) {
                        renderer->visible = false;
                    }
                }
            }
        }

        // Limpa a seleção se a entidade selecionada não existe mais.
        if (m_context && m_context->selected_entity &&
            (!contains(entries, *m_context->selected_entity) ||
             m_context->locked_entities.contains(*m_context->selected_entity))) {
            m_context->selected_entity = std::nullopt;
        }

        const auto parentage = parentage_signature(entries);
        if (entries != m_last_entries || current_filter != m_last_filter ||
            parentage != m_last_parentage)
            refresh(entries);

        const auto selection = m_context ? m_context->selected_entity : std::nullopt;
        if (selection != m_last_selection)
            update_row_styles();

        m_last_selection = selection;
    }

    void refresh() {
        if (!*m_surface_alive) return;
        refresh(collect_entries());
    }

    std::optional<entity_id> selected() const {
        return m_context ? m_context->selected_entity : std::nullopt;
    }

    std::optional<entity_id> entity_at(const bgui::vec2i& position) const {
        if (!*m_surface_alive || !m_root) return std::nullopt;
        const auto contains = [position](const bgui::element& element) {
            const auto rect = element.processed_rect();
            return position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w;
        };
        if (!contains(*m_root)) return std::nullopt;
        for (const auto& row : m_rows) {
            bool enabled = true;
            for (auto* parent = row.target; parent; parent = parent->get_parent())
                enabled = enabled && parent->is_enabled();
            if (enabled && row.target && contains(*row.target)) return row.id;
        }
        return std::nullopt;
    }

    bgui::linear* root() const noexcept {
        return m_root;
    }

private:
    void refresh(const std::vector<entry>& entries) {
        if (!m_list)
            return;

        for (const auto& row : m_rows)
            if (row.node)
                m_expanded_entities[row.id] = row.node->is_expanded();
        m_rows.clear();
        const auto filter = normalized_filter(
            m_filter ? m_filter->get_buffer() : std::string{}
        );
        std::unordered_map<entity_id, entry> entries_by_id;
        for (const auto& item : entries)
            entries_by_id.emplace(item.id, item);

        std::unordered_set<entity_id> included;
        for (const auto& item : entries) {
            if (!matches_filter(item, filter))
                continue;
            auto current = item.id;
            while (current && included.insert(current).second) {
                const auto parent = parent_of(current);
                if (!parent || !entries_by_id.contains(parent))
                    break;
                current = parent;
            }
        }

        std::unordered_map<entity_id, std::vector<entity_id>> children_by_parent;
        std::vector<entity_id> roots;
        for (const auto& item : entries) {
            if (!included.contains(item.id))
                continue;
            const auto parent = parent_of(item.id);
            if (parent && included.contains(parent))
                children_by_parent[parent].push_back(item.id);
            else
                roots.push_back(item.id);
        }

        const bool create_scene_tree = !m_scene_tree;
        if (!m_scene_tree) {
            m_scene_tree = &m_list->add<bgui::tree>(
                "Scene",
                0.28f,
                false
            );
            m_scene_tree->set_icon("directory_open", 0.24f);
        } else {
            clear_tree(*m_scene_tree);
        }

        m_rows.clear();
        std::unordered_set<entity_id> recursion_path;
        for (const auto id : roots) {
            const auto item = entries_by_id.find(id);
            if (item != entries_by_id.end())
                build_entity_node(item->second, m_scene_tree->children(),
                    children_by_parent, entries_by_id, recursion_path);
        }

        if (included.empty()) {
            auto& empty = m_scene_tree->children().add<bgui::text>(
                filter.empty() ? "No entities" : "No entities found",
                0.26f
            );
            empty.add_class("entities-empty");
        }

        if (create_scene_tree)
            m_scene_tree->set_expanded(true);

        m_last_entries = entries;
        m_last_filter = m_filter ? m_filter->get_buffer() : std::string{};
        m_last_parentage = parentage_signature(entries);

        update_row_styles();
    }
};

}
