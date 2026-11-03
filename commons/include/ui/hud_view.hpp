#pragma once
#include <glad.h>
#include <bgui.hpp>
#include "components/hud.hpp"
#include "components/camera.hpp"
#include "core/ecs.hpp"

namespace commons {
// Mount in the same BGUI tree as the viewport to share input routing and clipping.
class hud_view final : public bgui::linear {
    struct binding { std::shared_ptr<hud_element> data; bgui::element* widget; float scale; std::string kind; int parent; };
    std::weak_ptr<ecs> m_registry;
    std::shared_ptr<camera> m_camera;
    bgui::element& m_viewport;
    std::function<bool()> m_active;
    std::function<std::shared_ptr<camera>()> m_camera_provider;
    std::vector<binding> m_bindings;
    std::vector<std::shared_ptr<hud_element>> m_nodes;
    std::vector<int> m_parents;
public:
    hud_view(std::shared_ptr<ecs> registry, std::shared_ptr<camera> view_camera,
             bgui::element& viewport, std::function<bool()> active = {},
             std::function<std::shared_ptr<camera>()> camera_provider = {});
    bgui::vec4i element_rect(const std::shared_ptr<hud_element>& e) const {
        for (const auto& b : m_bindings) if (b.data == e) return b.widget->processed_rect();
        return {0,0,0,0};
    }
    bool clips_children() const override { return true; }
    bool supports_render_cache() const override { return false; }
    void on_update() override;
};
}
