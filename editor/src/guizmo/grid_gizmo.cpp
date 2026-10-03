#include "guizmo/grid_gizmo.hpp"

#include <algorithm>
#include <cmath>

#include "components/camera.hpp"

#include <bgui.hpp>

namespace {
    bool clip_segment(const bgui::vec4i& viewport, bgui::vec2& start, bgui::vec2& end) {
        const float left = static_cast<float>(viewport.x);
        const float top = static_cast<float>(viewport.y);
        const float right = static_cast<float>(viewport.x + viewport.z);
        const float bottom = static_cast<float>(viewport.y + viewport.w);
        const float delta_x = end.x - start.x;
        const float delta_y = end.y - start.y;
        float first = 0.f;
        float last = 1.f;
        const auto clip_edge = [&first, &last](const float p, const float q) {
            if (std::abs(p) < 0.0001f)
                return q >= 0.f;
            const float amount = q / p;
            if (p < 0.f) {
                if (amount > last)
                    return false;
                first = std::max(first, amount);
            } else {
                if (amount < first)
                    return false;
                last = std::min(last, amount);
            }
            return true;
        };

        if (!clip_edge(-delta_x, start.x - left) || !clip_edge(delta_x, right - start.x) ||
            !clip_edge(-delta_y, start.y - top) || !clip_edge(delta_y, bottom - start.y))
            return false;

        const auto original_start = start;
        start = original_start + bgui::vec2{delta_x, delta_y} * first;
        end = original_start + bgui::vec2{delta_x, delta_y} * last;
        return true;
    }

    bool project_clip(
        const COMMONS_NS::camera& camera,
        const bgui::vec4i& viewport,
        const glm::vec4& clip,
        bgui::vec2& screen)
    {
        if (clip.w <= 0.0001f || viewport.z <= 0 || viewport.w <= 0)
            return false;

        float ndc_x = clip.x / clip.w;
        float ndc_y = clip.y / clip.w;
        const float source_aspect = camera.viewportFBO.y > 0
            ? static_cast<float>(camera.viewportFBO.x) / camera.viewportFBO.y
            : static_cast<float>(viewport.z) / viewport.w;
        const float target_aspect = static_cast<float>(viewport.z) / viewport.w;
        if (source_aspect > target_aspect)
            ndc_x *= source_aspect / target_aspect;
        else if (source_aspect < target_aspect)
            ndc_y *= target_aspect / source_aspect;

        screen = {
            viewport.x + (ndc_x * 0.5f + 0.5f) * viewport.z,
            viewport.y + (0.5f - ndc_y * 0.5f) * viewport.w
        };
        return true;
    }

    bool project_segment(
        const COMMONS_NS::camera& camera,
        const bgui::vec4i& viewport,
        const COMMONS_NS::fvec3& world_start,
        const COMMONS_NS::fvec3& world_end,
        bgui::vec2& screen_start,
        bgui::vec2& screen_end)
    {
        const auto transform = [&camera](const COMMONS_NS::fvec3& world) {
            return camera.projMatriz * camera.viewMatrix *
                glm::vec4(world.x, world.y, world.z, 1.f);
        };
        const auto clip_start = transform(world_start);
        const auto clip_end = transform(world_end);
        float first = 0.f;
        float last = 1.f;
        const auto clip_plane = [&first, &last](const float start_value, const float end_value) {
            if (start_value < 0.f && end_value < 0.f)
                return false;
            if (start_value >= 0.f && end_value >= 0.f)
                return true;

            const float amount = start_value / (start_value - end_value);
            if (start_value < 0.f)
                first = std::max(first, amount);
            else
                last = std::min(last, amount);
            return first <= last;
        };

        if (!clip_plane(clip_start.z + clip_start.w, clip_end.z + clip_end.w) ||
            !clip_plane(clip_start.w - clip_start.z, clip_end.w - clip_end.z))
            return false;

        const auto delta = clip_end - clip_start;
        return project_clip(camera, viewport, clip_start + delta * first, screen_start) &&
            project_clip(camera, viewport, clip_start + delta * last, screen_end);
    }
}

void editor::grid_gizmo::draw(
    const COMMONS_NS::camera& camera,
    const bgui::vec4i& viewport) const
{
    if (!enabled || spacing <= 0.f || extent <= 0.f || viewport.z <= 0 || viewport.w <= 0)
        return;

    auto& draw_list = bgui::get_draw_list();
    const int line_count = static_cast<int>(std::floor(extent / spacing));
    const bgui::vec4 color{0.55f, 0.58f, 0.62f, 0.55f};
    const bgui::vec4 axis_color{0.78f, 0.34f, 0.31f, 0.8f};

    for (int index = -line_count; index <= line_count; ++index) {
        const float offset = static_cast<float>(index) * spacing;
        bgui::vec2 start{};
        bgui::vec2 end{};

        draw_list.set_color(index == 0 ? axis_color : color);
        if (project_segment(camera, viewport, {offset, 0.f, -extent},
            {offset, 0.f, extent}, start, end) &&
            clip_segment(viewport, start, end))
            draw_list.add_line(start, end, index == 0 ? 1.5f : 1.f);

        draw_list.set_color(index == 0 ? bgui::vec4{0.32f, 0.55f, 0.82f, 0.8f} : color);
        if (project_segment(camera, viewport, {-extent, 0.f, offset},
            {extent, 0.f, offset}, start, end) &&
            clip_segment(viewport, start, end))
            draw_list.add_line(start, end, index == 0 ? 1.5f : 1.f);
    }
    draw_list.set_color({1.f, 1.f, 1.f, 1.f});
}