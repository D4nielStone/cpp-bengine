#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <set>
#include <string>
#include <fstream>
#include <array>
#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>
#include "loaders/model_import.hpp"
#include "loaders/editable_model.hpp"
#include "loaders/material_asset.hpp"

namespace editor::ui {

inline bool path_inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

// Assimp opens sidecars such as .mtl and .bin without creating GPU resources.
class import_file_tracker final : public Assimp::IOSystem {
    Assimp::DefaultIOSystem filesystem;
public:
    std::set<std::filesystem::path> files;
    bool Exists(const char* path) const override { return filesystem.Exists(path); }
    char getOsSeparator() const override { return filesystem.getOsSeparator(); }
    void Close(Assimp::IOStream* stream) override { filesystem.Close(stream); }
    bool ComparePaths(const char* left, const char* right) const override { return filesystem.ComparePaths(left, right); }
    Assimp::IOStream* Open(const char* path, const char* mode = "rb") override {
        auto* stream = filesystem.Open(path, mode);
        if (stream) files.insert(std::filesystem::absolute(path).lexically_normal());
        return stream;
    }
};

inline std::set<std::filesystem::path> asset_dependencies(const std::filesystem::path& source, bool model) {
    namespace fs = std::filesystem;
    std::set<fs::path> files{source};
    auto extension=source.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (extension==".bmodel" || extension==".bmat") {
        const auto material_files = [&]() {
            std::vector<fs::path> references;
            if (extension==".bmat") references.push_back(source);
            else {
                commons::editable_model_data data; std::string error;
                if (!commons::load_editable_model(source,data,error)) throw std::runtime_error(error);
                for (const auto& mesh : data.meshes) if (!mesh.material_path.empty()) {
                    const fs::path ref(mesh.material_path);
                    if (ref.is_absolute()) throw std::runtime_error("Use referências de material relativas no pacote .bmodel.");
                    references.push_back((source.parent_path()/ref).lexically_normal());
                }
            }
            return references;
        }();
        for (const auto& path : material_files) {
            std::vector<fs::path> textures; std::string error;
            if (!commons::material_asset_dependencies(path,textures,error)) throw std::runtime_error(error);
            files.insert(fs::canonical(path));
            for (const auto& texture : textures) files.insert(fs::canonical(texture));
        }
        return files;
    }
    if (model) {
        const auto settings = commons::model_import_settings_path(source);
        if (fs::is_regular_file(settings)) files.insert(settings);
        Assimp::Importer importer;
        auto* tracker = new import_file_tracker();
        importer.SetIOHandler(tracker);
        const auto* scene = importer.ReadFile(source.string(), 0);
        if (!scene || !scene->HasMeshes())
            throw std::runtime_error("Não foi possível importar o modelo: " + std::string(importer.GetErrorString()));
        files.insert(tracker->files.begin(), tracker->files.end());
        for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
            for (int type = aiTextureType_DIFFUSE; type <= AI_TEXTURE_TYPE_MAX; ++type) {
                const auto texture_type = static_cast<aiTextureType>(type);
                for (unsigned j = 0; j < scene->mMaterials[i]->GetTextureCount(texture_type); ++j) {
                    aiString name;
                    if (scene->mMaterials[i]->GetTexture(texture_type, j, &name) != AI_SUCCESS ||
                        !name.length || scene->GetEmbeddedTexture(name.C_Str())) continue;
                    auto text = std::string(name.C_Str());
                    std::replace(text.begin(), text.end(), '\\', '/');
                    const auto texture = commons::resolve_model_texture(source, text);
                    if (fs::path(text).is_absolute() && texture == fs::path(text).lexically_normal())
                        throw std::runtime_error("O modelo usa uma textura com caminho absoluto não resolvido no pacote.");
                    if (!fs::is_regular_file(texture))
                        throw fs::filesystem_error("Textura do modelo não encontrada", texture,
                            std::make_error_code(std::errc::no_such_file_or_directory));
                    files.insert(texture);
                }
            }
        }
    }

    return files;
}

inline bool same_asset_bytes(const std::filesystem::path& a, const std::filesystem::path& b) {
    namespace fs = std::filesystem;
    if (!fs::is_regular_file(b) || fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    std::array<char, 8192> x{}, y{};
    while (left && right) {
        left.read(x.data(), x.size()); right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) return false;
    }
    return left.eof() && right.eof();
}

