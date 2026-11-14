#pragma once
#include "../core/localization.hpp"

#include <bgui.hpp>

#include "../presenters/entities_presenter.hpp"
#include "panel.hpp"
#include "viewport_input_state.hpp"
#include "context_action_button.hpp"
#include "primitive_catalog.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
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
    using name_provider = entities_presenter::name_provider;

private:
    using entry = entities_presenter::entry;

    struct row_state {
        bool selected;
        bool hidden;
        bool locked;

        bool operator==(const row_state&) const = default;
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
        std::optional<row_state> applied_state{};
        bgui::button* eye_button{};
        bgui::button* lock_button{};
        std::optional<editor_entity_kind> kind;
        std::size_t binding_version{};
    };

    static constexpr const char* selected_class = "editor-entity-row-selected";
    static constexpr const char* selected_class_legacy = "entity-row-selected";

    entities_presenter m_presenter;

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
    static constexpr int row_height = 26;
    static constexpr std::size_t virtualization_threshold = 128;
    struct visible_entry { std::size_t index, depth; bool branch, expanded; };
    std::vector<visible_entry> m_visible;
    bgui::linear* m_pool{};
    bgui::element* m_before{};
    bgui::element* m_after{};
    bgui::text* m_empty{};
    std::size_t m_binding_version{}, m_applied_version{}, m_first{}, m_end{};
    float m_pool_scale{};
    std::uint64_t m_scene_revision{}, m_properties_revision{};
    std::chrono::steady_clock::time_point m_snapshot_due{};
    bool m_structure_dirty{}, m_filtered{};
    std::unordered_set<entity_id> m_folded_matches;

    void drop_entity(const entity_id dragged_id, const bgui::vec2i& position) {
        if (!m_list || !m_scene_tree) return;
        const auto clip = m_list->get_children_clip_rect();
        if (position.x < clip.x || position.x >= clip.x + clip.z ||
            position.y < clip.y || position.y >= clip.y + clip.w) return;
        for (const auto& item : m_rows) {
            if (!item.target || !item.node->is_enabled() || !m_scene_tree->is_expanded())
                continue;
            const auto rect = item.target->processed_rect();
            if (position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w) {
                if (item.id != dragged_id)
                    m_presenter.api().reparent_entity(dragged_id, item.id);
                return;
            }
        }
        if (m_scene_tree) {
            const auto rect = m_scene_tree->processed_rect();
            if (position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w)
                m_presenter.api().reparent_entity(dragged_id, 0);
        }
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

        for (auto& item : m_rows) {
            if (!item.selection || !item.node->is_enabled())
                continue;

            const row_state state{
                m_presenter.api().is_selected(item.id),
                m_presenter.api().is_hidden(item.id),
                m_presenter.api().is_locked(item.id)
            };
            if (item.applied_state == state)
                continue;
            item.applied_state = state;

            if (state.selected) {
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

            if (item.visibility) {
                item.visibility->set_buffer(
                    state.hidden ? "\xef\x81\xb0" : "\xef\x81\xae"
                );
                item.visibility->style.visual.text.normal = state.hidden
                    ? bgui::color{0.42f, 0.42f, 0.42f, 1.f}
                    : bgui::color{0.72f, 0.72f, 0.72f, 1.f};
                item.visibility->mark_style_dirty();
            }

            if (item.lock) {
                item.lock->style.visual.text.normal = state.locked
                    ? bgui::color{0.92f, 0.72f, 0.32f, 1.f}
                    : bgui::color{0.42f, 0.42f, 0.42f, 1.f};
                item.lock->mark_style_dirty();
            }
        }
    }

    void select(const entity_id id) {
        if (m_presenter.api().is_locked(id))
            return;

        m_presenter.api().select_hierarchy(id, bgui::get_pressed(bgui::input_key::left_control) ||
            bgui::get_pressed(bgui::input_key::right_control));
        update_row_styles();
    }

    row& create_row(bgui::linear& parent) {
        auto& node = parent.add<bgui::tree>("", .30f, false);
        auto& header = node.header();
        node.label().set_wrap(false);
        auto& eye = header.add<bgui::button>("", .30f, nullptr);
        auto& lock = header.add<bgui::button>("", .30f, nullptr);
        configure_icon_button(eye, 20);
        configure_icon_button(lock, 20);
        auto& eye_icon = add_button_icon(eye, "eye", .24f);
        auto& lock_icon = add_button_icon(lock, "key", .24f);
        auto& result = m_rows.emplace_back();
        result.node = &node;
        result.target = result.selection = &header;
        result.expand = &node.disclosure();
        result.children = &node.children();
        result.visibility = &eye_icon;
        result.lock = &lock_icon;
        result.eye_button = &eye;
        result.lock_button = &lock;
        return result;
    }

    void bind_row(row& slot, const entry& item, bool branch, bool expanded) {
        const auto id = item.id;
        slot.id = id;
        slot.node->set_leaf(!branch);
        slot.node->set_expanded(expanded);
        slot.node->label().set_buffer(item.label);
        if (slot.kind != item.kind) {
            slot.kind = item.kind;
            const char* icon = "circle";
            if (item.kind == editor_entity_kind::camera) icon = "camera";
            if (item.kind == editor_entity_kind::light) icon = "lightbulb";
            if (item.kind == editor_entity_kind::mesh) icon = "cube";
            slot.node->set_icon(icon, .24f);
        }
        slot.selection->set_function([this, id] { select(id); });
        slot.node->set_on_drop([this, id](const bgui::vec2i& pos) { drop_entity(id, pos); });
        slot.eye_button->set_function([this, id] {
            m_presenter.api().toggle_visibility(id); update_row_styles();
        });
        slot.lock_button->set_function([this, id] {
            m_presenter.api().toggle_lock(id); update_row_styles();
        });
        slot.applied_state.reset();
    }

    void build_entity_node(
        const entry& item,
        bgui::linear& parent,
        const std::unordered_map<entity_id, std::vector<entity_id>>& children_by_parent,
        const std::unordered_map<entity_id, const entry*>& entries_by_id,
        std::unordered_set<entity_id>& recursion_path
    ) {
        if (!recursion_path.insert(item.id).second)
            return;
        const auto child_it = children_by_parent.find(item.id);
        const bool has_children = child_it != children_by_parent.end() &&
            !child_it->second.empty();

        auto& slot = create_row(parent);
        const bool expanded = has_children && (m_filtered || !m_expanded_entities.contains(item.id) ||
            m_expanded_entities.at(item.id));
        bind_row(slot, item, has_children, expanded);
        auto& nested = *slot.children;
        if (has_children) {
            for (const auto child_id : child_it->second) {
                const auto child = entries_by_id.find(child_id);
                if (child != entries_by_id.end())
                    build_entity_node(*child->second, nested, children_by_parent,
                        entries_by_id, recursion_path);
            }
        }
        recursion_path.erase(item.id);
    }

    void flatten() {
        const auto hierarchy = entities_presenter::build_hierarchy(m_last_entries,
            entities_presenter::normalized_filter(m_last_filter));
        m_visible.clear();
        std::vector<std::pair<entity_id, std::size_t>> pending;
        for (auto it = hierarchy.roots.rbegin(); it != hierarchy.roots.rend(); ++it)
            pending.emplace_back(*it, 0);
        while (!pending.empty()) {
            const auto [id, depth] = pending.back();
            pending.pop_back();
            const auto children = hierarchy.children_by_parent.find(id);
            const bool branch = children != hierarchy.children_by_parent.end();
            const bool expanded = branch && (m_filtered ? !m_folded_matches.contains(id) :
                !m_expanded_entities.contains(id) || m_expanded_entities.at(id));
            const auto index = static_cast<std::size_t>(hierarchy.entries_by_id.at(id) - m_last_entries.data());
            m_visible.push_back({index, depth, branch, expanded});
            if (expanded)
                for (auto it = children->second.rbegin(); it != children->second.rend(); ++it)
                    pending.emplace_back(*it, depth + 1);
        }
        ++m_binding_version;
        m_structure_dirty = false;
    }

    void update_pool() {
        if (!m_pool || !m_scene_tree->is_expanded()) return;
        if (!m_structure_dirty && bgui::get_pressed(bgui::input_key::mouse_left)) {
            for (auto* target = bgui::get_mouse_target(); target; target = target->get_parent())
                if (target == m_pool) return;
        }
        if (m_structure_dirty) flatten();
        const auto scale = bgui::get_global_scale();
        const int pixels = std::max(1, static_cast<int>(row_height * scale));
        const int header = m_scene_tree->header().processed_height();
        const auto count = m_visible.size();
        const auto capacity = static_cast<std::size_t>(std::max(0, m_list->processed_height()) / pixels + 4);
        const auto scroll = std::max(0, m_list->get_scroll_offset() - header);
        const auto first = std::min(count > capacity ? count - capacity : 0,
            static_cast<std::size_t>(std::max(0, scroll / pixels - 1)));
        const auto end = std::min(count, first + capacity);
        if (m_first != first || m_end != end || m_applied_version != m_binding_version || m_pool_scale != scale) {
            auto spacer = [pixels, scale](bgui::element& element, std::size_t rows) {
                element.set_enable(rows != 0);
                element.style.layout.require_height(bgui::mode::pixel, static_cast<float>(rows) * pixels / scale);
                element.mark_style_dirty();
            };
            spacer(*m_before, first);
            spacer(*m_after, count - end);
            m_pool_scale = scale;
            m_first = first;
            m_end = end;
            m_applied_version = m_binding_version;
        }
        while (m_rows.size() < end - first) create_row(*m_pool);
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            auto& slot = m_rows[i];
            const bool used = i < end - first;
            slot.node->set_enable(used);
            if (!used) continue;
            const auto& info = m_visible[first + i];
            const auto& item = m_last_entries[info.index];
            if (slot.id == item.id && slot.binding_version == m_binding_version) continue;
            bind_row(slot, item, info.branch, info.expanded);
            slot.binding_version = m_binding_version;
            slot.node->style.layout.require_height(bgui::mode::pixel, row_height);
            slot.node->style.layout.margin = bgui::vec4i{0};
            slot.node->style.layout.padding = bgui::vec4i{
                static_cast<int>(std::min<std::size_t>(info.depth, 20)) * 14, 0, 0, 0};
            slot.node->mark_style_dirty();
            slot.expand->set_function([this, id = item.id, expanded = info.expanded] {
                if (m_filtered) {
                    if (expanded) m_folded_matches.insert(id);
                    else m_folded_matches.erase(id);
                } else m_expanded_entities[id] = !expanded;
                m_structure_dirty = true;
            });
        }
        if (m_empty) m_empty->set_enable(count == 0);
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
        : m_presenter(std::move(context), std::move(get_name)) {}

    entities_view(const entities_view&) = delete;
    entities_view& operator=(const entities_view&) = delete;

    void build(bgui::dock& parent) override {
        if (m_root)
            return;

        auto& window = parent.add_window(
            tr("Hierarchy"),
            bgui::dock_area::left,
            0.3f
        );
        auto& window_context = window.get_context();
        window_context.set_scrollable(false);
        m_root = &bgui::panel_content(window).add<detail::entities_refreshing_layout>();
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

        auto& add_entity = m_toolbar->add<context_action_button>(
            "",
            0.28f,
            []() {}
        );
        add_entity.add_class("entities-add-button");
        configure_icon_button(add_entity, 18);
        add_button_icon(add_entity, "plus", 0.26f);
        add_entity.context_options = [this]() {
            context_actions actions;
            actions.emplace_back("Entidade", [this]() { if (auto id = m_presenter.api().create_entity()) select(*id); });
            for (size_t i = 0; i < primitive_entities.size(); ++i) {
                actions.emplace_back(primitive_entities[i].name, [this, i]() {
                    try { if (const auto id = m_presenter.api().create_primitive(i)) { select(*id); refresh(); } }
                    catch (const std::exception& e) {
                        auto& modal = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
                        modal.add<bgui::text>(e.what(), .30f);
                    }
                });
            }
            return actions;
        };
        add_entity.set_function([button = &add_entity]() { button->open_context(); });

        auto& group_entities = m_toolbar->add<bgui::button>(tr("Agrupar"), .30f,
            [this] { m_presenter.api().group_selected_entities(); refresh(); });
        group_entities.style.layout.require_size(76.f, 28.f);
        auto& remove_entity = m_toolbar->add<bgui::button>(
            "",
            .30f,
            [this]() { m_presenter.api().delete_selected_entity(); }
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

        m_filter = &panel::add_input(*m_toolbar,
            "",
            "Search entities...",
            .30f
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
        const auto* mouse_target = bgui::get_mouse_target();
        if (delete_down && !m_delete_was_down &&
            !(mouse_target && mouse_target->has_class("model-editor-viewport-input")) &&
            !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus()) && !visual_script_has_keyboard_focus() &&
            !viewport_input_blocked_by_modal() && !m_presenter.api().is_playing())
            m_presenter.api().delete_selected_entity();
        m_delete_was_down = delete_down;
        if (m_delete_button) {
            const bool can_delete = m_presenter.api().can_delete_selection();
            m_delete_button->receives_input(can_delete);
            m_delete_button->set_style_state(can_delete ? bgui::state::normal : bgui::state::disabled);
        }

        const auto now = std::chrono::steady_clock::now();
        const auto events = m_presenter.api().events();
        const auto scene_revision = events.revision(editor_event::scene);
        const auto properties_revision = events.revision(editor_event::properties);
        if (!m_pool || now >= m_snapshot_due || scene_revision != m_scene_revision ||
            properties_revision != m_properties_revision || m_filter->get_buffer() != m_last_filter) {
            m_presenter.api().synchronize_scene();
            const auto entries = m_presenter.entries();
            if (entries != m_last_entries || m_filter->get_buffer() != m_last_filter)
                refresh(entries);
            m_scene_revision = scene_revision;
            m_properties_revision = properties_revision;
            m_snapshot_due = now + std::chrono::milliseconds(250);
        }
        update_pool();
        update_row_styles();
    }

    void refresh() {
        if (!*m_surface_alive) return;
        refresh(m_presenter.entries());
    }

    std::optional<entity_id> selected() const {
        return m_presenter.api().selected_entity();
    }

    std::optional<entity_id> entity_at(const bgui::vec2i& position) const {
        if (!*m_surface_alive || !m_root) return std::nullopt;
        const auto contains = [position](const bgui::element& element) {
            const auto rect = element.processed_rect();
            return position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w;
        };
        if (!contains(*m_list)) return std::nullopt;
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

        if (!m_pool && !m_filtered)
            for (const auto& row : m_rows)
                if (row.node) m_expanded_entities[row.id] = row.node->is_expanded();
        const auto filter = entities_presenter::normalized_filter(m_filter->get_buffer());
        if (m_filter->get_buffer() != m_last_filter) m_folded_matches.clear();
        m_filtered = !filter.empty();
        const bool create_scene_tree = !m_scene_tree;
        if (!m_scene_tree) {
            m_scene_tree = &m_list->add<bgui::tree>("Scene", .30f, true);
            m_scene_tree->set_icon("directory_open", 0.24f);
        }
        m_last_entries = entries;
        m_last_filter = m_filter->get_buffer();
        if (entries.size() >= virtualization_threshold) {
            if (!m_pool) {
                m_rows.clear();
                clear_tree(*m_scene_tree);
                auto& parent = m_scene_tree->children();
                m_before = &parent.add<bgui::element>();
                m_pool = &parent.add<bgui::linear>(bgui::orientation::vertical);
                m_pool->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
                m_after = &parent.add<bgui::element>();
                for (auto* spacer : {m_before, m_after}) {
                    spacer->style.visual.visible = false;
                    spacer->receives_input(false);
                }
                m_empty = &parent.add<bgui::text>("No entities found", .30f);
            }
            m_structure_dirty = true;
            update_pool();
            update_row_styles();
            return;
        }
        m_pool = nullptr;
        m_empty = nullptr;
        m_rows.clear();
        clear_tree(*m_scene_tree);
        const auto hierarchy = entities_presenter::build_hierarchy(entries, filter);
        std::unordered_set<entity_id> recursion_path;
        for (const auto id : hierarchy.roots) {
            const auto item = hierarchy.entries_by_id.find(id);
            if (item != hierarchy.entries_by_id.end())
                build_entity_node(*item->second, m_scene_tree->children(),
                    hierarchy.children_by_parent, hierarchy.entries_by_id, recursion_path);
        }

        if (!hierarchy.has_matches) {
            auto& empty = m_scene_tree->children().add<bgui::text>(
                filter.empty() ? "No entities" : "No entities found",
                .30f
            );
            empty.add_class("entities-empty");
        }

        if (create_scene_tree)
            m_scene_tree->set_expanded(true);

        update_row_styles();
    }
};

}
