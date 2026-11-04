#pragma once

#include "editor_api.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace editor::ui {

class entities_presenter {
public:
    using entity_id = editor_api::entity_id;
    using name_provider = editor_api::name_provider;

    using entry = editor_api::scene_entity;

    explicit entities_presenter(std::shared_ptr<editor_context> context, name_provider get_name)
        : m_api(std::move(context)), m_get_name(std::move(get_name)) {}

    editor_api& api() { return m_api; }
    const editor_api& api() const { return m_api; }

    std::vector<entry> entries() const {
        return m_api.scene_entities(m_get_name);
    }

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
            normalize(item.label).find(filter) != std::string::npos ||
            std::to_string(item.id).find(filter) != std::string::npos;
    }

    struct hierarchy {
        std::unordered_map<entity_id, const entry*> entries_by_id;
        std::unordered_map<entity_id, std::vector<entity_id>> children_by_parent;
        std::vector<entity_id> roots;
        bool has_matches{false};
    };

    // Entry pointers remain valid while the view builds from this snapshot.
    static hierarchy build_hierarchy(const std::vector<entry>& entries, const std::string& filter) {
        hierarchy result;
        for (const auto& item : entries)
            result.entries_by_id.emplace(item.id, &item);

        std::unordered_set<entity_id> included;
        for (const auto& item : entries) {
            if (!matches_filter(item, filter))
                continue;
            auto current = item.id;
            while (current && included.insert(current).second) {
                const auto parent = result.entries_by_id.at(current)->parent;
                if (!parent || !result.entries_by_id.contains(parent))
                    break;
                current = parent;
            }
        }

        for (const auto& item : entries) {
            if (!included.contains(item.id))
                continue;
            const auto parent = item.parent;
            if (parent && included.contains(parent))
                result.children_by_parent[parent].push_back(item.id);
            else
                result.roots.push_back(item.id);
        }

        result.has_matches = !included.empty();
        return result;
    }

private:
    editor_api m_api;
    name_provider m_get_name;
};

}
