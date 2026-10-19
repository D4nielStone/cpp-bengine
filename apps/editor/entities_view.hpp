#pragma once

#include <bgui.hpp>

#include "editor_context.hpp"
#include "panel.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor::ui {

class entities_view final : public panel {
public:
    using entity_id = std::uint32_t;
    using name_provider = std::function<std::string(entity_id)>;

private:
    struct entry {
        entity_id id{};
        std::string label;

        bool operator==(const entry& other) const noexcept {
            return id == other.id && label == other.label;
        }
    };

    static constexpr const char* selected_class = "editor-entity-row-selected";
    static constexpr const char* selected_class_legacy = "entity-row-selected";

    std::shared_ptr<editor_context> m_context;
    name_provider m_get_name;

    bgui::linear* m_root{nullptr};
    bgui::linear* m_toolbar{nullptr};
    bgui::linear* m_list{nullptr};
    bgui::tree* m_scene_tree{nullptr};

    std::vector<std::pair<entity_id, bgui::button*>> m_rows;
    std::vector<entry> m_last_entries;
    std::optional<entity_id> m_last_selection;

    std::string entity_label(const entity_id id) const {
        if (m_get_name) {
            auto name = m_get_name(id);
            if (!name.empty())
                return name;
        }
        return "Entity " + std::to_string(id);
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
            entry{id, {}},
            [](const entry& a, const entry& b) { return a.id < b.id; }
        );
    }

    void update_row_styles() {
        const bgui::color transparent{0.f, 0.f, 0.f, 0.f};
        const bgui::color selected_background{0.16f, 0.30f, 0.44f, 1.f};

        for (const auto& [id, row] : m_rows) {
            if (!row)
                continue;

            const bool is_selected =
                m_context && m_context->selected_entity &&
                *m_context->selected_entity == id;
            if (is_selected) {
                row->add_class(selected_class);
                row->add_class(selected_class_legacy);
                row->style.visual.background.normal = selected_background;
                row->style.visual.background.hover = selected_background;
                row->style.visual.background.pressed = selected_background;
                row->style.visual.background.focused = selected_background;
            } else {
                row->remove_class(selected_class);
                row->remove_class(selected_class_legacy);
                row->style.visual.background.normal = transparent;
                row->style.visual.background.hover = transparent;
                row->style.visual.background.pressed = transparent;
                row->style.visual.background.focused = transparent;
            }
            row->style.visual.border.normal = transparent;
            row->style.visual.border.hover = transparent;
            row->style.visual.border.pressed = transparent;
            row->style.visual.border.focused = transparent;
            row->style.visual.border_radius = 0.f;
            row->style.visual.border_size = 0.f;
            row->mark_style_dirty();
        }
    }

    void select(const entity_id id) {
        if (!m_context || m_context->selected_entity == id)
            return;

        m_context->selected_entity = id;
        m_last_selection = id;
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

        auto& icon = row_layout.add<bgui::icon>("circle", 0.2f);
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

        m_rows.emplace_back(id, &row);
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
        m_root = &window.get_context();
        m_root->set_scrollable(false);

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
        m_toolbar->style.layout.padding = bgui::vec4i{4, 2, 4, 2};
        m_toolbar->style.visual.visible = false;

        auto& add_entity = m_toolbar->add<bgui::button>(
            "+",
            0.28f,
            [this]() {
                if (!m_context || !m_context->registry)
                    return;
                const auto created = m_context->registry->create();
                select(created.id);
            }
        );
        add_entity.add_class("entities-add-button");
        add_entity.style.layout.require_mode(
            bgui::mode::pixel,
            bgui::mode::pixel
        );
        add_entity.style.layout.require_width(bgui::mode::pixel, 20.f);
        add_entity.style.layout.require_height(bgui::mode::pixel, 20.f);
        add_entity.get_label().style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };

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
        if (!m_root || !m_list)
            return;

        const auto entries = collect_entries();

        // Limpa a seleção se a entidade selecionada não existe mais.
        if (m_context && m_context->selected_entity &&
            !contains(entries, *m_context->selected_entity)) {
            m_context->selected_entity = std::nullopt;
        }

        if (entries != m_last_entries)
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

        const bool create_scene_tree = !m_scene_tree;
        if (!m_scene_tree) {
            m_scene_tree = &m_list->add<bgui::tree>(
                "SampleScene 1",
                0.28f,
                false
            );
            m_scene_tree->set_icon("directory_open", 0.24f);
        } else {
            clear_tree(*m_scene_tree);
        }

        for (const auto& e : entries)
            build_row(e, m_scene_tree->children());

        if (entries.empty()) {
            auto& empty = m_scene_tree->children().add<bgui::text>(
                "No entities",
                0.26f
            );
            empty.add_class("entities-empty");
        }

        if (create_scene_tree)
            m_scene_tree->set_expanded(true);

        m_last_entries = entries;

        update_row_styles();
    }
};

}