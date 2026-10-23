#pragma once

#include <bgui.hpp>

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "editor_context.hpp"
#include "panel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editor::ui {
namespace detail {

class entities_refreshing_layout final : public bgui::linear {
public:
    entities_refreshing_layout()
        : bgui::linear(bgui::orientation::vertical) {}

    std::function<void()> before_update;

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
        bgui::button* selection{nullptr};
        bgui::icon* visibility{nullptr};
        bgui::icon* lock{nullptr};
    };

    static constexpr const char* selected_class = "editor-entity-row-selected";
    static constexpr const char* selected_class_legacy = "entity-row-selected";

    std::shared_ptr<editor_context> m_context;
    name_provider m_get_name;

    detail::entities_refreshing_layout* m_root{nullptr};
    bgui::linear* m_toolbar{nullptr};
    bgui::inputbox* m_filter{nullptr};
    bgui::linear* m_list{nullptr};
    bgui::tree* m_scene_tree{nullptr};

    std::vector<row> m_rows;
    std::vector<entry> m_last_entries;
    std::string m_last_filter;
    std::optional<entity_id> m_last_selection;

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
        const bgui::color selected_background{0.16f, 0.30f, 0.44f, 1.f};

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

    void build_row(const entry& e, bgui::linear& parent) {
        auto& row_layout = parent.add<bgui::linear>(
            bgui::orientation::horizontal
        );
        row_layout.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        row_layout.style.layout.padding = bgui::vec4i{0, 2, 0, 2};
        row_layout.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
        };

        auto& icon = row_layout.add<bgui::icon>(entity_icon(e.id), 0.2f);
        icon.add_class("entity-row-icon");
        icon.style.layout.require_mode(
            bgui::mode::pixel,
            bgui::mode::wrap_content
        );
        icon.style.layout.require_width(bgui::mode::pixel, 10.f);

        const auto id = e.id;
        auto& row = row_layout.add<bgui::button>(
            e.label,
            0.28f,
            [this, id]() { select(id); }
        );
        row.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        row.get_label().style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
        };
        row.get_label().set_wrap(false);

        auto& visibility_button = row_layout.add<bgui::button>(
            "",
            0.26f,
            [this, id]() { toggle_visibility(id); }
        );
        configure_icon_button(visibility_button, 20);
        auto& visibility_icon = add_button_icon(
            visibility_button,
            "eye",
            0.24f
        );

        auto& lock_button = row_layout.add<bgui::button>(
            "",
            0.26f,
            [this, id]() { toggle_lock(id); }
        );
        configure_icon_button(lock_button, 20);
        auto& lock_icon = add_button_icon(lock_button, "key", 0.24f);

        m_rows.push_back({id, &row, &visibility_icon, &lock_icon});
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
            [this]() {
                if (!m_context || !m_context->registry)
                    return;
                const auto created = m_context->registry->create();
                m_context->record_entity_creation(created.id);
                select(created.id);
            }
        );
        add_entity.add_class("entities-add-button");
        configure_icon_button(add_entity, 18);
        add_button_icon(add_entity, "plus", 0.26f);

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
        if (!m_root || !m_filter || !m_list)
            return;

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

        if (entries != m_last_entries || current_filter != m_last_filter)
            refresh(entries);

        const auto selection = m_context ? m_context->selected_entity : std::nullopt;
        if (selection != m_last_selection)
            update_row_styles();

        m_last_selection = selection;
    }

    void refresh() {
        refresh(collect_entries());
    }

    std::optional<entity_id> selected() const {
        return m_context ? m_context->selected_entity : std::nullopt;
    }

    bgui::linear* root() const noexcept {
        return m_root;
    }

private:
    void refresh(const std::vector<entry>& entries) {
        if (!m_list)
            return;

        m_rows.clear();
        const auto filter = normalized_filter(
            m_filter ? m_filter->get_buffer() : std::string{}
        );
        std::vector<entry> visible_entries;
        std::copy_if(
            entries.begin(),
            entries.end(),
            std::back_inserter(visible_entries),
            [&filter](const entry& item) {
                return matches_filter(item, filter);
            }
        );

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

        for (const auto& item : visible_entries)
            build_row(item, m_scene_tree->children());

        if (visible_entries.empty()) {
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

        update_row_styles();
    }
};

}