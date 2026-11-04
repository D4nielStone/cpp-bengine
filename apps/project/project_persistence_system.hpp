#pragma once

#include "core/ecs.hpp"

#include <array>
#include <functional>
#include <vector>
#include "loaders/model.hpp"
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

    struct asset_loading_hooks {
        std::function<void(const std::vector<std::string>&)> prepare;
        std::function<std::unique_ptr<commons::model>(const std::string&)> instantiate;
        std::function<void(const std::shared_ptr<commons::ecs>&)> finish;
        std::function<commons::material(const std::filesystem::path&)> material;
    };
    void set_asset_loading_hooks(asset_loading_hooks hooks) { m_asset_hooks = std::move(hooks); }

    void set_entity_names(entity_name_map* entity_names);
    const std::array<float, 3>& global_gravity() const noexcept;
    void set_global_gravity(const std::array<float, 3>& gravity) noexcept;

    using material_save_hook = std::function<std::filesystem::path(const commons::material&, const std::filesystem::path&)>;
    void set_material_save_hook(material_save_hook hook) { m_material_save_hook = std::move(hook); }
    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error) const;
    void set_migration_writes_enabled(bool enabled) noexcept { m_migration_writes_enabled = enabled; }
    bool has_unsaved_changes(const std::string& path) const;

private:
    bool serialize_project(const std::string& path, std::string& error,
        bool write_files, std::string& snapshot) const;
    struct saved_state {
        std::string snapshot;
        std::filesystem::path scene_path;
    };
    mutable std::unordered_map<std::string, saved_state> m_saved_states;
    std::shared_ptr<commons::ecs> m_registry;
    std::uint32_t m_editor_camera_id;
    entity_name_map* m_entity_names;
    entity_state_set* m_hidden_entities;
    entity_state_set* m_locked_entities;
    material_save_hook m_material_save_hook;
    asset_loading_hooks m_asset_hooks;
    bool m_migration_writes_enabled{true};
    mutable std::string m_project_version{"0.1.0"};
    mutable std::filesystem::path m_asset_root;
    mutable std::filesystem::path m_project_path;
    mutable std::filesystem::path m_scene_path;
    mutable std::array<float, 3> m_global_gravity{0.f, -9.8f, 0.f};
};

}
