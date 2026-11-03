#pragma once

#include "loaders/editable_model.hpp"
#include "loaders/model_import.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include <cctype>
#include <filesystem>
#include <string>

namespace editor::ui {

inline bool import_editable_model(
    const std::filesystem::path& path,
    commons::editable_model_data& result,
    std::string& error
) {
    error.clear();
    const auto options = commons::read_model_import_options(path);
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(
        path.string(),
        options.flags() & ~aiProcess_Triangulate
    );
    if (!scene || !scene->mRootNode ||
        (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->HasMeshes()) {
        error = "Não foi possível importar a topologia: " +
            std::string(importer.GetErrorString());
        return false;
    }
    if (scene->HasAnimations()) {
        error = "Modelos animados ainda não podem ser editados por topologia.";
        return false;
    }
    for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
        if (scene->mMeshes[mesh_index]->HasBones()) {
            error = "Modelos com esqueleto ainda não podem ser editados por topologia.";
            return false;
        }
    }

    commons::editable_model_data imported;
    const auto visit = [&](const auto& self, const aiNode* node,
                           const aiMatrix4x4& parent,
                           const std::string& node_path) -> void {
        if (!error.empty() || options.excluded_nodes.contains(node_path))
            return;
        const auto transform = parent * node->mTransformation;
        for (unsigned int item = 0; item < node->mNumMeshes; ++item) {
            const auto source_index = node->mMeshes[item];
            if (options.excluded_meshes.contains(source_index))
                continue;
            const auto* source = scene->mMeshes[source_index];
            commons::editable_mesh_data mesh;
            mesh.vertices.reserve(source->mNumVertices);
            for (unsigned int vertex_index = 0;
                 vertex_index < source->mNumVertices;
                 ++vertex_index) {
                const auto position =
                    transform * source->mVertices[vertex_index] *
                    static_cast<float>(options.scale);
                bgui::vec2 uv{};
                if (source->mTextureCoords[0]) {
                    uv.x = source->mTextureCoords[0][vertex_index].x;
                    uv.y = source->mTextureCoords[0][vertex_index].y;
                    if (options.flip_uvs)
                        uv.y = 1.f - uv.y;
                }
                mesh.vertices.emplace_back(
                    bgui::vec3{position.x, position.y, position.z},
                    bgui::vec3{},
                    uv
                );
            }
            mesh.faces.reserve(source->mNumFaces);
            for (unsigned int face_index = 0;
                 face_index < source->mNumFaces;
                 ++face_index) {
                const auto& source_face = source->mFaces[face_index];
                if (source_face.mNumIndices < 3) {
                    error = "O modelo contém uma face inválida.";
                    return;
                }
                auto& face = mesh.faces.emplace_back();
                face.reserve(source_face.mNumIndices);
                for (unsigned int index = 0;
                     index < source_face.mNumIndices;
                     ++index) {
                    if (source_face.mIndices[index] >= mesh.vertices.size()) {
                        error = "A malha contém um índice de vértice inválido.";
                        return;
                    }
                    face.push_back(source_face.mIndices[index]);
                }
            }
            mesh.recalculate_normals();
            if (!mesh.validate(error))
                return;
            imported.meshes.push_back(std::move(mesh));
        }
        for (unsigned int child = 0; child < node->mNumChildren; ++child)
            self(self, node->mChildren[child], transform,
                node_path + "/" + std::to_string(child));
    };
    visit(visit, scene->mRootNode, aiMatrix4x4{}, "0");
    if (!error.empty())
        return false;
    if (!imported.validate(error))
        return false;
    result = std::move(imported);
    return true;
}

}
