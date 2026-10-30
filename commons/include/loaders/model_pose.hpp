#pragma once
#include "model_import.hpp"
#include <assimp/quaternion.h>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

namespace commons {
// CPU pose evaluation keeps the existing renderer and vertex layout unchanged.
class model_pose {
public:
    std::shared_ptr<Assimp::Importer> importer = std::make_shared<Assimp::Importer>();
    const aiScene* scene = nullptr;
    model_import_options options;
    struct instance { unsigned mesh; const aiNode* node; };
    std::vector<instance> instances;
    std::map<const aiNode*, aiMatrix4x4> globals;
    std::map<std::string, const aiNode*> named_nodes;

    void collect(const aiNode* node, const std::string& path = "0", bool enabled = true) {
        named_nodes.emplace(node->mName.C_Str(), node);
        enabled = enabled && !options.excluded_nodes.contains(path);
        for (unsigned i = 0; enabled && i < node->mNumMeshes; ++i)
            if (!options.excluded_meshes.contains(node->mMeshes[i])) instances.push_back({node->mMeshes[i], node});
        for (unsigned i = 0; i < node->mNumChildren; ++i) collect(node->mChildren[i], path + "/" + std::to_string(i), enabled);
    }
    template<class Key, class Value, class Interpolate>
    static Value sample(const Key* keys, unsigned count, double tick, Value fallback, Interpolate interpolate) {
        if (!count) return fallback;
        if (tick <= keys[0].mTime || count == 1) return keys[0].mValue;
        for (unsigned i = 1; i < count; ++i) if (tick < keys[i].mTime) {
            const auto span = keys[i].mTime - keys[i-1].mTime;
            return interpolate(keys[i-1].mValue, keys[i].mValue, span > 0 ? float((tick - keys[i-1].mTime) / span) : 0.f);
        }
        return keys[count-1].mValue;
    }
    void evaluate(double seconds, unsigned clip = 0) {
        globals.clear();
        const aiAnimation* animation = nullptr;
        if (options.animations && clip < scene->mNumAnimations && !options.excluded_animations.contains(clip))
            animation = scene->mAnimations[clip];
        double tick = 0;
        if (animation && animation->mDuration > 0) tick = std::fmod(std::max(0.0, seconds) *
            (animation->mTicksPerSecond > 0 ? animation->mTicksPerSecond : 25.0), animation->mDuration);
        std::set<std::string> disabled_bones;
        if (!options.skeleton) for (unsigned m = 0; m < scene->mNumMeshes; ++m)
            for (unsigned b = 0; b < scene->mMeshes[m]->mNumBones; ++b)
                disabled_bones.insert(scene->mMeshes[m]->mBones[b]->mName.C_Str());
        std::map<std::string, const aiNodeAnim*> channels;
        if (animation) for (unsigned i = 0; i < animation->mNumChannels; ++i)
            if (!disabled_bones.contains(animation->mChannels[i]->mNodeName.C_Str()))
                channels.emplace(animation->mChannels[i]->mNodeName.C_Str(), animation->mChannels[i]);
        const auto visit = [&](const auto& self, const aiNode* node, const aiMatrix4x4& parent) -> void {
            auto local = node->mTransformation;
            if (auto it = channels.find(node->mName.C_Str()); it != channels.end()) {
                const auto* channel = it->second;
                aiVector3D scale, position; aiQuaternion rotation;
                local.Decompose(scale, rotation, position);
                const auto lerp = [](const aiVector3D& a, const aiVector3D& b, float t) { return a + (b-a)*t; };
                position = sample(channel->mPositionKeys, channel->mNumPositionKeys, tick, position, lerp);
                scale = sample(channel->mScalingKeys, channel->mNumScalingKeys, tick, scale, lerp);
                rotation = sample(channel->mRotationKeys, channel->mNumRotationKeys, tick, rotation,
                    [](const aiQuaternion& a, const aiQuaternion& b, float t) {
                        aiQuaternion q; aiQuaternion::Interpolate(q, a, b, t); return q.Normalize();
                    });
                local = aiMatrix4x4(scale, rotation, position);
            }
            const auto global = parent * local;
            globals[node] = global;
            for (unsigned i = 0; i < node->mNumChildren; ++i) self(self, node->mChildren[i], global);
        };
        visit(visit, scene->mRootNode, aiMatrix4x4{});
    }
    struct geometry { std::vector<aiVector3D> positions, normals; };
    geometry vertices(const instance& item) const {
        const auto* mesh = scene->mMeshes[item.mesh];
        geometry result;
        result.positions.resize(mesh->mNumVertices); result.normals.resize(mesh->mNumVertices);
        std::vector<float> weights(mesh->mNumVertices, 0.f);
        const auto transform_normal = [](const aiMatrix4x4& transform, aiVector3D normal) {
            aiMatrix3x3 matrix(transform); matrix.Inverse().Transpose(); return matrix * normal;
        };
        if (options.skins && options.skeleton) for (unsigned i = 0; i < mesh->mNumBones; ++i) {
            const auto* bone = mesh->mBones[i];
            auto node = named_nodes.find(bone->mName.C_Str());
            if (node == named_nodes.end()) continue;
            const auto matrix = globals.at(node->second) * bone->mOffsetMatrix;
            for (unsigned j = 0; j < bone->mNumWeights; ++j) {
                const auto weight = bone->mWeights[j];
                if (weight.mVertexId >= mesh->mNumVertices || weight.mWeight <= 0) continue;
                const auto v = weight.mVertexId;
                result.positions[v] += (matrix * mesh->mVertices[v]) * weight.mWeight;
                if (mesh->HasNormals()) result.normals[v] += transform_normal(matrix, mesh->mNormals[v]) * weight.mWeight;
                weights[v] += weight.mWeight;
            }
        }
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            if (weights[v] > 0) {
                result.positions[v] /= weights[v]; result.normals[v] /= weights[v];
            } else {
                result.positions[v] = globals.at(item.node) * mesh->mVertices[v];
                if (mesh->HasNormals()) result.normals[v] = transform_normal(globals.at(item.node), mesh->mNormals[v]);
            }
            result.positions[v] *= float(options.scale);
            if (result.normals[v].SquareLength() > 0) result.normals[v].Normalize();
        }
        return result;
    }
};
}
