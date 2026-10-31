#pragma once

#include "components/camera.hpp"
#include <bgui.hpp>
#include <elem/image.hpp>
#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <cstdint>
#include <string>

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
        if (m_material.m_texture.m_buffer.empty() || m_last_size != rect.z ||
            m_last_axes != direction_gizmo_axes(m_camera->viewMatrix)) {
            // Submit through the regular image queue so later windows cover the gizmo.
            // The global draw list is intentionally reserved for screen-wide overlays.
            bgui::draw_list draw;
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
            update_texture(draw, rect);
        }
        m_material.m_use_tex = true;
        m_material.m_shader_tag = "ui::image";
        m_material.set("border_radius", 0.f);
        m_material.set("border_size", 0.f);
        data->enqueue({m_material, 6, {float(rect.x), float(rect.y), float(rect.z), float(rect.w)}});
    }
private:
    void update_texture(const bgui::draw_list& draw, const bgui::vec4i& rect) {
        const auto axes = direction_gizmo_axes(m_camera->viewMatrix);
        auto& texture = m_material.m_texture;
        if (!texture.m_buffer.empty() && m_last_axes == axes && m_last_size == rect.z)
            return;
        m_last_axes = axes;
        m_last_size = rect.z;
        texture.m_path = "editor-direction-gizmo:" + std::to_string(m_texture_id);
        texture.m_size = {float(rect.z), float(rect.w)};
        texture.m_has_alpha = true;
        texture.m_generate_mipmap = false;
        texture.m_buffer.assign(size_t(rect.z) * rect.w * 4, 0);
        ++texture.m_revision;
        const auto& vertices = draw.get_vertices();
        auto edge = [](bgui::vec2 a, bgui::vec2 b, bgui::vec2 p) {
            return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
        };
        for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
            const auto a = vertices[i].m_position - bgui::vec2{float(rect.x), float(rect.y)};
            const auto b = vertices[i + 1].m_position - bgui::vec2{float(rect.x), float(rect.y)};
            const auto c = vertices[i + 2].m_position - bgui::vec2{float(rect.x), float(rect.y)};
            const auto rgba = vertices[i].m_color;
            const float area = edge(a, b, c);
            if (std::abs(area) < 0.00001f) continue;
            const int left = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
            const int top = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
            const int right = std::min(rect.z, int(std::ceil(std::max({a.x, b.x, c.x}))));
            const int bottom = std::min(rect.w, int(std::ceil(std::max({a.y, b.y, c.y}))));
            for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                const bgui::vec2 point{x + .5f, y + .5f};
                if (edge(a, b, point) * area < 0 || edge(b, c, point) * area < 0 ||
                    edge(c, a, point) * area < 0) continue;
                auto* pixel = &texture.m_buffer[(size_t(y) * rect.z + x) * 4];
                const float alpha = rgba.w + pixel[3] / 255.f * (1 - rgba.w);
                for (int channel = 0; channel < 3; ++channel)
                    pixel[channel] = static_cast<unsigned char>(std::clamp(
                        (rgba[channel] * rgba.w + pixel[channel] / 255.f * pixel[3] / 255.f *
                        (1 - rgba.w)) / alpha, 0.f, 1.f) * 255);
                pixel[3] = static_cast<unsigned char>(alpha * 255);
            }
        }
    }
    // Cache keys remain unique even when a destroyed widget's address is reused.
    inline static std::uint64_t s_next_texture_id{0};
    const std::uint64_t m_texture_id{++s_next_texture_id};
    std::array<glm::vec3, 3> m_last_axes{};
    int m_last_size{0};
    std::shared_ptr<commons::camera> m_camera;
    bgui::image& m_image;
};

}
