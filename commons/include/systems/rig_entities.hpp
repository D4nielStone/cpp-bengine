#pragma once
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "core/ecs.hpp"
#include <glm/gtx/quaternion.hpp>
#include <unordered_set>
#include <algorithm>

namespace commons {
inline glm::mat4 rig_entity_matrix(const rig_transform& pose) {
    return glm::translate(glm::mat4{1.f}, glm::vec3(pose.position.x, pose.position.y, pose.position.z)) *
        glm::toMat4(glm::quat(glm::radians(glm::vec3(pose.rotation.x, pose.rotation.y, pose.rotation.z)))) *
        glm::scale(glm::mat4{1.f}, glm::vec3(pose.scale.x, pose.scale.y, pose.scale.z));
}
inline std::shared_ptr<renderer> rig_entity_renderer(const transform& bone) {
    const auto registry = bone.reg.lock();
    if (!registry || bone.rig_joint_id.empty()) return {};
    std::unordered_set<uint32_t> visited{bone.my_object};
    auto parent = bone.parent_id;
    while (parent && visited.insert(parent).second) {
        if (auto value = registry->get<renderer>(parent); value && value->m_modelo)
            return value->m_modelo->joint_pose(bone.rig_joint_id) ? value : nullptr;
        const auto value = registry->get<transform>(parent);
        if (!value) break;
        parent = value->parent_id;
    }
    return {};
}
inline glm::mat4 rig_entity_rest_matrix(const transform& bone) {
    if (const auto owner = rig_entity_renderer(bone))
        for (const auto& joint : owner->m_modelo->rig_joints())
            if (joint.id == bone.rig_joint_id) return rig_entity_matrix(joint.rest);
    return glm::mat4{1.f};
}
inline void synchronize_rig_entities(ecs& registry) {
    std::unordered_set<model*> changed;
    registry.cada<transform>([&](uint32_t id) {
        auto bone = registry.get<transform>(id);
        const auto owner = rig_entity_renderer(*bone);
        if (!owner) return;
        const auto pose = owner->m_modelo->joint_pose(bone->rig_joint_id);
        const rig_transform edited{bone->position, bone->rotation, bone->scale};
        if (!bone->rig_pose_initialized || edited != bone->rig_last_pose) {
            if (owner->m_modelo->set_joint_pose(bone->rig_joint_id, edited)) {
                bone->rig_last_pose = edited;
                bone->rig_pose_initialized = true;
                changed.insert(owner->m_modelo);
            }
        } else if (pose) {
            bone->position = pose->position; bone->rotation = pose->rotation; bone->scale = pose->scale;
            bone->rig_last_pose = *pose;
        }
    });
    for (auto* model : changed) model->apply_pose();
}
inline std::vector<uint32_t> create_rig_entities(ecs& registry, uint32_t owner_id) {
    const auto owner = registry.get<renderer>(owner_id);
    if (!owner || !owner->m_modelo || !registry.get<transform>(owner_id)) return {};
    const auto& joints = owner->m_modelo->rig_joints();
    std::map<std::string, uint32_t> ids;
    registry.cada<transform>([&](uint32_t id) {
        const auto bone = registry.get<transform>(id);
        if (rig_entity_renderer(*bone) == owner) ids.emplace(bone->rig_joint_id, id);
    });
    std::vector<uint32_t> created;
    try {
        for (const auto& joint : joints) if (!ids.contains(joint.id)) {
            const auto entity = registry.create();
            created.push_back(entity.id);
            ids.emplace(joint.id, entity.id);
            auto bone = registry.get<transform>(entity.id);
            bone->rig_joint_id = joint.id;
            const auto pose = owner->m_modelo->joint_pose(joint.id).value_or(rig_transform{});
            bone->position = pose.position; bone->rotation = pose.rotation; bone->scale = pose.scale;
            bone->rig_last_pose = pose;
            bone->rig_pose_initialized = true;
            registry.entity_names[entity.id] = joint.name;
        }
        for (const auto& joint : joints) {
            const auto id = ids.at(joint.id);
            if (std::find(created.begin(), created.end(), id) != created.end())
                registry.get<transform>(id)->parent_id = joint.parent.empty() ? owner_id : ids.at(joint.parent);
        }
    } catch (...) {
        for (auto id : created) registry.remove(id);
        throw;
    }
    return created;
}
}
