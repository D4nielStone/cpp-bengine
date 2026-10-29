#pragma once

#include "../core/editor_context.hpp"
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include <os/asset_manager.hpp>
#include <cmath>
#include <filesystem>
#include <optional>

namespace editor::ui {

inline bgui::vec3 drop_position_on_ground(const commons::ray& ray) {
    float distance = 5.f;
    if (std::abs(ray.direction.y) > 0.0001f) {
        const float ground = -ray.origem.y / ray.direction.y;
        if (ground > 0.f && std::isfinite(ground)) distance = ground;
    }
    return ray.origem + ray.direction * distance;
}

inline std::optional<bgui::vec3> scene_drop_position(commons::camera& camera,
    const bgui::element& viewport, const bgui::vec2i& cursor) {
    const auto rect = viewport.processed_rect();
    if (rect.z <= 0 || rect.w <= 0 || cursor.x < rect.x || cursor.y < rect.y ||
        cursor.x >= rect.x + rect.z || cursor.y >= rect.y + rect.w) return std::nullopt;
    camera.getViewMatrix();
    camera.obtProjectionMatrix();
    const auto ray = camera.point_to_ray({
        float(cursor.x - rect.x) * camera.viewportFBO.x / rect.z,
        float(cursor.y - rect.y) * camera.viewportFBO.y / rect.w});
    return drop_position_on_ground(ray);
}

inline bool apply_dropped_albedo(const std::shared_ptr<editor_context>& context,
    editor_context::entity_id target, const std::filesystem::path& path, std::string& error) {
    error.clear();
    if (!context || !context->registry || context->is_playing ||
        context->locked_entities.contains(target) || context->hidden_entities.contains(target) ||
        !context->registry->is_alive(target) || context->editor_camera_entity == target) {
        error = "Solte a imagem sobre um objeto editável.";
        return false;
    }
    const auto renderer = context->registry->get<commons::renderer>(target);
    if (!renderer || !renderer->visible || !renderer->m_modelo || renderer->m_modelo->meshes.empty()) {
        error = "O objeto não possui material para receber a imagem.";
        return false;
    }
    try {
        // Decode now, upload lazily during rendering. This also works without a GPU.
        auto& texture = bgui::asset_manager::get_instance().load_texture(path.string());
        std::vector<commons::material> before, after;
        for (const auto& mesh : renderer->m_modelo->meshes) {
            before.push_back(mesh.getMaterial());
            auto next = before.back();
            next.set_texture("tex_albedo", commons::texture{0, texture.m_path});
            next.set_uniform("use_tex_albedo", true);
            after.push_back(std::move(next));
        }
        auto apply = [renderer](const std::vector<commons::material>& materials) {
            if (!renderer->m_modelo || renderer->m_modelo->meshes.size() != materials.size()) return;
            for (std::size_t i = 0; i < materials.size(); ++i)
                renderer->m_modelo->meshes[i].setMaterial(materials[i]);
        };
        apply(after);
        context->record_action([apply, before]() { apply(before); }, [apply, after]() { apply(after); });
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}
