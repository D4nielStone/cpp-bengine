#include "loaders/model.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include "debugging/debug.hpp"
#include "util/material.hpp"
#include <os/asset_manager.hpp>
#include <fstream>


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
    if (m_pose && m_pose->options.animations && m_pose->scene->HasAnimations())
        update_animation(std::chrono::duration<double>(std::chrono::steady_clock::now() - m_animation_start).count());
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
    model replacement;
    replacement.load_model_data(path);
    if (replacement.meshes.empty()) return;
    meshes.swap(replacement.meshes);
    m_pose.swap(replacement.m_pose);
    directory.swap(replacement.directory);
    source_path.swap(replacement.source_path);
    m_animation_clip = replacement.m_animation_clip;
    m_animation_start = replacement.m_animation_start;
}

void model::load_model_data(const std::string& path) {
    meshes.clear();
    m_pose.reset();
    const auto resolved_path = resolve_model_path(path);
    source_path = resolved_path.string();
    const auto resolved_string = resolved_path.string();
    auto pose = std::make_shared<model_pose>();
    pose->options = read_model_import_options(resolved_path);
    pose->scene = pose->importer->ReadFile(resolved_string, pose->options.flags());
    if (!pose->scene || !pose->scene->mRootNode || !pose->scene->HasMeshes() ||
        (pose->scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        debugging::emit(erro, pose->importer->GetErrorString());
        return;
    }
    validate_model_selection(*pose->scene, pose->options);
    pose->collect(pose->scene->mRootNode);
    directory = resolved_path.parent_path().string();
    m_pose = std::move(pose);
    m_animation_clip = 0;
    while (m_animation_clip < m_pose->scene->mNumAnimations &&
        m_pose->options.excluded_animations.contains(m_animation_clip)) ++m_animation_clip;
    for (const auto& instance : m_pose->instances)
        meshes.push_back(process_mesh(m_pose->scene->mMeshes[instance.mesh], m_pose->scene));
    update_animation(0);
    for (auto& mesh : meshes) mesh.load();
    m_animation_start = std::chrono::steady_clock::now();
}

void model::set_animation(unsigned clip) {
    if (!m_pose || clip >= m_pose->scene->mNumAnimations || m_pose->options.excluded_animations.contains(clip))
        throw std::out_of_range("Animation is not imported");
    m_animation_clip = clip;
    m_animation_start = std::chrono::steady_clock::now();
    update_animation(0);
}

void model::update_animation(double seconds) {
    if (!m_pose) return;
    m_pose->evaluate(seconds, m_animation_clip);
    for (size_t i = 0; i < std::min(meshes.size(), m_pose->instances.size()); ++i) {
        auto vertices = meshes[i].getVertices();
        const auto geometry = m_pose->vertices(m_pose->instances[i]);
        for (size_t v = 0; v < std::min(vertices.size(), geometry.positions.size()); ++v) {
            const auto p = geometry.positions[v], n = geometry.normals[v];
            vertices[v].position = {p.x, p.y, p.z};
            vertices[v].normal = {n.x, n.y, n.z};
        }
        meshes[i].set_vertices(vertices);
    }
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
        bgui::vec3 vector;
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
            bgui::vec2 vec;

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

    if(hasTextura(ai_material, aiTextureType_BASE_COLOR))
        bmat.set_texture("tex_albedo", load_texture(ai_material, aiTextureType_BASE_COLOR));
    else if(hasTextura(ai_material, aiTextureType_DIFFUSE))
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

        auto reference = std::string(str.C_Str());
        std::replace(reference.begin(), reference.end(), '\\', '/');
        if (m_pose) if (const auto* embedded = m_pose->scene->GetEmbeddedTexture(reference.c_str())) {
            tex.embedded = std::make_shared<bgui::texture>();
            tex.path = source_path + "#" + reference;
            if (embedded->mHeight) {
                tex.embedded->m_size = {float(embedded->mWidth), float(embedded->mHeight)};
                tex.embedded->m_buffer.reserve(size_t(embedded->mWidth) * embedded->mHeight * 4);
                for (size_t i = 0; i < size_t(embedded->mWidth) * embedded->mHeight; ++i) {
                    const auto pixel = embedded->pcData[i];
                    for (auto c : {pixel.r, pixel.g, pixel.b, pixel.a}) tex.embedded->m_buffer.push_back(c);
                }
                tex.embedded->m_has_alpha = true;
            } else {
                // Reuse BGUI's platform image decoder; it owns the decoded pixels.
                static unsigned sequence = 0;
                auto temporary = std::filesystem::temp_directory_path() / ("bengine-embedded-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
                if (!std::filesystem::create_directory(temporary)) throw std::runtime_error("Cannot stage embedded image");
                struct cleanup { std::filesystem::path p; ~cleanup() { std::error_code ec; std::filesystem::remove_all(p, ec); } } cleanup{temporary};
                const auto image = temporary / "image";
                std::ofstream output(image, std::ios::binary);
                output.write(reinterpret_cast<const char*>(embedded->pcData), embedded->mWidth);
                output.close();
                if (!output) throw std::runtime_error("Cannot decode embedded image");
                *tex.embedded = bgui::asset_manager::get_instance().load_texture(image.string());
            }
            tex.embedded->m_path = tex.path;
            return tex;
        }
        tex.path = (std::filesystem::path(directory) / reference).lexically_normal().string();
    }
    return tex;
}

bool model::hasTextura(aiMaterial* mat, const aiTextureType& type) {
    return (mat->GetTextureCount(type) > 0);
}
