#include "loaders/model_pose.hpp"
#include "loaders/editable_model.hpp"
#include "loaders/model.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <limits>
#include <stdexcept>

namespace commons {
aiMatrix4x4 model_pose::matrix(const rig_transform& value, float units) {
    const auto position = glm::vec3(value.position.x, value.position.y, value.position.z) / units;
    const auto rotation = glm::quat(glm::radians(glm::vec3(value.rotation.x, value.rotation.y, value.rotation.z)));
    const auto scale = glm::vec3(value.scale.x, value.scale.y, value.scale.z);
    const auto m = glm::translate(glm::mat4{1.f}, position) * glm::toMat4(rotation) * glm::scale(glm::mat4{1.f}, scale);
    return {m[0][0],m[1][0],m[2][0],m[3][0], m[0][1],m[1][1],m[2][1],m[3][1],
        m[0][2],m[1][2],m[2][2],m[3][2], m[0][3],m[1][3],m[2][3],m[3][3]};
}
void model_pose::collect_joint(const aiNode* node, const std::string& path) {
    std::string id = "node-" + path;
    std::replace(id.begin(), id.end(), '/', '-');
    auto parent = id.substr(0, id.find_last_of('-'));
    if (!node->mParent) parent.clear();
    aiVector3D position, scale;
    aiQuaternion rotation;
    node->mTransformation.Decompose(scale, rotation, position);
    const auto angles = glm::degrees(glm::eulerAngles(glm::quat{rotation.w,rotation.x,rotation.y,rotation.z}));
    joints.push_back({id, node->mName.length ? node->mName.C_Str() : id, parent,
        {{position.x * float(options.scale),position.y * float(options.scale),position.z * float(options.scale)},
         {angles.x,angles.y,angles.z}, {scale.x,scale.y,scale.z}}});
    identified_nodes.emplace(id, node);
}
std::shared_ptr<model_pose> model_pose::from_editable(const editable_model_data& data) {
    std::string error;
    if (!data.validate(error)) throw std::invalid_argument(error);
    if (data.rig.joints.empty()) return {};
    auto pose = std::make_shared<model_pose>();
    pose->authored_scene = std::make_shared<aiScene>();
    pose->scene = pose->authored_scene.get();
    pose->options.animations = false;
    auto& scene = *pose->authored_scene;
    scene.mRootNode = new aiNode("__bengine_rig__");
    std::map<std::string, aiNode*> nodes;
    std::vector<std::unique_ptr<aiNode>> owned_nodes;
    for (const auto& joint : data.rig.joints) {
        auto node = std::make_unique<aiNode>(("rig-" + joint.id).c_str());
        node->mTransformation = matrix(joint.rest);
        nodes.emplace(joint.id, node.get());
        owned_nodes.push_back(std::move(node));
    }
    // Allocate all adjacency arrays before changing ownership edges.
    std::map<std::string, std::vector<aiNode*>> hierarchy;
    for (const auto& joint : data.rig.joints) hierarchy[joint.parent].push_back(nodes.at(joint.id));
    std::map<std::string, std::unique_ptr<aiNode*[]>> arrays;
    for (const auto& [parent, list] : hierarchy) {
        auto array = std::make_unique<aiNode*[]>(list.size());
        std::copy(list.begin(), list.end(), array.get());
        arrays.emplace(parent, std::move(array));
    }
    for (const auto& [parent, list] : hierarchy) {
        auto* owner = parent.empty() ? scene.mRootNode : nodes.at(parent);
        owner->mNumChildren = unsigned(list.size());
        owner->mChildren = arrays.at(parent).release();
        for (auto* node : list) node->mParent = owner;
    }
    for (auto& node : owned_nodes) node.release();
    scene.mNumMeshes = unsigned(data.meshes.size());
    scene.mMeshes = new aiMesh*[scene.mNumMeshes]{};
    scene.mRootNode->mNumMeshes = scene.mNumMeshes;
    scene.mRootNode->mMeshes = new unsigned[scene.mNumMeshes];
    for (unsigned i = 0; i < scene.mNumMeshes; ++i) {
        scene.mRootNode->mMeshes[i] = i;
        auto* mesh = scene.mMeshes[i] = new aiMesh();
        const auto& source = data.meshes[i];
        mesh->mNumVertices = unsigned(source.vertices.size());
        mesh->mVertices = new aiVector3D[mesh->mNumVertices];
        mesh->mNormals = new aiVector3D[mesh->mNumVertices];
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            const auto& vertex = source.vertices[v];
            mesh->mVertices[v] = {vertex.position.x,vertex.position.y,vertex.position.z};
            mesh->mNormals[v] = {vertex.normal.x,vertex.normal.y,vertex.normal.z};
        }
    }
    pose->collect(scene.mRootNode);
    pose->joints = data.rig.joints;
    pose->identified_nodes.clear();
    for (const auto& [id, node] : nodes) pose->identified_nodes.emplace(id, node);
    pose->evaluate(0);
    for (unsigned i = 0; i < scene.mNumMeshes; ++i) {
        auto& mesh = *scene.mMeshes[i];
        std::map<std::string, std::vector<aiVertexWeight>> weights;
        const auto& skin = data.meshes[i].skin;
        for (unsigned v = 0; v < skin.size(); ++v)
            for (const auto& weight : skin[v]) weights[weight.joint].emplace_back(v, weight.weight);
        mesh.mNumBones = unsigned(weights.size());
        mesh.mBones = new aiBone*[mesh.mNumBones]{};
        unsigned b = 0;
        for (const auto& [id, values] : weights) {
            auto* bone = mesh.mBones[b++] = new aiBone();
            bone->mName = nodes.at(id)->mName;
            bone->mOffsetMatrix = pose->globals.at(nodes.at(id));
            bone->mOffsetMatrix.Inverse();
            bone->mNumWeights = unsigned(values.size());
            bone->mWeights = new aiVertexWeight[values.size()];
            std::copy(values.begin(), values.end(), bone->mWeights);
        }
    }
    return pose;
}

