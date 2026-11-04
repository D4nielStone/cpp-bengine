#pragma once

#include <components/renderer.hpp>
#include <core/ecs.hpp>
#include <algorithm>
#include <vector>

namespace editor {

inline std::size_t unify_scene_materials(commons::ecs& registry) {
    std::vector<const commons::mesh*> unique_materials;
    std::size_t unified = 0;
    registry.cada<commons::renderer>([&](const auto entity) {
        const auto renderer = registry.get<commons::renderer>(entity);
        if (!renderer || !renderer->m_modelo) return;
        for (auto& mesh : renderer->m_modelo->meshes) {
            const auto& material = mesh.material_configuration();
            const auto match = std::find_if(unique_materials.begin(), unique_materials.end(), [&](const auto* candidate) {
                return material.same_configuration(candidate->material_configuration());
            });
            if (match == unique_materials.end()) {
                unique_materials.push_back(&mesh);
            } else if (&material != &(*match)->material_configuration()) {
                mesh.share_material_from(**match);
                ++unified;
            }
        }
    });
    return unified;
}

}
