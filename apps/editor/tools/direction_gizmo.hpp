#pragma once

#include "components/camera.hpp"
#include <bgui.hpp>
#include <elem/image.hpp>
#include <algorithm>
#include <array>
#include <memory>

namespace editor::ui {

// Only the view rotation affects directions; camera translation is irrelevant.
inline std::array<glm::vec3, 3> direction_gizmo_axes(const glm::mat4& view) {
    const glm::mat3 rotation(view);
    return {rotation * glm::vec3(1, 0, 0), rotation * glm::vec3(0, 1, 0),
        rotation * glm::vec3(0, 0, 1)};
}

inline bgui::vec4i direction_gizmo_rect(bgui::vec4i viewport, float scale) {
    const int margin = std::max(1, int(12 * scale));
    const int size = std::max(0, std::min({int(100 * scale),
        viewport.z - 2 * margin, viewport.w - 2 * margin}));
    return {viewport.x + viewport.z - margin - size, viewport.y + margin, size, size};
}

class direction_gizmo final : public bgui::element {
public:
    direction_gizmo(std::shared_ptr<commons::camera> camera, bgui::image& image)
        : m_camera(std::move(camera)), m_image(image) {
        receives_input(false);
        set_flex(false);
    }

    void on_update() override {
        const auto rect = direction_gizmo_rect(m_image.processed_rect(), bgui::get_global_scale());
        set_final_rect(rect.x, rect.y, rect.z, rect.w);
    }
    bool supports_render_cache() const override { return false; }

    void get_requires(bgui::draw_data* data) override {
        const auto rect = processed_rect();
        if (rect.z < 24 || !m_camera) return;
        auto& draw = data->m_draw_list;
        const auto color = draw.get_color();
        const float unit = rect.z / 100.f;
        const bgui::vec2 center{rect.x + rect.z * .5f, rect.y + rect.w * .5f};
        draw.set_color({.08f, .09f, .12f, .72f});
        draw.add_circle_filled(center, 49 * unit);
        const auto axes = direction_gizmo_axes(m_camera->viewMatrix);
        std::array<int, 3> order{0, 1, 2};
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return axes[a].z < axes[b].z;
        });
        const std::array<bgui::vec4, 3> colors{{{.95f, .25f, .28f, 1},
            {.35f, .85f, .35f, 1}, {.3f, .55f, 1.f, 1}}};
        for (const int axis : order) {
            const bgui::vec2 end{center.x + axes[axis].x * 32 * unit,
                center.y - axes[axis].y * 32 * unit};
            draw.set_color(colors[axis]);
            draw.add_line(center, end, 2 * unit);
            draw.add_circle_filled(end, 9 * unit);
            draw.set_color({.06f, .07f, .09f, 1});
            auto stroke = [&](float x1, float y1, float x2, float y2) {
                draw.add_line({end.x + x1 * unit, end.y + y1 * unit},
                    {end.x + x2 * unit, end.y + y2 * unit}, 1.5f * unit);
            };
            if (axis == 0) { stroke(-3,-4,3,4); stroke(-3,4,3,-4); }
            if (axis == 1) { stroke(-3,-4,0,0); stroke(3,-4,0,0); stroke(0,0,0,4); }
            if (axis == 2) { stroke(-3,-4,3,-4); stroke(3,-4,-3,4); stroke(-3,4,3,4); }
        }
        draw.set_color(color);
    }
private:
    std::shared_ptr<commons::camera> m_camera;
    bgui::image& m_image;
};

}
