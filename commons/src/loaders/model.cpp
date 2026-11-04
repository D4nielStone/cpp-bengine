#include "resources/assimp_resource_io.hpp"
#include <os/resource_files.hpp>
#include "loaders/model.hpp"
#include "loaders/editable_model.hpp"
#include "loaders/material_asset.hpp"
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
            if (bgui::resource_files::exists(absolute_candidate) || bgui::resource_files::exists(candidate))
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
    advance_animation();
    draw_meshes();
}

void model::advance_animation() {
    if (m_pose && m_pose->options.animations && m_pose->scene->HasAnimations())
        update_animation(std::chrono::duration<double>(std::chrono::steady_clock::now() - m_animation_start).count());
}

void model::draw_meshes() {
    for(auto& mesh : meshes) {
        if (!mesh.isLoaded()) mesh.load();
        mesh.draw(getShader());
    }
}

shader& model::getShader() {
    if (!m_shader.ID)
        m_shader.compilar("pbr.vert", "pbr.frag");
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
    std::swap(m_source_missing, replacement.m_source_missing);
    m_animation_clip = replacement.m_animation_clip;
    m_animation_start = replacement.m_animation_start;
}

bool model::source_available() const {
    if (source_path.empty())
        return !meshes.empty();

    const bool available = bgui::resource_files::exists(source_path);
    if (!available && !m_source_missing)
        debugging::emit(alerta, "model", "Asset source is missing: " + source_path);
    m_source_missing = !available;
    return available;
}

std::unique_ptr<model> model::prepare(const std::string& path) {
    return prepare(path, nullptr);
}

std::unique_ptr<model> model::prepare(const std::string& path, const model_import_options* options) {
    auto result = std::unique_ptr<model>(new model(shader::deferred_t{}));
    result->load_model_data(path, false, options);
    if (result->meshes.empty()) throw std::runtime_error("Model has no geometry: " + path);
    return result;
}

