#include "system/editor_system.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "system/editor_ui_elements.hpp"

#include <bgui.hpp>
#include <elem/window.hpp>
#include <os/os.hpp>

namespace {
    bgui::vec4i intersection(const bgui::vec4i& first, const bgui::vec4i& second) {
        const int left = std::max(first.x, second.x);
        const int top = std::max(first.y, second.y);
        const int right = std::min(first.x + first.z, second.x + second.z);
        const int bottom = std::min(first.y + first.w, second.y + second.w);
        return {left, top, std::max(0, right - left), std::max(0, bottom - top)};
    }

    bool contains(const bgui::vec4i& rectangle, const bgui::vec2i& point) {
        return point.x >= rectangle.x && point.x < rectangle.x + rectangle.z &&
            point.y >= rectangle.y && point.y < rectangle.y + rectangle.w;
    }

    std::vector<bgui::vec4i> floating_window_rects(
        const bgui::vec4i& viewport,
        const std::string& exclude_class)
    {
        std::vector<bgui::vec4i> result;
        std::function<void(bgui::layout&)> visit = [&](bgui::layout& layout) {
            for (auto& [layer, elements] : layout.get_elements()) {
                (void)layer;
                for (auto& element : elements) {
                    if (!element || !element->is_enabled())
                        continue;
                    if (auto* window = dynamic_cast<bgui::window*>(element.get())) {
                        if (!window->has_class(exclude_class) && window->is_floating()) {
                            const auto clipped = intersection(window->processed_rect(), viewport);
                            if (clipped.z > 0 && clipped.w > 0)
                                result.push_back(clipped);
                        }
                    }
                    if (auto* child_layout = element->as_layout()) {
                        visit(*child_layout);
                    }
                }
            }
        };
        visit(bgui::get_layout());
        return result;
    }

    std::vector<bgui::vec4i> unobscured_regions(
        const bgui::vec4i& viewport,
        const std::vector<bgui::vec4i>& occluders)
    {
        std::vector<bgui::vec4i> regions{viewport};
        for (const auto& occluder : occluders) {
            std::vector<bgui::vec4i> remaining;
            for (const auto& region : regions) {
                const auto cut = intersection(region, occluder);
                if (cut.z == 0 || cut.w == 0) {
                    remaining.push_back(region);
                    continue;
                }

                const int region_right = region.x + region.z;
                const int region_bottom = region.y + region.w;
                if (cut.y > region.y)
                    remaining.push_back({region.x, region.y, region.z, cut.y - region.y});
                if (cut.y + cut.w < region_bottom)
                    remaining.push_back({
                        region.x, cut.y + cut.w, region.z, region_bottom - cut.y - cut.w
                    });
                if (cut.x > region.x)
                    remaining.push_back({region.x, cut.y, cut.x - region.x, cut.w});
                if (cut.x + cut.z < region_right)
                    remaining.push_back({
                        cut.x + cut.z, cut.y, region_right - cut.x - cut.z, cut.w
                    });
            }
            regions = std::move(remaining);
            if (regions.empty())
                break;
        }
        return regions;
    }
}

void editor::editor_system::setup_scene_view_panel(bgui::window& window) {
    window.add_class(ui_elements::scene_view_window);
    auto& window_context = window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    window_context.add_class(ui_elements::scene_view_context);
    window_context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    window_context.style.layout.padding = bgui::vec4i{0};
    window_context.style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::center,
        bgui::alignment::center
    };
    auto& framebuffer_image = window_context.add_persistent<bgui::image>();
    framebuffer_image.add_class(ui_elements::framebuffer_image);
    framebuffer_image.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    framebuffer_image.recives_input(true);
}