inline std::filesystem::path find_identical_asset(const std::filesystem::path& source,
    const std::filesystem::path& root, bool model,
    const std::set<std::filesystem::path>& files, const commons::model_import_options* options) {
    namespace fs = std::filesystem;
    const auto settings = commons::model_import_settings_path(source);
    const auto desired = model ? commons::serialize_model_import_options(
        options ? *options : commons::read_model_import_options(source)) : std::string{};
    for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != source.extension()) continue;
        const auto candidate = fs::canonical(entry.path());
        if (!path_inside(candidate, root) || !same_asset_bytes(source, candidate)) continue;
        if (!model) return candidate;
        try {
            if (commons::serialize_model_import_options(commons::read_model_import_options(candidate)) != desired) continue;
            auto dependencies = asset_dependencies(candidate, true);
            dependencies.erase(commons::model_import_settings_path(candidate));
            if (dependencies.size() != files.size() - files.count(settings)) continue;
            bool equal = true;
            for (const auto& file : files) {
                if (file == settings) continue;
                const auto target = file == source ? candidate :
                    (candidate.parent_path() / file.lexically_relative(source.parent_path())).lexically_normal();
                if (!path_inside(fs::weakly_canonical(target), root) || !dependencies.contains(target) ||
                    !same_asset_bytes(file, target)) { equal = false; break; }
            }
            if (equal) return candidate;
        } catch (const std::exception&) { /* A broken existing bundle cannot be reused. */ }
    }
    return {};
}

inline std::filesystem::path import_asset(const std::filesystem::path& requested,
    const std::filesystem::path& project, bool model, const std::filesystem::path& destination_folder = {},
    bool another_copy = false, const commons::model_import_options* options = nullptr) {
    namespace fs = std::filesystem;
    const auto source = fs::canonical(requested);
    const auto root = fs::canonical(project);
    if (!fs::is_regular_file(source))
        throw fs::filesystem_error("Solte um arquivo", source,
            std::make_error_code(std::errc::invalid_argument));
    if (path_inside(source, root) && !another_copy) return source;

    const auto files = asset_dependencies(source, model);
    const auto assets = destination_folder.empty() ? root / "assets" : fs::canonical(destination_folder);
    if (!path_inside(assets, root))
        throw std::runtime_error("Escolha uma pasta dentro do projeto.");
    if (!another_copy) {
        const auto existing = find_identical_asset(source, root, model, files, options);
        if (!existing.empty()) return existing;
    }
    fs::create_directories(assets);
    if (!path_inside(fs::canonical(assets), root))
        throw std::runtime_error("A pasta assets precisa estar dentro do projeto.");
    // A separate bundle preserves relative references between models and sidecars.
    const bool bundle = files.size() > 1;
    auto common = source.parent_path();
    for (const auto& file : files)
        while (!path_inside(file, common) && common.has_relative_path()) common = common.parent_path();
    auto destination = assets / (bundle ? source.stem() : source.filename());
    for (unsigned suffix = 1; fs::exists(destination); ++suffix)
        destination = assets / (source.stem().string() + "_" + std::to_string(suffix) +
            (bundle ? std::string{} : source.extension().string()));
    if (!bundle) {
        fs::copy_file(source, destination); // Never overwrite existing assets.
        return destination;
    }
    if (!fs::create_directory(destination))
        throw std::runtime_error("A pasta de importação já existe.");
    try {
        for (const auto& file : files) {
            const auto target = destination / file.lexically_relative(common);
            fs::create_directories(target.parent_path());
            fs::copy_file(file, target);
        }
        auto extension=source.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (extension==".bmodel" || extension==".bmat") {
            for (const auto& dependency : asset_dependencies(destination/source.lexically_relative(common),model))
                if (!path_inside(dependency,destination)) throw std::runtime_error("Dependência nativa fora do pacote importado.");
            return destination/source.lexically_relative(common);
        }
        Assimp::Importer verification;
        auto* tracker = new import_file_tracker();
        verification.SetIOHandler(tracker);
        if (!verification.ReadFile((destination / source.lexically_relative(common)).string(), 0))
            throw std::runtime_error("O modelo não pode ser carregado após a cópia.");
        for (const auto& opened : tracker->files)
            if (!path_inside(opened, destination))
                throw std::runtime_error("O modelo usa arquivos auxiliares com caminhos absolutos. Use caminhos relativos antes de importar.");
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(destination, ignored);
        throw;
    }
    return destination / source.lexically_relative(common);
}
}
