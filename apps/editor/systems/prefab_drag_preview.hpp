#pragma once

#include "asset_drag_state.hpp"
#include "prefab_asset.hpp"
#include "scene_asset_drop.hpp"
#include "viewport_input_state.hpp"
#include <array>

namespace editor::ui {

class prefab_drag_preview {
public:
    using triangle = std::array<glm::vec3, 3>;

    bool load(const std::filesystem::path& path, const std::filesystem::path& project,
        std::string& error) {
        clear();
        m_path = path;
        m_project = project;
        auto isolated = std::make_shared<editor_context>(std::make_shared<commons::ecs>());
        isolated->project_root = project;
        if (!instantiate_prefab(isolated, path, error)) return false;
        const auto root = isolated->registry->get<commons::transform>(*isolated->selected_entity);
        const auto origin = root->get_position();
        const glm::vec3 offset{origin.x, origin.y, origin.z};
        isolated->registry->cada<commons::renderer, commons::transform>([&](std::uint32_t id) {
            const auto renderer = isolated->registry->get<commons::renderer>(id);
            if (!renderer->visible || !renderer->m_modelo || isolated->hidden_entities.contains(id)) return;
            const auto world = isolated->registry->get<commons::transform>(id)->get_world_model_matrix();
            for (const auto& mesh : renderer->m_modelo->meshes) {
                const auto& vertices = mesh.getVertices();
                const auto& indices = mesh.getIndices();
                for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
                    triangle face;
                    bool valid = true;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto index = indices[i + corner];
                        if (index >= vertices.size()) { valid = false; break; }
                        const auto p = vertices[index].position;
                        face[corner] = glm::vec3(world * glm::vec4(p.x, p.y, p.z, 1.f)) - offset;
                    }
                    if (valid) m_triangles.push_back(face);
                }
            }
        });
        return true;
    }

    void clear() { m_path.clear(); m_project.clear(); m_triangles.clear(); }
    const std::vector<triangle>& triangles() const { return m_triangles; }

    void draw(commons::camera& camera, bgui::image& image, const editor_context& context) {
        if (!asset_drag_active || !bgui::get_pressed(bgui::input_key::mouse_left)) {
            clear();
            return;
        }
        const auto mouse = bgui::get_mouse_position();
        if (context.is_playing || asset_drag_path.extension() != ".bobject" ||
            !viewport_contains_drop(&image, mouse)) return;
        if (m_path != asset_drag_path || m_project != context.project_root) {
            std::string error;
            load(asset_drag_path, context.project_root, error);
        }
        const auto position = scene_drop_position(camera, image, mouse);
        const auto size = camera.viewport_size();
        if (!position || size.x <= 0 || size.y <= 0) return;
        const auto rect = image.processed_rect();
        auto& draw = bgui::get_draw_list();
        const auto color = draw.get_color();
        const auto clip = draw.get_clip_rect();
        const bool clipped = draw.has_clip_rect();
        draw.set_clip_rect(clipped ? bgui::intersect_rect(clip, rect) : rect);
        for (const auto& face : m_triangles) {
            std::array<bgui::vec2, 3> screen;
            bool visible = true;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto p = face[corner] + glm::vec3(position->x, position->y, position->z);
                const auto projected = camera.world_to_screen({p.x, p.y, p.z});
                if (!projected || projected->z < 0.f || projected->z > 1.f) { visible = false; break; }
                screen[corner] = {rect.x + projected->x * rect.z / size.x,
                    rect.y + projected->y * rect.w / size.y};
            }
            if (!visible) continue;
            draw.set_color({.25f, .7f, 1.f, .16f});
            draw.add_triangle(screen[0], screen[1], screen[2]);
            draw.set_color({.35f, .8f, 1.f, .65f});
            for (std::size_t corner = 0; corner < 3; ++corner)
                draw.add_line(screen[corner], screen[(corner + 1) % 3]);
        }
        draw.set_color(color);
        if (clipped) draw.set_clip_rect(clip);
        else draw.clear_clip_rect();
    }

private:
    std::filesystem::path m_path, m_project;
    std::vector<triangle> m_triangles;
};

}
