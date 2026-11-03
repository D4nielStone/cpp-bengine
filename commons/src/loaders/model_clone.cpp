#include "loaders/model.hpp"

namespace COMMONS_NS {
std::unique_ptr<model> model::clone() const {
    auto result = std::make_unique<model>();
    result->directory = directory;
    result->source_path = source_path;
    if (m_shader.ID) result->m_shader = m_shader;
    result->m_animation_clip = m_animation_clip;
    result->m_animation_start = m_animation_start;
    if (m_pose) result->m_pose = std::make_shared<model_pose>(*m_pose);
    result->meshes.reserve(meshes.size());
    for (const auto& source : meshes) {
        auto material = source.getMaterial();
        for (auto& [name, value] : material.uniforms) {
            using type = uniform::type;
            switch (value.m_type) {
#define BENGINE_CAPTURE_UNIFORM(kind, member) case type::kind: if (value.ponteiro.member) value = uniform(*value.ponteiro.member); break
                BENGINE_CAPTURE_UNIFORM(Float_ptr, f_ptr);
                BENGINE_CAPTURE_UNIFORM(Int_ptr, i_ptr);
                BENGINE_CAPTURE_UNIFORM(Bool_ptr, b_ptr);
                BENGINE_CAPTURE_UNIFORM(Vet2_ptr, vec2_ptr);
                BENGINE_CAPTURE_UNIFORM(Vet3_ptr, vec3_ptr);
                BENGINE_CAPTURE_UNIFORM(Vet4_ptr, vec4_ptr);
                BENGINE_CAPTURE_UNIFORM(Cor_ptr, color_ptr);
                BENGINE_CAPTURE_UNIFORM(Mat4_ptr, mat4_ptr);
#undef BENGINE_CAPTURE_UNIFORM
                default: break;
            }
        }
        result->meshes.emplace_back(source.vertices(), source.indices(), material);
        std::vector<transform> instances(source.instances().size());
        for (size_t i = 0; i < instances.size(); ++i) {
            instances[i] = source.instances()[i];
            instances[i].m_usar_target = source.instances()[i].m_usar_target;
            instances[i].reg = source.instances()[i].reg;
        }
        result->meshes.back().setInstances(std::move(instances));
        result->meshes.back().setSobreposition(source.isOverlayed());
    }
    return result;
}
}
