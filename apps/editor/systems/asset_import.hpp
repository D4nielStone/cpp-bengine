#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <set>
#include <string>
#include <assimp/DefaultIOSystem.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

namespace editor::ui {

inline bool path_inside(const std::filesystem::path& path, const std::filesystem::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

// Assimp opens sidecars such as .mtl and .bin without creating GPU resources.
class import_file_tracker final : public Assimp::DefaultIOSystem {
public:
    std::set<std::filesystem::path> files;
    Assimp::IOStream* Open(const char* path, const char* mode = "rb") override {
        auto* stream = Assimp::DefaultIOSystem::Open(path, mode);
        if (stream) files.insert(std::filesystem::absolute(path).lexically_normal());
        return stream;
    }
};

inline std::set<std::filesystem::path> asset_dependencies(const std::filesystem::path& source, bool model) {
    namespace fs = std::filesystem;
    std::set<fs::path> files{source};
    if (model) {
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
                        !name.length || name.C_Str()[0] == '*') continue;
                    auto text = std::string(name.C_Str());
                    std::replace(text.begin(), text.end(), '\\', '/');
                    const auto reference = fs::path(text);
                    if (reference.is_absolute())
                        throw std::runtime_error("O modelo usa uma textura com caminho absoluto. Use caminhos relativos antes de importar.");
                    const auto texture = fs::absolute(source.parent_path() / reference).lexically_normal();
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

inline std::filesystem::path import_asset(const std::filesystem::path& requested,
    const std::filesystem::path& project, bool model, const std::filesystem::path& destination_folder = {}) {
    namespace fs = std::filesystem;
    const auto source = fs::canonical(requested);
    const auto root = fs::canonical(project);
    if (!fs::is_regular_file(source))
        throw fs::filesystem_error("Solte um arquivo", source,
            std::make_error_code(std::errc::invalid_argument));
    if (path_inside(source, root)) return source;

    const auto files = asset_dependencies(source, model);

    const auto assets = destination_folder.empty() ? root / "assets" : fs::canonical(destination_folder);
    if (!path_inside(assets, root))
        throw std::runtime_error("Escolha uma pasta dentro do projeto.");
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
