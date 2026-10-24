#pragma once

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/transform.hpp"
#include "editor_context.hpp"
#include "viewport_input_state.hpp"
#include "camera_preview_state.hpp"
#include "guizmo/transform_gizmo.hpp"
#include "guizmo/grid_gizmo.hpp"

#include <bgui.hpp>
#include <elem/image.hpp>
#include <os/os.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace editor::ui {

class guizmo_system final : public bgui::element {
    struct transform_snapshot {
        std::shared_ptr<commons::transform> transform;
        bgui::vec3 position;
        bgui::vec3 rotation;
        bgui::vec3 scale;
    };

private:
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<editor_context> m_context;
    std::shared_ptr<viewport_input_state> m_input_state;
    std::shared_ptr<camera_preview_state> m_camera_preview;
    bgui::image* m_image{nullptr};

    editor::transform_gizmo m_guizmo;
    editor::grid_gizmo m_grid_gizmo;

    bgui::vec2i m_last_mouse_position{0, 0};

    bool m_mouse_position_initialized{false};
    bool m_left_was_down{false};
    std::optional<transform_snapshot> m_drag_snapshot;

    static bool same_vector(const bgui::vec3& left, const bgui::vec3& right) {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

    void finish_transform_action() {
        if (!m_drag_snapshot)
            return;

        auto snapshot = std::move(*m_drag_snapshot);
        m_drag_snapshot.reset();
        const auto position = snapshot.transform->get_position();
        const auto rotation = snapshot.transform->get_rotation();
        const auto scale = snapshot.transform->get_scale();
        if (same_vector(snapshot.position, position) &&
            same_vector(snapshot.rotation, rotation) &&
            same_vector(snapshot.scale, scale))
            return;

        const auto transform = snapshot.transform;
        const auto old_position = snapshot.position;
        const auto old_rotation = snapshot.rotation;
        const auto old_scale = snapshot.scale;
        m_context->record_action(
            [transform, old_position, old_rotation, old_scale]() {
                transform->set_position(old_position);
                transform->set_rotation(old_rotation);
                transform->set_scale(old_scale);
            },
            [transform, position, rotation, scale]() {
                transform->set_position(position);
                transform->set_rotation(rotation);
                transform->set_scale(scale);
            }
        );
    }

    std::vector<bgui::vec4i> visible_viewport_regions(
        const bgui::vec4i& viewport
    ) const {
        std::vector<bgui::vec4i> regions{viewport};
        auto* owner = m_image ? m_image->get_parent() : nullptr;
        while (owner && owner->type != "window")
            owner = owner->get_parent();
        auto* owner_window = dynamic_cast<bgui::window*>(owner);
        auto* dock = owner_window
            ? dynamic_cast<bgui::dock*>(owner_window->get_parent())
            : nullptr;
        if (!dock)
            return regions;

        for (const auto& [layer, elements] : dock->get_elements()) {
            (void)layer;
            for (const auto& element : elements) {
                auto* other = dynamic_cast<bgui::window*>(element.get());
                if (!other || other == owner_window || !other->is_enabled())
                    continue;

                const auto cover = other->processed_rect();
                std::vector<bgui::vec4i> uncovered;
                for (const auto& region : regions) {
                    const auto overlap = bgui::intersect_rect(region, cover);
                    if (overlap.z <= 0 || overlap.w <= 0) {
                        uncovered.push_back(region);
                        continue;
                    }
                    if (overlap.y > region.y)
                        uncovered.push_back({region.x, region.y, region.z, overlap.y - region.y});
                    const int region_bottom = region.y + region.w;
                    const int overlap_bottom = overlap.y + overlap.w;
                    if (overlap_bottom < region_bottom)
                        uncovered.push_back({region.x, overlap_bottom, region.z, region_bottom - overlap_bottom});
                    if (overlap.x > region.x)
                        uncovered.push_back({region.x, overlap.y, overlap.x - region.x, overlap.w});
                    const int region_right = region.x + region.z;
                    const int overlap_right = overlap.x + overlap.z;
                    if (overlap_right < region_right)
                        uncovered.push_back({overlap_right, overlap.y, region_right - overlap_right, overlap.w});
                }
                regions = std::move(uncovered);
            }
            for (const auto& element : elements) {
                if (!element || !element->is_enabled() ||
                    !element->has_class("dock-drop-preview"))
                    continue;
                const auto cover = element->processed_rect();
                std::vector<bgui::vec4i> uncovered;
                for (const auto& region : regions) {
                    const auto overlap = bgui::intersect_rect(region, cover);
                    if (overlap.z <= 0 || overlap.w <= 0) {
                        uncovered.push_back(region);
                        continue;
                    }
                    if (overlap.y > region.y)
                        uncovered.push_back({region.x, region.y, region.z, overlap.y - region.y});
                    const int region_bottom = region.y + region.w;
                    const int overlap_bottom = overlap.y + overlap.w;
                    if (overlap_bottom < region_bottom)
                        uncovered.push_back({region.x, overlap_bottom, region.z, region_bottom - overlap_bottom});
                    if (overlap.x > region.x)
                        uncovered.push_back({region.x, overlap.y, overlap.x - region.x, overlap.w});
                    const int region_right = region.x + region.z;
                    const int overlap_right = overlap.x + overlap.z;
                    if (overlap_right < region_right)
                        uncovered.push_back({overlap_right, overlap.y, region_right - overlap_right, overlap.w});
                }
                regions = std::move(uncovered);
            }
        }
        return regions;
    }

    static bool rectangle_is_visible(
        const bgui::vec4i& rectangle,
        const std::vector<bgui::vec4i>& visible_regions
    ) {
        return std::any_of(visible_regions.begin(), visible_regions.end(),
            [&rectangle](const bgui::vec4i& region) {
                return rectangle.x >= region.x && rectangle.y >= region.y &&
                    rectangle.x + rectangle.z <= region.x + region.z &&
                    rectangle.y + rectangle.w <= region.y + region.w;
            });
    }

public:
    guizmo_system(
        std::shared_ptr<commons::camera> camera_component,
        bgui::image& image,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<viewport_input_state> input_state,
        std::shared_ptr<camera_preview_state> camera_preview
    )
        : m_camera(std::move(camera_component)),
          m_context(std::move(context)),
          m_input_state(std::move(input_state)),
          m_camera_preview(std::move(camera_preview)),
          m_image(&image) {
        // The gizmo system is a viewport-sized overlay. Keep it as a hit target
        // so the overlay consumes events while preserving the owning window in
        // the mouse-target ancestry used by viewport input tools.
        receives_input(true);
        set_flex(false);
        style.visual.visible = false;
    }

    void on_update() override {
        if (m_image) {
            const auto rect = m_image->processed_rect();
            set_final_rect(rect.x, rect.y, rect.z, rect.w);
        }
    }

    bool supports_render_cache() const override { return false; }

    void get_requires(bgui::draw_data* data) override {
        if (!m_camera || !m_image || !m_context || !m_context->registry) {
            bgui::element::get_requires(data);
            return;
        }

        const bool left_down = bgui::get_pressed(
            bgui::input_key::mouse_left
        );

        const bgui::vec2i mouse_position =
            bgui::get_mouse_position();

        if (!m_mouse_position_initialized) {
            m_last_mouse_position = mouse_position;
            m_left_was_down = left_down;
            m_mouse_position_initialized = true;
            if (m_input_state)
                m_input_state->gizmo_active = false;
        }

        const bgui::vec2i mouse_delta{
            mouse_position.x - m_last_mouse_position.x,
            mouse_position.y - m_last_mouse_position.y
        };

        const bool left_just_pressed =
            left_down && !m_left_was_down;

        const int x = m_image->processed_x();
        const int y = m_image->processed_y();
        const int width = m_image->processed_width();
        const int height = m_image->processed_height();

        if (width <= 0 || height <= 0) {
            m_last_mouse_position = mouse_position;
            m_left_was_down = left_down;

            if (m_input_state)
                m_input_state->gizmo_active = false;

            bgui::element::get_requires(data);
            return;
        }

        const bgui::vec4i viewport{
            x,
            y,
            width,
            height
        };

        const auto& root_elements = bgui::get_layout().get_elements();
        const auto overlays = root_elements.find(bgui::layer::overlay);
        bool modal_active = false;
        if (overlays != root_elements.end()) {
            for (const auto& overlay : overlays->second) {
                if (overlay && overlay->is_enabled() && overlay->type == "modal") {
                    modal_active = true;
                    break;
                }
            }
        }

        if (modal_active) {
            const bool was_dragging = m_guizmo.is_dragging();
            m_guizmo.update(
                *m_camera,
                nullptr,
                viewport,
                {},
                mouse_position,
                mouse_delta,
                false,
                false,
                false
            );
            if (was_dragging)
                finish_transform_action();

            if (m_input_state)
                m_input_state->gizmo_active = false;

            m_last_mouse_position = mouse_position;
            m_left_was_down = left_down;
            bgui::element::get_requires(data);
            return;
        }

        const bool mouse_over_view = mouse_over_viewport(m_image) &&
            mouse_position.x >= x &&
            mouse_position.x < x + width &&
            mouse_position.y >= y &&
            mouse_position.y < y + height;

        const auto visible_regions = visible_viewport_regions(viewport);

        m_grid_gizmo.draw(
            *m_camera,
            viewport
        );

        if (m_camera_preview) {
            m_camera_preview->viewport = viewport;
            m_camera_preview->hovered = false;
            m_camera_preview->main_runtime_camera.reset();
        }

        const auto editor_camera = m_context->editor_camera_entity;
        m_context->registry->cada<commons::camera>([&](const std::uint32_t id) {
            if (editor_camera && id == *editor_camera)
                return;
            const auto scene_camera = m_context->registry->get<commons::camera>(id);
            const auto scene_transform = m_context->registry->get<commons::transform>(id);
            if (!scene_camera || !scene_transform)
                return;

            // Match runtime_app's camera choice: first scene camera, excluding the editor camera.
            if (m_camera_preview && !m_camera_preview->main_runtime_camera)
                m_camera_preview->main_runtime_camera = scene_camera;

            scene_camera->getViewMatrix();
            scene_camera->obtProjectionMatrix();
            m_grid_gizmo.draw_camera_frustum(*m_camera, *scene_camera, viewport);

            const auto position = scene_transform->get_position();
            const glm::vec4 clip = m_camera->projMatriz * m_camera->viewMatrix *
                glm::vec4(position.x, position.y, position.z, 1.f);
            if (clip.w <= 0.f)
                return;
            const float ndc_x = clip.x / clip.w;
            const float ndc_y = clip.y / clip.w;
            const int icon_x = x + static_cast<int>((ndc_x * 0.5f + 0.5f) * width);
            const int icon_y = y + static_cast<int>((0.5f - ndc_y * 0.5f) * height);
            if (icon_x < x || icon_x >= x + width || icon_y < y || icon_y >= y + height)
                return;

            const bgui::vec4i camera_icon{icon_x - 9, icon_y - 7, 20, 16};
            if (rectangle_is_visible(camera_icon, visible_regions)) {
                auto& draw = data->m_draw_list;
                const auto previous_color = draw.get_color();
                draw.set_color({1.f, 0.72f, 0.16f, 1.f});
                draw.add_rect({static_cast<float>(icon_x - 6), static_cast<float>(icon_y - 4)},
                              {static_cast<float>(icon_x + 4), static_cast<float>(icon_y + 4)}, 2.f);
                draw.add_triangle({static_cast<float>(icon_x + 4), static_cast<float>(icon_y - 3)},
                                  {static_cast<float>(icon_x + 9), static_cast<float>(icon_y - 6)},
                                  {static_cast<float>(icon_x + 9), static_cast<float>(icon_y + 6)});
                draw.set_color(previous_color);
            }

            if (m_camera_preview && scene_camera == m_camera_preview->main_runtime_camera) {
                const int dx = mouse_position.x - icon_x;
                const int dy = mouse_position.y - icon_y;
                m_camera_preview->hovered = mouse_over_view && dx * dx + dy * dy <= 100;
            }
        });

        m_context->registry->cada<commons::directional_light>(
            [&](const std::uint32_t id) {
                if (editor_camera && id == *editor_camera)
                    return;
                const auto light = m_context->registry->get<commons::directional_light>(id);
                const auto transform = m_context->registry->get<commons::transform>(id);
                if (!light || !transform)
                    return;
                m_grid_gizmo.draw_directional_light(
                    *m_camera,
                    viewport,
                    transform->get_position(),
                    light->direction
                );
            }
        );

        std::shared_ptr<commons::transform> selected_transform;
        if (m_context && m_context->selected_entity) {
            selected_transform = m_context->registry->get<commons::transform>(
                *m_context->selected_entity
            );
        }

        if (selected_transform) {
            const bool was_dragging = m_guizmo.is_dragging();
            m_guizmo.update(
                *m_camera,
                selected_transform.get(),
                viewport,
                visible_regions,
                mouse_position,
                mouse_delta,
                mouse_over_view,
                left_just_pressed && mouse_over_view,
                left_down && viewport_owns_mouse(m_image)
            );
            const bool is_dragging = m_guizmo.is_dragging();
            if (!was_dragging && is_dragging && m_context->selected_entity) {
                m_drag_snapshot = transform_snapshot{
                    selected_transform,
                    selected_transform->get_position(),
                    selected_transform->get_rotation(),
                    selected_transform->get_scale()
                };
            } else if (was_dragging && !is_dragging) {
                finish_transform_action();
            }
        }

        if (m_input_state) {
            m_input_state->gizmo_active =
                selected_transform && m_guizmo.is_dragging();
        }

        m_last_mouse_position = mouse_position;
        m_left_was_down = left_down;
        bgui::element::get_requires(data);
    }
};

}