const std::vector<rig_joint>& model::rig_joints() const {
    static const std::vector<rig_joint> empty;
    return m_pose ? m_pose->joints : empty;
}
std::optional<rig_transform> model::joint_pose(const std::string& id) const {
    if (!m_pose) return {};
    const auto node = m_pose->identified_nodes.find(id);
    if (node == m_pose->identified_nodes.end()) return {};
    const auto value = m_pose->overrides.find(node->second);
    return value == m_pose->overrides.end() ? rig_transform{} : value->second;
}
bool model::set_joint_pose(const std::string& id, const rig_transform& value) {
    if (!m_pose || !value.valid()) return false;
    const auto node = m_pose->identified_nodes.find(id);
    if (node == m_pose->identified_nodes.end()) return false;
    if (joint_pose(id) == value && m_manual_pose) return true;
    m_pose->overrides[node->second] = value;
    m_manual_pose = m_pose_dirty = true;
    ++m_pose_revision;
    return true;
}
void model::copy_joint_poses(const model& source) {
    if (!m_pose || !source.m_pose) return;
    m_pose->overrides.clear();
    for (const auto& joint : m_pose->joints)
        if (auto value = source.joint_pose(joint.id); value && *value != rig_transform{})
            m_pose->overrides[m_pose->identified_nodes.at(joint.id)] = *value;
    m_manual_pose = source.m_manual_pose; m_pose_dirty = true;
    ++m_pose_revision;
}
bool model::set_rig(const editable_model_data& data, std::string& error) {
    if (!data.validate(error)) return false;
    if (data.meshes.size() != meshes.size()) {
        error = "A geometria mudou; recarregue o modelo antes de editar o rig."; return false;
    }
    for (std::size_t i = 0; i < meshes.size(); ++i)
        if (data.meshes[i].vertices.size() != meshes[i].vertices().size()) {
            error = "A geometria mudou; recarregue o modelo antes de editar o rig."; return false;
        }
    auto previous = std::move(m_pose);
    try { m_pose = model_pose::from_editable(data); }
    catch (const std::exception& e) { m_pose = std::move(previous); error = e.what(); return false; }
    if (m_pose && previous)
        for (const auto& [id, node] : m_pose->identified_nodes) {
            const auto old = previous->identified_nodes.find(id);
            if (old != previous->identified_nodes.end())
                if (const auto pose = previous->overrides.find(old->second); pose != previous->overrides.end())
                    m_pose->overrides[node] = pose->second;
        }
    if (!m_pose) for (std::size_t i = 0; i < meshes.size(); ++i) meshes[i].set_vertices(data.meshes[i].vertices);
    m_pose_dirty = true; ++m_pose_revision;
    apply_pose();
    return true;
}
void model::reset_pose() {
    if (!m_pose) return;
    m_pose->overrides.clear(); m_manual_pose = m_pose_dirty = true; ++m_pose_revision;
}
void model::apply_pose() { if (m_pose_dirty) update_animation(m_animation_seconds); }
}
