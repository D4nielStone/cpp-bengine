#include "loaders/model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <stdexcept>
#include "debugging/debug.hpp"
#include "util/material.hpp"

using namespace COMMONS_NS;

namespace {
    std::filesystem::path safe_absolute(const std::filesystem::path& p)
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(p, ec);
        if (!ec)
            return absolute;
        return p.empty() ? std::filesystem::path{} : p.lexically_normal();
    }

    std::filesystem::path resolve_model_path(const std::string& requested)
    {
        const std::filesystem::path input(requested);
        const std::string name = input.filename().string();
        const std::vector<std::filesystem::path> candidates = {
            input,
            std::filesystem::path("assets/models") / (name + ".obj"),
            std::filesystem::path("commons/assets/models") / (name + ".obj"),
            std::filesystem::path("../commons/assets/models") / (name + ".obj"),
            std::filesystem::path(COMMONS_MODEL_ASSET_DIR) / (name + ".obj"),
            std::filesystem::path(COMMONS_MODEL_ASSET_DIR) / input.filename()
        };

        for (const auto& candidate : candidates) {
            std::error_code ec;
            const auto absolute_candidate = safe_absolute(candidate);
            if (std::filesystem::is_regular_file(absolute_candidate, ec) || std::filesystem::is_regular_file(candidate, ec))
                return absolute_candidate;
        }

        try {
            return safe_absolute(input);
        } catch (...) {
            return std::filesystem::path(COMMONS_MODEL_ASSET_DIR) / (name + ".obj");
        }
    }
}

model::model(const char* directory) {
    if (directory) {
        load_model(std::string(directory));
    }
}
model::model(const std::string& directory) {
    load_model(directory);
}
mesh& model::getMalha(size_t i) {
    if(i < meshes.size()) {
        return meshes[i];
    }
    return meshes.back();
}
void model::draw() {
    for(auto& mesh : meshes)
        mesh.draw(m_shader);
}

shader& model::getShader() {
    return m_shader;
}

void model::setShader(const shader& s) {
    m_shader = s;
}

std::string model::getDiretorio() const {
    return directory;
}

void model::load_model(const std::string& path) {
    meshes.clear();
    const auto resolved_path = resolve_model_path(path);
    const auto resolved_string = resolved_path.string();
    Assimp::Importer importer;
    auto flags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices;

    // Carrega a cena sem colapsar ainda
    const aiScene* scene = importer.ReadFile(resolved_string, flags);
    if (!scene || !scene->HasMeshes() || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        // erro de carregamento
        debugging::emit(erro, importer.GetErrorString());
        return;
    }

    // Verifica se é estático
    bool is_static = true;

    // Verifica se há animações ou bones
    if (scene->HasAnimations())
        is_static = false;

    for (unsigned int i = 0; i < scene->mNumMeshes && is_static; ++i) {
        if (scene->mMeshes[i]->HasBones()) {
            is_static = false;
            break;
        }
    }
    if (is_static && resolved_path.extension() != ".dae") {
        importer.FreeScene(); // limpa a cena anterior

        flags |= aiProcess_PreTransformVertices;

        scene = importer.ReadFile(resolved_string, flags);
        if (!scene || !scene->HasMeshes() || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            // erro de carregamento
            debugging::emit(erro, importer.GetErrorString());
            return;
        }
    }

    directory = resolved_path.parent_path().string();

    /// Processa o no principal
    process_node(scene->mRootNode, scene);
    for(auto& m_ : meshes)
        m_.load();
}

void model::process_node(aiNode* node, const aiScene* scene) {
    // processa todos as meshes se tiver
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(process_mesh(mesh, scene));
    }
    // then do the same for each of its children
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        process_node(node->mChildren[i], scene);
    }

}

mesh model::process_mesh(aiMesh* mesh, const aiScene* scene) {
    std::vector<vertex> vertices;
    std::vector<unsigned int> indices;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        vertex vertex;
        // processa coordenadas de vertex
        vector3<float> vector;
        vector.x = mesh->mVertices[i].x;
        vector.y = mesh->mVertices[i].y;
        vector.z = mesh->mVertices[i].z;
        vertex.position = vector;

        // normais
        if (mesh->HasNormals())
        {
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.normal = vector;
        }

        // uvs
        if (mesh->mTextureCoords[0])
        {
            vector2<float> vec;

            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.uvcoords = vec;
        }
        else
            vertex.uvcoords = {0.0f, 0.0f};

        vertices.push_back(vertex);
    }
    /// processa os indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        // retrieve all indices of the face and store them in the indices vector
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    // processa materiais
    aiMaterial* ai_material = scene->mMaterials[mesh->mMaterialIndex];
    commons::material bmat;

    if(hasTextura(ai_material, aiTextureType_DIFFUSE))
        bmat.set_texture("tex_albedo", load_texture(ai_material, aiTextureType_DIFFUSE));

    if(hasTextura(ai_material, aiTextureType_METALNESS))
        bmat.set_texture("tex_metallic", load_texture(ai_material, aiTextureType_METALNESS));

    if(hasTextura(ai_material, aiTextureType_DIFFUSE_ROUGHNESS))
        bmat.set_texture("tex_roughness", load_texture(ai_material, aiTextureType_DIFFUSE_ROUGHNESS));

    if(hasTextura(ai_material, aiTextureType_NORMALS))
        bmat.set_texture("tex_normal", load_texture(ai_material, aiTextureType_NORMALS));

    if(hasTextura(ai_material, aiTextureType_AMBIENT_OCCLUSION))
        bmat.set_texture("tex_ao", load_texture(ai_material, aiTextureType_AMBIENT_OCCLUSION));

    if(hasTextura(ai_material, aiTextureType_HEIGHT))
        bmat.set_texture("tex_height", load_texture(ai_material, aiTextureType_HEIGHT));

    bmat.set_uniform("use_tex_albedo", false);
    bmat.set_uniform("use_tex_metallic", false);
    bmat.set_uniform("use_tex_roughness", false);
    bmat.set_uniform("use_tex_normal", false);
    bmat.set_uniform("use_tex_ao", false);
    bmat.set_uniform("use_tex_height", false);
    for (auto& [nome, tex] : bmat.textures) {
        bmat.set_uniform(std::string("use_") + nome, true);
    }
    /// extrai color diffuse
    aiColor4D diffuse_color;
    color diffuse;
    if (AI_SUCCESS == ai_material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse_color)) {
        diffuse.r = diffuse_color.r;
        diffuse.g = diffuse_color.g;
        diffuse.b = diffuse_color.b;
        diffuse.a = diffuse_color.a;
    }
    bmat.set_uniform("material.albedo", diffuse);
    bmat.set_uniform("material.metallic", 0.0f);
    bmat.set_uniform("material.ao", 1.0f);
    bmat.set_uniform("material.roughness", 0.5f);

    COMMONS_NS::mesh m_(vertices, indices, bmat);
    return m_;
}

// Carrega Textura assimp2bubble
texture model::load_texture(aiMaterial* mat, const aiTextureType& type) {
    texture tex;
    if (hasTextura(mat, type)) {
        aiString str;
        mat->GetTexture(type, 0, &str);

        tex.path = std::filesystem::path(directory).parent_path().string() + "/" + std::filesystem::path(str.C_Str()).filename().string();
    }
    return tex;
}

bool model::hasTextura(aiMaterial* mat, const aiTextureType& type) {
    return (mat->GetTextureCount(type) > 0);
}
