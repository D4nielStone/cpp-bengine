#pragma once
#include <utils/draw.hpp>
#include <glm/glm.hpp>
#include <array>

namespace editor::ui {
inline void draw_rig_segment(bgui::draw_list& draw, const glm::mat4& matrix,
    const bgui::vec4i& rect, glm::vec3 origin, glm::vec3 tip, bool selected = false) {
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, float shade) {
        const auto pa = matrix * glm::vec4(a, 1), pb = matrix * glm::vec4(b, 1), pc = matrix * glm::vec4(c, 1);
        for (const auto& p : {pa, pb, pc})
            if (p.w <= 1.e-5f || std::abs(p.z) > p.w) return;
        const auto screen = [&](const glm::vec4& p) { return bgui::vec2{
            rect.x + (p.x / p.w * .5f + .5f) * rect.z, rect.y + (.5f - p.y / p.w * .5f) * rect.w}; };
        draw.set_color(selected ? bgui::vec4{shade, shade * .65f, .15f, 1} : bgui::vec4{shade * .65f, shade * .8f, shade, 1});
        draw.add_triangle(screen(pa), screen(pb), screen(pc));
    };
    const auto delta = tip - origin;
    const float length = glm::length(delta);
    if (!std::isfinite(length)) return;
    const float radius = std::max(length * .075f, .015f);
    if (length > 1.e-5f) {
        const auto axis = delta / length;
        const auto side = glm::normalize(glm::cross(axis, std::abs(axis.y) < .9f ? glm::vec3{0,1,0} : glm::vec3{1,0,0})) * radius;
        const auto up = glm::normalize(glm::cross(axis, side)) * radius;
        const auto base = origin + delta * .18f;
        const std::array<glm::vec3, 4> ring{base + side, base + up, base - side, base - up};
        for (int i = 0; i < 4; ++i) {
            triangle(ring[i], ring[(i+1)%4], tip, .55f + .12f * i);
            triangle(origin, ring[(i+1)%4], ring[i], .45f + .1f * i);
        }
    }
    const float joint_radius = radius * .65f;
    const std::array<glm::vec3, 4> ring{tip + glm::vec3{joint_radius,0,0}, tip + glm::vec3{0,0,joint_radius},
        tip - glm::vec3{joint_radius,0,0}, tip - glm::vec3{0,0,joint_radius}};
    for (int i = 0; i < 4; ++i) {
        triangle(tip + glm::vec3{0,joint_radius,0}, ring[i], ring[(i+1)%4], .6f + .1f * i);
        triangle(tip - glm::vec3{0,joint_radius,0}, ring[(i+1)%4], ring[i], .45f + .1f * i);
    }
}
inline bgui::vec4 rig_weight_color(float weight) {
    weight = std::clamp(weight, 0.f, 1.f);
    return {weight, 1 - std::abs(2 * weight - 1), 1 - weight, 1};
}
inline void draw_weight_triangle(bgui::draw_list& draw, std::vector<bgui::draw_vertex> polygon, const bgui::vec4i& rect) {
    for (int edge = 0; edge < 4 && !polygon.empty(); ++edge) {
        const auto distance = [&](const bgui::draw_vertex& v) {
            if (edge == 0) return v.m_position.x - rect.x;
            if (edge == 1) return float(rect.x + rect.z) - v.m_position.x;
            if (edge == 2) return v.m_position.y - rect.y;
            return float(rect.y + rect.w) - v.m_position.y;
        };
        std::vector<bgui::draw_vertex> clipped;
        auto previous = polygon.back();
        float before = distance(previous);
        for (const auto& vertex : polygon) {
            const float after = distance(vertex);
            if ((after >= 0) != (before >= 0)) {
                const float t = before / (before - after);
                clipped.push_back({previous.m_position + (vertex.m_position - previous.m_position) * t,
                    previous.m_color + (vertex.m_color - previous.m_color) * t});
            }
            if (after >= 0) clipped.push_back(vertex);
            previous = vertex; before = after;
        }
        polygon = std::move(clipped);
    }
    std::vector<bgui::draw_vertex> triangles;
    for (std::size_t i = 1; i + 1 < polygon.size(); ++i)
        triangles.insert(triangles.end(), {polygon[0], polygon[i], polygon[i+1]});
    draw.append_vertices(triangles);
}
}
