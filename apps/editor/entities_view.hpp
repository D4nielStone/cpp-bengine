#pragma once

#include <bgui.hpp>

#include "editor_context.hpp"
#include "panel.hpp"

#include <algorithm>
#include <cctype>
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
        std::string normalized_label;

        bool operator==(const entry& other) const noexcept {
            return id == other.id && label == other.label;
        }
    };

    static constexpr const char* selected_class = "editor-entity-row-selected";
    static constexpr const char* selected_class_legacy = "entity-row-selected";

    std::shared_ptr<editor_context> m_context;
    name_provider m_get_name;

    bgui::linear* m_root{nullptr};
    bgui::inputbox* m_filter{nullptr};
    bgui::linear* m_list{nullptr};
    bgui::linear* m_scene_root{nullptr};
    bgui::linear* m_scene_children{nullptr};
    bgui::text* m_scene_disclosure{nullptr};
    bool m_scene_expanded{true};

    std::vector<std::pair<entity_id, bgui::button*>> m_rows;
    std::vector<entry> m_last_entries;
    std::string m_last_filter;
    std::optional<entity_id> m_last_selection;

    static std::string normalize(std::string value) {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
        );
        return value;
    }

    static std::string trim(const std::string& value) {
        const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };

        auto begin = std::find_if_not(value.begin(), value.end(), is_space);
        auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();

        return begin < end ? std::string(begin, end) : std::string{};
    }

    static std::string normalized_filter(const std::string& raw) {
        return normalize(trim(raw));
    }

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

    static bool matches_filter(const entry& e, const std::string& filter) {
        if (filter.empty())
            return true;

        return e.normalized_label.find(filter) != std::string::npos ||
               std::to_string(e.id).find(filter) != std::string::npos;
    }

    static bool contains(const std::vector<entry>& entries, const entity_id id) {
        return std::binary_search(
            entries.begin(),
            entries.end(),
            entry{id, {}, {}},
            [](const entry& a, const entry& b) { return a.id < b.id; }
        );
    }

    void update_row_styles() {
        for (const auto& [id, row] : m_rows) {
            if (!row)
                continue;

            const bool is_selected =
                m_context && m_context->selected_entity &&
                *m_context->selected_entity == id;
            if (is_selected) {
                row->add_class(selected_class);
                row->add_class(selected_class_legacy);
                row->style.visual.background.normal =
                    bgui::color{0.16f, 0.30f, 0.44f, 1.f};
            } else {
                row->remove_class(selected_class);
                row->remove_class(selected_class_legacy);
                row->style.visual.background.normal.reset();
            }
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

    void set_scene_expanded(const bool expanded) {
        m_scene_expanded = expanded;
        if (m_scene_children)
            m_scene_children->set_enable(expanded);
        if (m_scene_disclosure)
            m_scene_disclosure->set_buffer(expanded ? "▾" : "▸");
    }

    void build_row(const entry& e) {
        auto& row_layout = m_scene_children->add<bgui::linear>(
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
            "Entities",
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

        auto& heading = m_root->add<bgui::text>("Entities", 0.31f);
        heading.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        heading.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
        };

        m_filter = &m_root->add<bgui::inputbox>("", "Search entities...", 0.28f);
        m_filter->add_class("editor-input");
        m_filter->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        m_filter->style.layout.require_height(bgui::mode::pixel, 28.f);
        m_filter->style.layout.margin = bgui::vec4i{0, 4, 0, 6};

        m_list = &m_root->add<bgui::linear>(bgui::orientation::vertical);
        m_list->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::match_parent
        );
        m_list->style.layout.align = start_align;
        m_list->style.layout.padding = bgui::vec4i{0, 4, 0, 0};
        m_list->set_scrollable(true);

        refresh();
    }

    void update() override {
        if (!m_root || !m_filter || !m_list)
            return;

        const auto entries = collect_entries();
        const auto current_filter = m_filter->get_buffer();

        // Limpa a seleção se a entidade selecionada não existe mais.
        if (m_context && m_context->selected_entity &&
            !contains(entries, *m_context->selected_entity)) {
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

        const auto raw_filter = m_filter ? m_filter->get_buffer() : std::string{};
        const auto filter = normalized_filter(raw_filter);

        if (!m_scene_root) {
            m_scene_root = &m_list->add<bgui::linear>(
                bgui::orientation::vertical
            );
            m_scene_root->style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );

            auto& scene_header = m_scene_root->add<bgui::button>(
                "",
                0.28f,
                [this]() { set_scene_expanded(!m_scene_expanded); }
            );
            scene_header.add_class("tree-element-label");
            scene_header.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );
            scene_header.get_label().set_enable(false);

            m_scene_disclosure = &scene_header.add<bgui::text>(
                m_scene_expanded ? "▾" : "▸",
                0.28f
            );
            m_scene_disclosure->style.layout.require_width(
                bgui::mode::pixel,
                16.f
            );
            m_scene_disclosure->style.layout.align =
                bgui::vec<2, bgui::alignment>{
                    bgui::alignment::center,
                    bgui::alignment::center
                };

            auto& scene_icon = scene_header.add<bgui::icon>(
                "directory_open",
                0.24f
            );
            scene_icon.style.layout.require_width(bgui::mode::pixel, 18.f);

            auto& scene_label = scene_header.add<bgui::text>("Scene", 0.28f);
            scene_label.add_class("tree-element-label-text");
            scene_label.style.layout.require_mode(
                bgui::mode::stretch,
                bgui::mode::wrap_content
            );
            scene_label.style.layout.align =
                bgui::vec<2, bgui::alignment>{
                    bgui::alignment::start,
                    bgui::alignment::center
                };

            m_scene_children = &m_scene_root->add<bgui::linear>(
                bgui::orientation::vertical
            );
            m_scene_children->style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );
            m_scene_children->style.layout.padding =
                bgui::vec4i{32, 2, 0, 0};
            m_scene_children->add_class("tree-element-children");
            set_scene_expanded(m_scene_expanded);
        } else {
            for (auto& [layer, elements] : m_scene_children->get_elements()) {
                (void)layer;
                while (!elements.empty())
                    m_scene_children->remove(elements.back().get());
            }
        }

        std::size_t visible = 0;

        if (!filter.empty())
            set_scene_expanded(true);

        for (const auto& e : entries) {
            if (!matches_filter(e, filter))
                continue;

            build_row(e);
            ++visible;
        }

        if (visible == 0) {
            auto& empty = m_scene_children->add<bgui::text>(
                entries.empty() ? "No entities" : "No entities found",
                0.26f
            );
            empty.add_class("entities-empty");
        } else if (!filter.empty()) {
            auto& count = m_scene_children->add<bgui::text>(
                std::to_string(visible) + " of " + std::to_string(entries.size()),
                0.24f
            );
            count.add_class("entities-count");
        }

        m_last_entries = entries;
        m_last_filter = raw_filter;

        update_row_styles();
    }
};

}