#pragma once
#include "../core/editor_context.hpp"
#include "asset_import.hpp"
#include "components/code.hpp"
#include "components/renderer.hpp"
#include "components/camera.hpp"

namespace editor::ui {
inline void relocate_scene_asset(const std::shared_ptr<editor_context>& context,
    const std::filesystem::path& requested_from, const std::filesystem::path& requested_to) {
    const auto root = std::filesystem::weakly_canonical(context->project_root);
    const auto from = std::filesystem::weakly_canonical(requested_from);
    const auto to = std::filesystem::weakly_canonical(requested_to);
    const auto absolute = [&](const std::string& text) {
        const std::filesystem::path path(text);
        return text.empty() ? path : std::filesystem::weakly_canonical(path.is_absolute() ? path : root / path);
    };
    std::function<void(const std::shared_ptr<commons::code>&)> update_script;
    update_script = [&](const auto& script) {
        if (!script) return;
        if (absolute(script->arquivoCompleto) == from || absolute(script->file) == from) {
            script->arquivoCompleto = to.string();
            script->file = std::filesystem::path(script->file).is_absolute() ? to.string() :
                to.lexically_relative(root).generic_string();
        }
        for (auto& property : script->properties) {
            if (property.type != "image") continue;
            auto& value = std::get<std::string>(property.value);
            if (absolute(value) == from)
                value = std::filesystem::path(value).is_absolute() ? to.string() :
                    to.lexically_relative(root).generic_string();
        }
        for (const auto& child : script->scripts) update_script(child);
    };
    context->registry->cada<commons::code>([&](auto id) { update_script(context->registry->get<commons::code>(id)); });
    context->registry->cada<commons::camera>([&](auto id) {
        const auto camera = context->registry->get<commons::camera>(id);
        const auto relocate = [&](std::string& value) {
            if (!value.empty() && absolute(value) == from)
                value = std::filesystem::path(value).is_absolute() ? to.string() : to.lexically_relative(root).generic_string();
        };
        relocate(camera->skybox_image);
        for (auto& face : camera->skybox_faces) relocate(face);
    });
    context->registry->cada<commons::renderer>([&](auto id) {
        const auto renderer = context->registry->get<commons::renderer>(id);
        auto* model = renderer->m_modelo;
        if (!model) return;
        const bool moving_model = absolute(model->get_source_path()) == from;
        if (moving_model) model->relocate_source_path(to);
        for (auto& mesh : model->meshes) {
            auto material = mesh.getMaterial();
            for (auto& [name, texture] : material.textures) {
                const auto path = absolute(texture.path);
                if (path == from) texture.path = to.string();
                else if (moving_model && path_inside(path, from.parent_path())) {
                    const auto relocated = to.parent_path() / path.lexically_relative(from.parent_path());
                    if (std::filesystem::is_regular_file(relocated)) texture.path = relocated.string();
                }
            }
            mesh.setMaterial(material);
        }
    });
}
}
