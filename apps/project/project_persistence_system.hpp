#pragma once

#include "core/ecs.hpp"

#include <array>
#include <functional>
#include <vector>
#include "loaders/model.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace commons { struct animation_clip; }

namespace editor::ui {

bool validate_scene_entities(const rapidjson::Value& entities, std::string& error);

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
    using animation_assets_provider = std::function<std::vector<std::shared_ptr<commons::animation_clip>>() >;
    void set_animation_assets_provider(animation_assets_provider provider) { m_animation_assets_provider=std::move(provider); }
    bool save(const std::string& path, std::string& error) const;
    bool save_prefab(const std::string& path, std::uint32_t root, std::string& error) const;
    bool save_snapshot(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error) const;
    bool switch_scene(const std::string& project, const std::filesystem::path& scene, std::string& error) const;
    bool load_runtime_scene(const std::string& project, const std::filesystem::path& scene, std::string& error) const {
        if (scene.empty() || (scene.extension() != ".scene" && scene.extension() != ".world")) {
            error = "Choose a project scene (.scene or .world).";
            return false;
        }
        return load_impl(project, scene, error, true);
    }
    const std::filesystem::path& active_scene() const noexcept { return m_scene_path; }
    bool has_project_manifest() const noexcept { return !m_project_path.empty(); }
    void set_migration_writes_enabled(bool enabled) noexcept { m_migration_writes_enabled = enabled; }
    bool has_unsaved_changes(const std::string& path) const;

private:
    bool load_impl(const std::string& path, const std::filesystem::path& scene, std::string& error, bool runtime = false) const;
    bool serialize_project(const std::string& path, std::string& error,
        bool write_files, std::string& snapshot, bool update_paths = true, std::optional<std::uint32_t> prefab_root = {}) const;
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
    animation_assets_provider m_animation_assets_provider;
    asset_loading_hooks m_asset_hooks;
    bool m_migration_writes_enabled{true};
    mutable std::string m_project_version{"0.1.0"};
    mutable std::filesystem::path m_asset_root;
    mutable std::filesystem::path m_project_path;
    mutable std::filesystem::path m_scene_path;
    mutable std::array<float, 3> m_global_gravity{0.f, -9.8f, 0.f};
};

}
