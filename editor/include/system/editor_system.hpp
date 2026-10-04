#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "guizmo/grid_gizmo.hpp"
#include "system/editor_config.hpp"
#include "system/editor_ui.hpp"
#include "guizmo/transform_gizmo.hpp"
#include "systems/system.hpp"

namespace bgui {
    class window;
}

namespace COMMONS_NS {
    class camera;
    class ecs;
}

namespace editor {
    class editor_system final : public COMMONS_NS::system {
    public:
        ~editor_system() override;
        void setup(const std::shared_ptr<COMMONS_NS::ecs>& registry) override;
        void update(const std::shared_ptr<COMMONS_NS::ecs> &registry) override;

        float camera_move_speed() const;
        float camera_look_sensitivity() const;
        float camera_zoom_sensitivity() const;
        float ui_scale() const;
        float camera_min_z_far() const;
        bool grid_enabled() const;
        float grid_spacing() const;
        float grid_extent() const;

        void set_camera_move_speed(float value);
        void set_camera_look_sensitivity(float value);
        void set_camera_zoom_sensitivity(float value);
        void set_ui_scale(float value);
        void set_camera_min_z_far(float value);
        void set_grid_enabled(bool value);
        void set_grid_spacing(float value);
        void set_grid_extent(float value);

    private:
        std::weak_ptr<COMMONS_NS::camera> m_camera;
        std::weak_ptr<COMMONS_NS::ecs> m_registry;
        std::weak_ptr<COMMONS_NS::ecs> m_scene_file_registry;
        std::string m_project_config_path;
        std::string m_project_name;
        std::string m_current_scene;
        std::vector<std::string> m_project_scenes;
        bool m_scene_file_save{false};
        bool m_scene_initialized{false};
        bool m_left_view_active{false};
        bool m_right_move_active{false};
        bool m_left_mouse_was_down{false};
        bool m_right_mouse_was_down{false};
        editor_config m_config;
        editor_ui m_ui;
        transform_gizmo m_transform_gizmo;
        std::vector<std::pair<uint32_t, uint32_t>> m_scene_signature;
        uint32_t m_selected_entity{0};
        uint32_t m_editor_camera_entity{0};
        unsigned int m_framebuffer_texture{0};
        int m_last_mouse_x{0};
        int m_last_mouse_y{0};
        float m_last_update_time{0.f};
        float m_last_editor_cache_save_time{0.f};
        float m_camera_min_z_far{0.01f};
        grid_gizmo m_grid_gizmo;

        void refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void select_entity(uint32_t entity_id, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void setup_scene_view_panel(bgui::window& window);
        void setup_entities_panel(bgui::window& window);
        void setup_components_panel(bgui::window& window);
        void setup_assets_panel(bgui::window& window, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void update_scene_view_panel(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void rebuild_entities(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void rebuild_components(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void open_scene_file_dialog(bool save, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void create_scene_file_dialog();
        void apply_scene_file_path(const std::string& path);
        void create_project(const std::string& directory);
        void open_project(const std::string& path);
        void save_project(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        bool save_project_config();
        bool save_editor_cache(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        bool load_editor_cache(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void refresh_project_scenes();
        void load_project_scene(const std::string& scene, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void browse_model_file(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void import_model_file(const std::string& path, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        bool save_scene_file(const std::string& path, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        std::size_t import_scene_file(const std::string& path, const std::shared_ptr<COMMONS_NS::ecs>& registry);
    };
}