#include <os/resource_files.hpp>
#pragma once
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

namespace commons {
// Ordinal node paths distinguish duplicate and unnamed nodes in the source scene.
struct model_import_options {
    bool flip_uvs = true, smooth_normals = true, join_vertices = true;
    bool skins = true, skeleton = true, animations = true;
    double scale = 1.0;
    std::set<std::string> excluded_nodes;
    std::set<unsigned> excluded_meshes, excluded_animations;
    unsigned flags() const {
        return aiProcess_Triangulate | (flip_uvs ? aiProcess_FlipUVs : 0) |
            (smooth_normals ? aiProcess_GenSmoothNormals : 0) |
            (join_vertices ? aiProcess_JoinIdenticalVertices : 0);
    }
};
inline std::filesystem::path model_import_settings_path(const std::filesystem::path& p) {
    return p.string() + ".import.json";
}
inline model_import_options read_model_import_options(const std::filesystem::path& p) {
    model_import_options o;
    const auto settings = model_import_settings_path(p);
    if (!bgui::resource_files::exists(settings)) return o;
    auto in = bgui::resource_files::stream(settings);
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    rapidjson::Document d;
    d.Parse(text.c_str());
    if (!in || d.HasParseError() || !d.IsObject() || !d.HasMember("version") ||
        !d["version"].IsInt() || d["version"].GetInt() != 1)
        throw std::runtime_error("Configuração de importação inválida ou versão incompatível.");
    for (auto [name, value] : {std::pair{"flip_uvs", &o.flip_uvs}, {"smooth_normals", &o.smooth_normals},
        {"join_vertices", &o.join_vertices}, {"skins", &o.skins}, {"skeleton", &o.skeleton}, {"animations", &o.animations}}) {
        if (d.HasMember(name)) {
            if (!d[name].IsBool()) throw std::runtime_error("Opção de importação inválida.");
            *value = d[name].GetBool();
        }
    }
    if (d.HasMember("scale")) {
        if (!d["scale"].IsNumber() || !(d["scale"].GetDouble() > 0) || d["scale"].GetDouble() > 1000000)
            throw std::runtime_error("Escala de importação inválida.");
        o.scale = d["scale"].GetDouble();
    }
    for (const char* name : {"excluded_nodes", "excluded_meshes", "excluded_animations"}) {
        if (!d.HasMember(name)) continue;
        if (!d[name].IsArray()) throw std::runtime_error("Seleção de importação inválida.");
        for (const auto& v : d[name].GetArray()) {
            if (std::string(name) == "excluded_nodes" && v.IsString()) o.excluded_nodes.insert(v.GetString());
            else if (std::string(name) == "excluded_meshes" && v.IsUint()) o.excluded_meshes.insert(v.GetUint());
            else if (std::string(name) == "excluded_animations" && v.IsUint()) o.excluded_animations.insert(v.GetUint());
            else throw std::runtime_error("Seleção de importação inválida.");
        }
    }
    return o;
}
inline std::string serialize_model_import_options(const model_import_options& o) {
    rapidjson::StringBuffer b;
    rapidjson::Writer<rapidjson::StringBuffer> w(b);
    w.StartObject(); w.Key("version"); w.Int(1);
    for (auto [name, value] : {std::pair{"flip_uvs", o.flip_uvs}, {"smooth_normals", o.smooth_normals},
        {"join_vertices", o.join_vertices}, {"skins", o.skins}, {"skeleton", o.skeleton}, {"animations", o.animations}}) { w.Key(name); w.Bool(value); }
    w.Key("scale"); w.Double(o.scale);
    w.Key("excluded_nodes"); w.StartArray(); for (const auto& v : o.excluded_nodes) w.String(v.c_str()); w.EndArray();
    w.Key("excluded_meshes"); w.StartArray(); for (auto v : o.excluded_meshes) w.Uint(v); w.EndArray();
    w.Key("excluded_animations"); w.StartArray(); for (auto v : o.excluded_animations) w.Uint(v); w.EndArray();
    w.EndObject(); return b.GetString();
}
inline bool supports_model_import(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    // Recognize common sources even when this Assimp build cannot decode them,
    // so the editor can show the importer and its diagnostic instead of ignoring a drop.
    if (extension == ".glb" || extension == ".gltf" || extension == ".fbx" ||
        extension == ".obj" || extension == ".blend" || extension == ".dae" || extension == ".3ds") return true;
    Assimp::Importer importer;
    return !extension.empty() && importer.IsExtensionSupported(extension);
}
inline void validate_model_selection(const aiScene& scene, const model_import_options& o) {
    unsigned selected = 0;
    const auto visit = [&](const auto& self, const aiNode* node, const std::string& path) -> void {
        if (o.excluded_nodes.contains(path)) return;
        for (unsigned i = 0; i < node->mNumMeshes; ++i)
            if (!o.excluded_meshes.contains(node->mMeshes[i])) ++selected;
        for (unsigned i = 0; i < node->mNumChildren; ++i) self(self, node->mChildren[i], path + "/" + std::to_string(i));
    };
    if (scene.mRootNode) visit(visit, scene.mRootNode, "0");
    if (!selected) throw std::runtime_error("Selecione pelo menos uma malha em um node habilitado.");
}
}
