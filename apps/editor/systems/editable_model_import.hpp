#pragma once

#include "loaders/editable_model.hpp"
#include "loaders/model.hpp"
#include "loaders/material_asset.hpp"
#include "asset_import.hpp"
#include "project_file_transaction.hpp"

#include <cstdint>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include <cctype>
#include <filesystem>
#include <string>

namespace editor::ui {

inline bool import_editable_model(
    const std::filesystem::path& path,
    commons::editable_model_data& result,
    std::string& error,
    const commons::model_import_options* settings = nullptr
) {
    error.clear();
    const auto options = settings ? *settings : commons::read_model_import_options(path);
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

inline bool is_native_model(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".bmodel";
}

inline std::filesystem::path import_native_model(const std::filesystem::path& source,
    const std::filesystem::path& project, const std::filesystem::path& destination = {},
    bool another_copy = false, const commons::model_import_options* settings = nullptr) {
    const auto imported = import_asset(source, project, true, destination, another_copy, settings);
    if (is_native_model(imported)) return imported;

    const auto options = settings ? *settings : commons::read_model_import_options(imported);
    auto native = imported;
    const auto encoded = commons::serialize_model_import_options(options);
    if (encoded != commons::serialize_model_import_options({})) {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : encoded) { hash ^= byte; hash *= 1099511628211ull; }
        native += "." + std::to_string(hash);
    }
    native += ".bmodel";
    if (std::filesystem::is_regular_file(native)) return native;

    commons::editable_model_data data;
    std::string error;
    if (!import_editable_model(imported, data, error, &options))
        throw std::runtime_error(error);
    const auto model = commons::model::prepare(imported.string(), &options);
    if (model->meshes.size() != data.meshes.size())
        throw std::runtime_error("A topologia importada não corresponde às malhas do modelo.");
    const auto materials = native.parent_path() / (native.stem().string() + ".materials");
    for (std::size_t i = 0; i < data.meshes.size(); ++i) {
        std::filesystem::path material;
        if (!commons::store_material_asset(materials, model->meshes[i].getMaterial(), material, error))
            throw std::runtime_error(error);
        data.meshes[i].material_path = material.lexically_relative(native.parent_path()).generic_string();
    }
    if (!commons::save_editable_model(native, data, error))
        throw std::runtime_error(error);
    if (settings && imported != std::filesystem::canonical(source) &&
        encoded != commons::serialize_model_import_options(commons::read_model_import_options(imported))) {
        const auto sidecar = commons::model_import_settings_path(imported);
        project_files::stage({sidecar, encoded});
        project_files::replace(sidecar);
    }
    return native;
}

}
