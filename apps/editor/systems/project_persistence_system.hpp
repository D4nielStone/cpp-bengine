#pragma once

#include "core/ecs.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace editor::ui {

class project_persistence_system final {
public:
    using entity_name_map = std::unordered_map<std::uint32_t, std::string>;
    using entity_state_set = std::unordered_set<std::uint32_t>;

    project_persistence_system(
        std::shared_ptr<commons::ecs> registry,
        std::uint32_t editor_camera_id,
        entity_name_map* entity_names = nullptr,
        entity_state_set* hidden_entities = nullptr,
        entity_state_set* locked_entities = nullptr
    );

    void set_entity_names(entity_name_map* entity_names);
    const std::array<float, 3>& global_gravity() const noexcept;
    void set_global_gravity(const std::array<float, 3>& gravity) noexcept;
    const std::filesystem::path& get_repository_root(const std::string& project_path) const;

    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error) const;

private:
    std::shared_ptr<commons::ecs> m_registry;
    std::uint32_t m_editor_camera_id;
    entity_name_map* m_entity_names;
    entity_state_set* m_hidden_entities;
    entity_state_set* m_locked_entities;
    mutable std::filesystem::path m_asset_root;
    mutable std::filesystem::path m_project_path;
    mutable std::filesystem::path m_scene_path;
    mutable std::array<float, 3> m_global_gravity{0.f, -9.8f, 0.f};
};

}