void model::load_model_data(const std::string& path, bool upload, const model_import_options* options) {
    meshes.clear();
    m_pose.reset();
    m_source_missing = false;
    const auto resolved_path = resolve_model_path(path);
    source_path = resolved_path.string();
    const auto resolved_string = resolved_path.string();
    auto extension = resolved_path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".bmodel") {
        editable_model_data editable;
        std::string error;
        if (!load_editable_model(resolved_path, editable, error)) {
            if (!upload) throw std::runtime_error(error);
            debugging::emit(erro, error);
            return;
        }
        directory = resolved_path.parent_path().string();
        meshes.reserve(editable.meshes.size());
        for (auto& source : editable.meshes) {
            material model_material;
            model_material.set_uniform("use_tex_albedo", false);
            model_material.set_uniform("use_tex_metallic", false);
            model_material.set_uniform("use_tex_roughness", false);
            model_material.set_uniform("use_tex_normal", false);
            model_material.set_uniform("use_tex_ao", false);
            model_material.set_uniform("use_tex_height", false);
            model_material.set_uniform("material.albedo", color{0.8f, 0.8f, 0.8f, 1.f});
            model_material.set_uniform("material.metallic", 0.f);
            model_material.set_uniform("material.ao", 1.f);
            model_material.set_uniform("material.roughness", 0.5f);
            if (!source.material_path.empty()) {
                const std::filesystem::path reference(source.material_path);
                if (!load_material_asset(reference.is_absolute() ? reference : resolved_path.parent_path()/reference, model_material, error))
                    throw std::runtime_error(error);
            }
            meshes.emplace_back(
                source.vertices,
                source.triangulated_indices(),
                model_material
            );
        }
        return;
    }

    auto pose = std::make_shared<model_pose>();
    pose->options = options ? *options : read_model_import_options(resolved_path);
    if (bgui::resource_files::embedded()) pose->importer->SetIOHandler(new resource_io());
    pose->scene = pose->importer->ReadFile(resolved_string, pose->options.flags());
    if (!pose->scene || !pose->scene->mRootNode || !pose->scene->HasMeshes() ||
        (pose->scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        if (!upload) throw std::runtime_error(resolved_string + ": " + pose->importer->GetErrorString());
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
    embedded_textures decoded;
    for (const auto& instance : m_pose->instances)
        meshes.push_back(process_mesh(m_pose->scene->mMeshes[instance.mesh], m_pose->scene, decoded));
    update_animation(0);
    if (upload) for (auto& mesh : meshes) mesh.load();
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
    embedded_textures decoded;
    return process_mesh(mesh, scene, decoded);
}

mesh model::process_mesh(aiMesh* mesh, const aiScene* scene, embedded_textures& decoded) {
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
        bmat.set_texture("tex_albedo", load_texture(ai_material, aiTextureType_BASE_COLOR, decoded));
    else if(hasTextura(ai_material, aiTextureType_DIFFUSE))
        bmat.set_texture("tex_albedo", load_texture(ai_material, aiTextureType_DIFFUSE, decoded));

    if(hasTextura(ai_material, aiTextureType_METALNESS))
        bmat.set_texture("tex_metallic", load_texture(ai_material, aiTextureType_METALNESS, decoded));

    if(hasTextura(ai_material, aiTextureType_DIFFUSE_ROUGHNESS))
        bmat.set_texture("tex_roughness", load_texture(ai_material, aiTextureType_DIFFUSE_ROUGHNESS, decoded));

    if(hasTextura(ai_material, aiTextureType_NORMALS))
        bmat.set_texture("tex_normal", load_texture(ai_material, aiTextureType_NORMALS, decoded));

    if(hasTextura(ai_material, aiTextureType_AMBIENT_OCCLUSION))
        bmat.set_texture("tex_ao", load_texture(ai_material, aiTextureType_AMBIENT_OCCLUSION, decoded));

    if(hasTextura(ai_material, aiTextureType_HEIGHT))
        bmat.set_texture("tex_height", load_texture(ai_material, aiTextureType_HEIGHT, decoded));

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
    embedded_textures decoded;
    return load_texture(mat, type, decoded);
}

texture model::load_texture(aiMaterial* mat, const aiTextureType& type, embedded_textures& decoded) {
    texture tex;
    if (m_pose) tex.filtering = m_pose->options.filtering;
    if (hasTextura(mat, type)) {
        aiString str;
        mat->GetTexture(type, 0, &str);

        auto reference = std::string(str.C_Str());
        std::replace(reference.begin(), reference.end(), '\\', '/');
        if (m_pose) if (const auto* embedded = m_pose->scene->GetEmbeddedTexture(reference.c_str())) {
            tex.path = source_path + "#" + reference;
            if (const auto found = decoded.find(reference); found != decoded.end()) {
                tex.embedded = found->second.embedded;
                tex.imported_image = found->second.imported_image;
                tex.imported_image_revision = found->second.imported_image_revision;
                return tex;
            }
            tex.embedded = std::make_shared<bgui::texture>();
            std::shared_ptr<std::vector<unsigned char>> bytes;
            if (embedded->mHeight) {
                tex.embedded->m_size = {float(embedded->mWidth), float(embedded->mHeight)};
                tex.embedded->m_buffer.reserve(size_t(embedded->mWidth) * embedded->mHeight * 4);
                for (size_t i = 0; i < size_t(embedded->mWidth) * embedded->mHeight; ++i) {
                    const auto pixel = embedded->pcData[i];
                    for (auto c : {pixel.r, pixel.g, pixel.b, pixel.a}) tex.embedded->m_buffer.push_back(c);
                }
                tex.embedded->m_has_alpha = true;
            } else {
                const auto* first = reinterpret_cast<const unsigned char*>(embedded->pcData);
                bytes = std::make_shared<std::vector<unsigned char>>(first, first + embedded->mWidth);
                *tex.embedded = bgui::decode_texture_memory(*bytes, tex.path);
            }
            tex.embedded->m_path = tex.path;
            tex.update_embedded_revision();
            tex.imported_image = std::move(bytes);
            tex.imported_image_revision = tex.embedded->m_revision;
            decoded.emplace(reference, tex);
            return tex;
        }
        tex.path = resolve_model_texture(source_path, reference).string();
    }
    return tex;
}

bool model::hasTextura(aiMaterial* mat, const aiTextureType& type) {
    return (mat->GetTextureCount(type) > 0);
}