void editor::editor_system::update_scene_view_panel(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    auto& framebuffer_image = ui_elements::require<bgui::image>(ui_elements::framebuffer_image);
    auto& window_context = ui_elements::require<bgui::linear>(ui_elements::scene_view_context);
    const auto camera_component = m_camera.lock();
    if (!camera_component)
        return;
    if (!camera_component->flag_fb)
        camera_component->createFB();
    if (m_framebuffer_texture != camera_component->framebuffer_texture()) {
        m_framebuffer_texture = camera_component->framebuffer_texture();
        framebuffer_image.set_external_texture(
            m_framebuffer_texture,
            bgui::vec2{
                static_cast<float>(camera_component->viewportFBO.x),
                static_cast<float>(camera_component->viewportFBO.y)
            }
        );
    }

    const auto mouse_position = bgui::get_mouse_position();
    const bgui::vec4i viewport{
        framebuffer_image.processed_x(),
        framebuffer_image.processed_y(),
        framebuffer_image.processed_width(),
        framebuffer_image.processed_height()
    };
    const auto occluders = floating_window_rects(viewport, ui_elements::scene_view_window);
    const auto visible_regions = unobscured_regions(viewport, occluders);
    const bool mouse_over_floating_window = std::any_of(
        occluders.begin(), occluders.end(),
        [&mouse_position](const bgui::vec4i& rectangle) {
            return contains(rectangle, mouse_position);
        });
    const bool mouse_over_view = !mouse_over_floating_window &&
        bgui::get_mouse_target() == &framebuffer_image;
    const float scroll_delta = bgui::get_context().m_scroll_delta_y;
    bgui::get_context().m_scroll_delta_y = 0.f;
    const auto& inputs = bgui::get_context().m_input_map;
    const auto input_down = [&inputs](const bgui::input_key key) {
        const auto found = inputs.find(key);
        return found != inputs.end() &&
            (found->second == bgui::input_action::press || found->second == bgui::input_action::repeat);
    };
    const bool left_down = input_down(bgui::input_key::mouse_left);
    const bool right_down = input_down(bgui::input_key::mouse_right);
    const bool left_just_pressed = left_down && !m_left_mouse_was_down;
    const bool right_just_pressed = right_down && !m_right_mouse_was_down;
    const bgui::vec2i gizmo_mouse_delta{
        mouse_position.x - m_last_mouse_x,
        mouse_position.y - m_last_mouse_y
    };

    m_grid_gizmo.draw(*camera_component, viewport);

    const auto selected_transform = m_selected_entity != 0
        ? registry->get<COMMONS_NS::transform>(m_selected_entity)
        : nullptr;
    const bool gizmo_consumed_mouse = m_transform_gizmo.update(
        *camera_component,
        selected_transform.get(),
        viewport,
        visible_regions,
        mouse_position,
        gizmo_mouse_delta,
        mouse_over_view,
        left_just_pressed,
        left_down
    );
    if (left_just_pressed && mouse_over_view && !gizmo_consumed_mouse)
        m_left_view_active = true;
    if (right_just_pressed && mouse_over_view)
        m_right_move_active = true;
    if (!left_down)
        m_left_view_active = false;
    if (!right_down)
        m_right_move_active = false;
    m_left_mouse_was_down = left_down;
    m_right_mouse_was_down = right_down;

    const float current_time = bgui::get_time();
    const float delta_time = m_last_update_time > 0.f
        ? std::min(current_time - m_last_update_time, 0.1f)
        : 0.f;
    m_last_update_time = current_time;
    const int mouse_delta_x = mouse_position.x - m_last_mouse_x;
    const int mouse_delta_y = mouse_position.y - m_last_mouse_y;
    m_last_mouse_x = mouse_position.x;
    m_last_mouse_y = mouse_position.y;

    auto camera_transform = registry->get<COMMONS_NS::transform>(camera_component->my_object);
    if (camera_transform && mouse_over_view && scroll_delta != 0.f) {
        const auto rotation = camera_transform->get_rotation();
        const float pitch = glm::radians(rotation.x);
        const float yaw = glm::radians(rotation.y);
        const float distance = scroll_delta * m_config.camera_zoom_sensitivity();
        camera_transform->move({
            std::cos(yaw) * std::cos(pitch) * distance,
            std::sin(pitch) * distance,
            std::sin(yaw) * std::cos(pitch) * distance
        });
    }
    if (camera_transform && delta_time > 0.f) {
        auto rotation = camera_transform->get_rotation();
        if (m_left_view_active) {
            const int view_delta_x = left_just_pressed ? 0 : mouse_delta_x;
            const int view_delta_y = left_just_pressed ? 0 : mouse_delta_y;
            rotation.x = std::clamp(rotation.x - view_delta_y * m_config.camera_look_sensitivity(), -89.f, 89.f);
            rotation.y += view_delta_x * m_config.camera_look_sensitivity();
            camera_transform->set_rotation(rotation);
        }

        if (m_right_move_active) {
            const float pitch = glm::radians(rotation.x);
            const float yaw = glm::radians(rotation.y);
            const COMMONS_NS::fvec3 forward{
                std::cos(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::sin(yaw) * std::cos(pitch)
            };
            const COMMONS_NS::fvec3 right{-std::sin(yaw), 0.f, std::cos(yaw)};
            float move_x = 0.f;
            float move_y = 0.f;
            float move_z = 0.f;
            if (input_down(bgui::input_key::w)) { move_x += forward.x; move_y += forward.y; move_z += forward.z; }
            if (input_down(bgui::input_key::s)) { move_x -= forward.x; move_y -= forward.y; move_z -= forward.z; }
            if (input_down(bgui::input_key::d)) { move_x += right.x; move_z += right.z; }
            if (input_down(bgui::input_key::a)) { move_x -= right.x; move_z -= right.z; }
            if (input_down(bgui::input_key::e)) move_y += 1.f;
            if (input_down(bgui::input_key::q)) move_y -= 1.f;

            const float move_length = std::sqrt(move_x * move_x + move_y * move_y + move_z * move_z);
            auto position = camera_transform->get_position();
            if (move_length > 0.f) {
                const float speed = m_config.camera_move_speed() * (input_down(bgui::input_key::left_shift) ||
                    input_down(bgui::input_key::right_shift) ? 3.f : 1.f);
                const float distance = speed * delta_time / move_length;
                position.x += move_x * distance;
                position.y += move_y * distance;
                position.z += move_z * distance;
            }
            if (!right_just_pressed) {
                position.x -= right.x * mouse_delta_x * 0.02f;
                position.y += mouse_delta_y * 0.02f;
                position.z -= right.z * mouse_delta_x * 0.02f;
            }
            camera_transform->set_position(position);
        }
    }

    const auto source_size = camera_component->viewportFBO;
    const auto padding = window_context.computed_style.layout.padding;
    const int available_width = std::max(0, window_context.processed_width() - padding.x - padding.z);
    const int available_height = std::max(0, window_context.processed_height() - padding.y - padding.w);
    if (source_size.x <= 0 || source_size.y <= 0 || available_width <= 0 || available_height <= 0)
        return;

    const float source_aspect = static_cast<float>(source_size.x) / source_size.y;
    const float target_aspect = static_cast<float>(available_width) / available_height;
    bgui::vec2 uv_min{0.f, 0.f};
    bgui::vec2 uv_max{1.f, 1.f};
    if (source_aspect > target_aspect) {
        const float horizontal_crop = (1.f - target_aspect / source_aspect) * 0.5f;
        uv_min[0] = horizontal_crop;
        uv_max[0] = 1.f - horizontal_crop;
    } else if (source_aspect < target_aspect) {
        const float vertical_crop = (1.f - source_aspect / target_aspect) * 0.5f;
        uv_min[1] = vertical_crop;
        uv_max[1] = 1.f - vertical_crop;
    }
    framebuffer_image.set_uv_region(uv_min, uv_max);
}