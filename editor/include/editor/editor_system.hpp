#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "systems/system.hpp"

namespace bgui {
    class image;
    class linear;
}

namespace COMMONS_NS {
    class camera;
}

namespace editor {
    class editor_system final : public COMMONS_NS::system {
    public:
        void setup(const std::shared_ptr<COMMONS_NS::ecs>& registry) override;
        void update(const std::shared_ptr<COMMONS_NS::ecs>& registry) override;

    private:
        std::weak_ptr<COMMONS_NS::camera> m_camera;
        bgui::linear* m_entities_list{nullptr};
        bgui::linear* m_components_list{nullptr};
        bgui::linear* m_window_context{nullptr};
        bgui::image* m_framebuffer_image{nullptr};
        bool m_scene_initialized{false};
        std::vector<std::pair<uint32_t, uint32_t>> m_scene_signature;
        uint32_t m_selected_entity{0};
        int m_framebuffer_display_width{0};
        int m_framebuffer_display_height{0};

        void refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void select_entity(uint32_t entity_id, const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void rebuild_entities(const std::shared_ptr<COMMONS_NS::ecs>& registry);
        void rebuild_components(const std::shared_ptr<COMMONS_NS::ecs>& registry);
    };
}