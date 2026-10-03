#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "systems/system.hpp"

namespace bgui {
    class button;
    class image;
    class input_area;
    class linear;
    class window;
}

namespace COMMONS_NS {
    class camera;
}

namespace editor {
    class editor_system final : public COMMONS_NS::system {
    public:
        ~editor_system() override;
        void setup(const std::shared_ptr<COMMONS_NS::ecs>& registry) override;
        void create_editor_camera_settings();
        void open_editor_camera_settings();
        void update(const std::shared_ptr<COMMONS_NS::ecs> &registry) override;

    private:
        std::weak_ptr<COMMONS_NS::camera> m_camera;
        bgui::linear* m_entities_list{nullptr};
        bgui::linear* m_components_list{nullptr};
        bgui::linear* m_window_context{nullptr};
        bgui::linear* m_config_menu{nullptr};
        bgui::linear* m_editor_settings{nullptr};
        bgui::button* m_config_button{nullptr};
        bgui::image* m_framebuffer_image{nullptr};
        bool m_scene_initialized{false};
        bool m_left_view_active{false};
        bool m_right_move_active{false};
        bool m_left_mouse_was_down{false};
        bool m_right_mouse_was_down{false};
        std::vector<std::pair<uint32_t, uint32_t>> m_scene_signature;
        uint32_t m_selected_entity{0};
        uint32_t m_editor_camera_entity{0};
        unsigned int m_framebuffer_texture{0};
        int m_last_mouse_x{0};
        int m_last_mouse_y{0};
        float m_last_update_time{0.f};
        float m_camera_move_speed{4.f};
        float m_camera_look_sensitivity{0.12f};
        float m_camera_zoom_sensitivity{3.f};
        float m_ui_scale{1.f};

        void refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void select_entity(uint32_t entity_id, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void setup_scene_view_panel(bgui::window& window);
        void setup_entities_panel(bgui::window& window);
        void setup_components_panel(bgui::window& window);
        void setup_assets_panel(bgui::window& window);
        void update_scene_view_panel(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void rebuild_entities(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void rebuild_components(const std::shared_ptr<COMMONS_NS::ecs>& registry);
    };
}